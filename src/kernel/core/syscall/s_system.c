#include "syscall_internal.h"

#include "arch/x86_64/acpi.h"
#include "kernel/block.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/module.h"
#include "kernel/process.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

#include <string.h>

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
	default: return ERRNO_EINVAL;
	}
}

struct interrupt_frame *
syscall_system_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
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
			syscall_error(frame, ERRNO_EINVAL);
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
				syscall_error(frame, ERRNO_EINVAL);
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
					syscall_error(frame, ERRNO_EINVAL);
					break;
				}
				memcpy(name, canonical + begin, name_length);
				name[name_length] = '\0';
			}
			result = vfs_open_as(canonical, VFS_O_RDONLY, 0, 0, 0, &file);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			image = kheap_alloc(image_size);
			if (image == NULL) {
				fd_object_put(file);
				syscall_error(frame, ERRNO_ENOMEM);
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
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
	return frame;
}
