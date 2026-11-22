#include "syscall_internal.h"
#include "abi/sysctl.h"
#include "kernel/pmm.h"
#include "kernel/fd.h"
#include "kernel/smp.h"
#include "kernel/version.h"

#include "arch/x86_64/acpi.h"
#include "kernel/block.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/module.h"
#include "kernel/process.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

#include <string.h>

struct sysctl_value {
	union {
		int32_t integer;
		uint64_t wide;
	} number;
	const void *data;
	size_t size;
};

static int
sysctl_value(const int name[2], struct sysctl_value *value)
{
	const char *text = NULL;
	if (name[0] == CTL_KERN) {
		switch (name[1]) {
		case KERN_OSTYPE: text = KERNEL_OSTYPE; break;
		case KERN_OSRELEASE: text = KERNEL_OSRELEASE; break;
		case KERN_VERSION: text = KERNEL_VERSION; break;
		case KERN_HOSTNAME: text = KERNEL_HOSTNAME; break;
		case KERN_MAXFILESPERPROC:
			value->number.integer = FD_TABLE_CAPACITY;
			break;
		default: return ENOENT;
		}
	} else if (name[0] == CTL_VM && name[1] == VM_FREEPAGES) {
		value->number.wide = pmm_free_pages();
		value->data = &value->number.wide;
		value->size = sizeof(value->number.wide);
		return 0;
	} else if (name[0] == CTL_HW) {
		switch (name[1]) {
		case HW_MACHINE:
		case HW_MACHINE_ARCH:
			text = KERNEL_MACHINE;
			break;
		case HW_NCPU:
			value->number.integer = (int32_t)smp_online_count();
			break;
		case HW_BYTEORDER:
			value->number.integer = 1234;
			break;
		case HW_PAGESIZE:
			value->number.integer = PMM_PAGE_SIZE;
			break;
		case HW_PHYSMEM:
			if (pmm_usable_pages() > UINT64_MAX / PMM_PAGE_SIZE)
				return EOVERFLOW;
			value->number.wide = pmm_usable_pages() * PMM_PAGE_SIZE;
			value->data = &value->number.wide;
			value->size = sizeof(value->number.wide);
			return 0;
		default: return ENOENT;
		}
	} else {
		return ENOENT;
	}
	if (text == NULL) {
		value->data = &value->number.integer;
		value->size = sizeof(value->number.integer);
		return 0;
	}
	value->data = text;
	value->size = strlen(text) + 1;
	return 0;
}

static struct interrupt_frame *
syscall_sysctl(struct interrupt_frame *frame)
{
	struct address_space *space = vmm_active_address_space();
	int name[2];
	struct sysctl_value value;
	uint64_t capacity = 0;
	if (frame->rsi != 2) {
		syscall_error(frame, EINVAL);
		return frame;
	}
	if (!vmm_copy_from_user(space, name, frame->rdi, sizeof(name))) {
		syscall_error(frame, EFAULT);
		return frame;
	}
	int error = sysctl_value(name, &value);
	if (error != 0) {
		syscall_error(frame, error);
		return frame;
	}
	if (frame->r8 != 0 || frame->r9 != 0) {
		syscall_error(frame, frame->r8 != 0 ? EPERM : EINVAL);
		return frame;
	}
	if (frame->r10 == 0) {
		if (frame->rdx != 0)
			syscall_error(frame, EFAULT);
		else
			frame->rax = 0;
		return frame;
	}
	if (!vmm_validate_user_write(space, frame->r10, sizeof(capacity)) ||
	    (frame->rdx != 0 && !vmm_copy_from_user(space, &capacity,
	    frame->r10, sizeof(capacity)))) {
		syscall_error(frame, EFAULT);
		return frame;
	}
	uint64_t amount = frame->rdx == 0 ? value.size :
	    capacity < value.size ? capacity : value.size;
	if (frame->rdx != 0 && amount != 0 &&
	    !vmm_copy_to_user(space, frame->rdx, value.data, (size_t)amount)) {
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (!vmm_copy_to_user(space, frame->r10, &amount, sizeof(amount))) {
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (frame->rdx != 0 && capacity < value.size)
		syscall_error(frame, ENOMEM);
	else
		frame->rax = 0;
	return frame;
}

static uint64_t
errno_from_module(enum kernel_module_error error)
{
	switch (error) {
	case KERNEL_MODULE_OK: return 0;
	case KERNEL_MODULE_NO_MEMORY: return 12U;
	case KERNEL_MODULE_ALREADY_LOADED: return 17U;
	case KERNEL_MODULE_NOT_FOUND: return 2U;
	case KERNEL_MODULE_BUSY: return 16U;
	case KERNEL_MODULE_INIT_FAILED: return 6U;
	case KERNEL_MODULE_IO_ERROR: return 5U;
	case KERNEL_MODULE_UNSUPPORTED:
	case KERNEL_MODULE_UNSUPPORTED_RELOCATION: return 95U;
	default: return EINVAL;
	}
}

struct interrupt_frame *
syscall_system_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
	case SYSCALL_SYSCTL:
		return syscall_sysctl(frame);
	case SYSCALL_KERNEL_CRASH:
		{
			struct os_credentials credentials;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			klog("kernel crash requested by shell\n");
			kernel_halt();
		}
	case SYSCALL_POWER_CONTROL:
		{
			struct os_credentials credentials;
			if (!process_current_credentials(&credentials) ||
			    (credentials.effective_uid != 0 &&
			    !fd_object_is_console(fd_object_for_descriptor(current_fd_table(), 0)))) {
				syscall_error(frame, 1U);
				break;
			}
		}
		if (frame->rdi == POWER_CONTROL_POWEROFF) {
			/* The local console user can shut down the machine. Flush
			 * registered disks before invoking firmware S5. */
			for (size_t i = 0; i < block_device_count(); i++) {
				if (block_flush(block_device_at(i)) != 0) {
					syscall_error(frame, EIO);
					return frame;
				}
			}
			(void)acpi_poweroff();
			/* Compatibility fallback for emulators without usable ACPI. */
			__asm__ volatile ("outw %0, %1" : : "a" ((uint16_t)0x2000),
			    "Nd" ((uint16_t)0x0604));
			__asm__ volatile ("outw %0, %1" : : "a" ((uint16_t)0x2000),
			    "Nd" ((uint16_t)0xb004));
		} else if (frame->rdi == POWER_CONTROL_REBOOT) {
			uint8_t reset = 0x02;
			__asm__ volatile ("outb %0, %1" : : "a" (reset),
			    "Nd" ((uint16_t)0x0cf9));
			for (volatile unsigned int delay = 0; delay < 100000; delay++)
				__asm__ volatile ("pause");
			reset = 0x06;
			__asm__ volatile ("outb %0, %1" : : "a" (reset),
			    "Nd" ((uint16_t)0x0cf9));
		} else {
			syscall_error(frame, EINVAL);
			break;
		}
		syscall_error(frame, EIO);
		break;
	case SYSCALL_KLDLOAD:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			char name[KERNEL_MODULE_NAME_MAX + 1U];
			struct os_credentials credentials;
			struct vnode *node = NULL;
			struct fd_object *file = NULL;
			void *image = NULL;
			size_t image_size, offset = 0, name_length;
			int result;
			enum kernel_module_error module_error;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0)
				result = vfs_lookup_as(canonical, 0, 0, &node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (node->type != VNODE_REGULAR || node->size == 0 ||
			    node->size > 8U * 1024U * 1024U || node->size > SIZE_MAX) {
				vnode_put(node);
				syscall_error(frame, EINVAL);
				break;
			}
			image_size = (size_t)node->size;
			vnode_put(node);
			name_length = strlen(canonical);
			while (name_length != 0 && canonical[name_length - 1U] != '/')
				name_length--;
			{
				size_t begin = name_length;
				name_length = strlen(canonical) - begin;
				if (name_length > 3U &&
				    canonical[begin + name_length - 3U] == '.' &&
				    canonical[begin + name_length - 2U] == 'k' &&
				    canonical[begin + name_length - 1U] == 'o')
					name_length -= 3U;
				if (name_length == 0 || name_length > KERNEL_MODULE_NAME_MAX) {
					syscall_error(frame, EINVAL);
					break;
				}
				memcpy(name, canonical + begin, name_length);
				name[name_length] = '\0';
			}
			result = vfs_open_as(canonical, OS_O_RDONLY, 0, 0, 0, &file);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			image = kheap_alloc(image_size);
			if (image == NULL) {
				fd_object_put(file);
				syscall_error(frame, ENOMEM);
				break;
			}
			while (offset < image_size) {
				size_t amount = 0;
				result = fd_object_read(file, (uint8_t *)image + offset,
				    image_size - offset, &amount);
				if (result != 0 || amount == 0)
					break;
				offset += amount;
			}
			fd_object_put(file);
			if (result != 0 || offset != image_size) {
				(void)kheap_free(image);
				syscall_error(frame, result == 0 ? 5U :
				    errno_from_kernel(result));
				break;
			}
			module_error = kernel_module_load(name, image, image_size);
			(void)kheap_free(image);
			if (module_error != KERNEL_MODULE_OK)
				syscall_error(frame, errno_from_module(module_error));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_KLDUNLOAD:
		{
			char name[KERNEL_MODULE_NAME_MAX + 1U];
			struct os_credentials credentials;
			enum kernel_module_error module_error;
			int result;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			result = copy_user_string(frame->rdi, name, sizeof(name));
			if (result != 0) {
				syscall_error(frame, (uint64_t)-result);
				break;
			}
			module_error = kernel_module_unload(name);
			if (module_error != KERNEL_MODULE_OK)
				syscall_error(frame, errno_from_module(module_error));
			else
				frame->rax = 0;
		}
		break;
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
