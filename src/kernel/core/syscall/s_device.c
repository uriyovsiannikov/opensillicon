#include "syscall_internal.h"

#include "abi/disk.h"
#include "abi/audio.h"
#include "kernel/hda_audio.h"
#include "kernel/fd.h"
#include "abi/framebuffer.h"
#include "abi/input.h"
#include "abi/ioctl.h"
#include "abi/termios.h"
#include "arch/x86_64/ps2_keyboard.h"
#include "kernel/block.h"
#include "kernel/framebuffer_device.h"
#include "kernel/process.h"
#include "kernel/pty.h"
#include "kernel/tcp_socket.h"
#include "kernel/tty.h"
#include "kernel/usb_host.h"
#include "kernel/unix_socket.h"
#include "kernel/vmm.h"

#define TIOCGWINSZ_REQUEST 0x5413U
#define OS_FIONBIO 0x5421U
#define TCGETS_REQUEST 0x5401U
#define TCSETS_REQUEST 0x5402U
#define TIOCSRAW_REQUEST 0x4f530001U
#define TIOCCANON_REQUEST 0x4f530002U
#define TIOCSFB_REQUEST 0x4f530003U
struct syscall_winsize {
	uint16_t rows;
	uint16_t columns;
	uint16_t xpixel;
	uint16_t ypixel;
};

struct interrupt_frame *
syscall_device_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	struct fd_table *table;

	switch (number) {
	case SYSCALL_FRAMEBUFFER_INFO:
		{
			struct os_framebuffer_info info;
			struct address_space *space = vmm_active_address_space();

			if (!framebuffer_device_info(&info)) {
				syscall_error(frame, ENODEV);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi, sizeof(info)) ||
			    !vmm_copy_to_user(space, frame->rdi, &info, sizeof(info))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_FRAMEBUFFER_PRESENT:
		{
			int result = framebuffer_device_present(frame->rdi,
			    (size_t)frame->rsi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FRAMEBUFFER_PRESENT_RECT:
		{
			struct os_framebuffer_rect rect;
			int result;

			if (!vmm_copy_from_user(vmm_active_address_space(), &rect,
			    frame->rdi, sizeof(rect))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = framebuffer_device_present_rect(&rect);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_KEYBOARD_POLL:
		{
			struct os_credentials credentials;
			struct terminal_key_event event;
			struct os_keyboard_event output;
			struct address_space *space = vmm_active_address_space();

			if (!process_current_credentials(&credentials) ||
			    (credentials.effective_uid != 0 &&
			    !tty_console_input_owned(process_current_pid()))) {
				syscall_error(frame, 1U);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi, sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!tty_keyboard_available()) {
				syscall_error(frame, ENODEV);
				break;
			}
			if (!ps2_keyboard_event_poll(&event) && !usb_host_keyboard_poll(&event)) {
				frame->rax = 0;
				break;
			}
			output = (struct os_keyboard_event){
				.scancode = event.scancode,
				.extended = event.extended,
				.pressed = event.pressed,
				.reserved = 0
			};
			if (!vmm_copy_to_user(space, frame->rdi, &output,
			    sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 1;
		}
		break;
	case SYSCALL_MOUSE_POLL:
		{
			struct os_credentials credentials;
			struct os_mouse_event output;
			static uint8_t ps2_buttons, usb_buttons;
			struct address_space *space = vmm_active_address_space();

			if (!process_current_credentials(&credentials) ||
			    (credentials.effective_uid != 0 &&
			    !tty_console_input_owned(process_current_pid()))) {
				syscall_error(frame, 1U);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!ps2_mouse_available() && !usb_host_available()) {
				syscall_error(frame, ENODEV);
				break;
			}
			if (ps2_mouse_event_poll(&output)) {
				ps2_buttons = output.buttons;
			} else if (usb_host_mouse_poll(&output)) {
				usb_buttons = output.buttons;
			} else {
				frame->rax = 0;
				break;
			}
			output.buttons = ps2_buttons | usb_buttons;
			if (!vmm_copy_to_user(space, frame->rdi, &output,
			    sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 1;
		}
		break;
	case SYSCALL_PTY_PAIR:
		{
			uint32_t descriptors[2];
			int read_descriptor;
			int write_descriptor;
			int result;

			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdi, sizeof(descriptors))) {
				syscall_error(frame, EFAULT);
				break;
			}
			table = current_fd_table();
			result = pty_create(table, &read_descriptor, &write_descriptor);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			descriptors[0] = (uint32_t)read_descriptor;
			descriptors[1] = (uint32_t)write_descriptor;
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    descriptors, sizeof(descriptors))) {
				(void)fd_close(table, (uint64_t)read_descriptor);
				(void)fd_close(table, (uint64_t)write_descriptor);
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_IOCTL:
		{
			struct syscall_winsize window = { 0 };
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);

			if (object == NULL) {
				syscall_error(frame, 9);
				break;
			}
            if (frame->rsi == OS_AUDIO_GETINFO || frame->rsi == OS_AUDIO_SETVOLUME ||
                frame->rsi == OS_AUDIO_DRAIN || frame->rsi == OS_AUDIO_RESET) {
                if (!hda_audio_is_object(object)) { syscall_error(frame, ENOTTY); break; }
                struct os_audio_info info={0};uint32_t volume=0;
                void *payload=NULL;
                if(frame->rsi==OS_AUDIO_GETINFO) {
                    if(!vmm_validate_user_write(vmm_active_address_space(),frame->rdx,sizeof(info))) {
                        syscall_error(frame, EFAULT); break;
                    }
                    payload=&info;
                } else if(frame->rsi==OS_AUDIO_SETVOLUME) {
                    if(!vmm_copy_from_user(vmm_active_address_space(),&volume,frame->rdx,sizeof(volume))) {
                        syscall_error(frame, EFAULT); break;
                    }
                    payload=&volume;
                }
                int result=object->operations->ioctl(object->context,frame->rsi,payload);
                if(result==-EAGAIN && frame->rsi==OS_AUDIO_DRAIN && !fd_object_nonblocking(object))
                    return process_block_on_channel(frame,fd_readiness_wait_channel());
                if(result) {syscall_error(frame,errno_from_kernel(result));break;}
                if(frame->rsi==OS_AUDIO_GETINFO &&
                    !vmm_copy_to_user(vmm_active_address_space(),frame->rdx,&info,sizeof(info))) {
                    syscall_error(frame,EFAULT);break;
                }
                frame->rax=0;break;
            }
			if (frame->rsi == OS_DIOCGSECTORSIZE ||
			    frame->rsi == OS_DIOCGMEDIASIZE ||
			    frame->rsi == OS_DIOCGFLUSH ||
			    frame->rsi == OS_DIOCGPARTINFO ||
			    frame->rsi == OS_DIOCREREADGPT ||
			    frame->rsi == OS_DIOCGMODEL ||
			    frame->rsi == OS_DIOCGIDENT) {
				if (frame->rsi == OS_DIOCREREADGPT) {
					struct os_credentials credentials;
					if (!process_current_credentials(
						&credentials) ||
					    credentials.effective_uid != 0) {
						syscall_error(frame, 1);
						break;
					}
				}

				union {
					uint32_t sector;
					int64_t media;
					struct os_disk_partinfo part;
					char text[256];
				} value = {0};
				void *payload = frame->rsi == OS_DIOCGSECTORSIZE
				    ? (void *)&value.sector
				    : frame->rsi == OS_DIOCGPARTINFO
				    ? (void *)&value.part
				    : (frame->rsi == OS_DIOCGMODEL ||
					  frame->rsi == OS_DIOCGIDENT)
				    ? (void *)value.text
				    : (void *)&value.media;
				size_t size = frame->rsi == OS_DIOCGSECTORSIZE
				    ? 4
				    : frame->rsi == OS_DIOCGMEDIASIZE ? 8
				    : frame->rsi == OS_DIOCGPARTINFO
				    ? sizeof(value.part)
				    : frame->rsi == OS_DIOCGMODEL ? 41
				    : frame->rsi == OS_DIOCGIDENT ? 256
								  : 0;
				struct address_space *space =
				    vmm_active_address_space();
				if (size &&
				    !vmm_validate_user_write(
					space, frame->rdx, size)) {
					syscall_error(frame, EFAULT);
					break;
				}
				int result = object->operations->ioctl
				    ? object->operations->ioctl(
					  object->context, frame->rsi, payload)
				    : -ENOTTY;
				if (result != 0)
					syscall_error(
					    frame, errno_from_kernel(result));
				else if (size &&
				    !vmm_copy_to_user(
					space, frame->rdx, payload, size))
					syscall_error(frame, EFAULT);
				else
					frame->rax = 0;
				break;
			}
			if (frame->rsi == OS_FIONBIO) {
				int enabled;
				if (!vmm_copy_from_user(vmm_active_address_space(),
				    &enabled, frame->rdx, sizeof(enabled))) {
					syscall_error(frame, EFAULT);
					break;
				}
				if (enabled)
					__atomic_fetch_or(&object->status_flags, OS_O_NONBLOCK, __ATOMIC_RELAXED);
				else
					__atomic_fetch_and(&object->status_flags, ~OS_O_NONBLOCK, __ATOMIC_RELAXED);
				fd_readiness_wake();
				frame->rax = 0;
				break;
			}
			if (frame->rsi == OS_FIONREAD) {
				size_t available;
				int result;
				if (!(unix_socket_is_object(object) || pty_is_object(object))) {
					syscall_error(frame, ENOTTY);
					break;
				}
				if (!vmm_validate_user_write(vmm_active_address_space(),
				    frame->rdx, sizeof(int))) {
					syscall_error(frame, EFAULT);
					break;
				}
				result = pty_is_object(object) ? pty_bytes_available(object, &available) :
				    unix_socket_bytes_available(object, &available);
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				int value = (int)available;
				if (!vmm_copy_to_user(vmm_active_address_space(),
				    frame->rdx, &value, sizeof(value))) {
					syscall_error(frame, EFAULT);
					break;
				}
				frame->rax = 0;
				break;
			}
			if (frame->rsi == OS_TIOCSINPUT) {
				int acquire;

				if (!fd_object_is_console(object)) {
					syscall_error(frame, ENOTTY);
					break;
				}
				if (!vmm_copy_from_user(vmm_active_address_space(),
				    &acquire, frame->rdx, sizeof(acquire))) {
					syscall_error(frame, EFAULT);
					break;
				}
				if (acquire != 0 && acquire != 1) {
					syscall_error(frame, EINVAL);
					break;
				}
				if (!tty_console_input_set(acquire != 0,
				    process_current_pid(), process_current_ppid())) {
					syscall_error(frame, 1U);
					break;
				}
				frame->rax = 0;
				break;
			}
			if (frame->rsi == TIOCSFB_REQUEST) {
				int enabled;

				if (!fd_object_is_console(object)) {
					syscall_error(frame, ENOTTY);
					break;
				}
				if (!vmm_copy_from_user(vmm_active_address_space(), &enabled,
				    frame->rdx, sizeof(enabled))) {
					syscall_error(frame, EFAULT);
					break;
				}
				if ((enabled != 0 && enabled != 1) ||
				    !tty_console_framebuffer_set(enabled != 0,
				    process_current_pid())) {
					syscall_error(frame, enabled != 0 && enabled != 1 ?
					    EINVAL : 1U);
					break;
				}
				frame->rax = 0;
				break;
			}
			if (pty_is_object(object) &&
			    (frame->rsi == TIOCGWINSZ_REQUEST || frame->rsi == 0x5414U)) {
				uint16_t dimensions[4];
				bool set = frame->rsi == 0x5414U;
				struct address_space *space = vmm_active_address_space();
				if ((set && !vmm_copy_from_user(space, dimensions,
				    frame->rdx, sizeof(dimensions))) ||
				    (!set && !vmm_validate_user_write(space, frame->rdx,
				    sizeof(dimensions)))) {
					syscall_error(frame, EFAULT);
					break;
				}
				(void)pty_window(object, dimensions, set);
				if (!set && !vmm_copy_to_user(space, frame->rdx,
				    dimensions, sizeof(dimensions)))
					syscall_error(frame, EFAULT);
				else
					frame->rax = 0;
				break;
			}
			if ((pty_is_object(object) || fd_object_is_terminal(object)) &&
			    (frame->rsi == 0x5409U || frame->rsi == 0x540bU)) {
				int result = 0;
				if (frame->rsi == 0x5409U) {
					/* Writes already reach the console or the PTY peer's receive
					 * queue synchronously; there is no pending transmit queue. */
					if (frame->rdx != 1) result = -EOPNOTSUPP;
				} else if (frame->rdx > 2) result = -EINVAL;
				else if (frame->rdx != 1)
					result = pty_is_object(object) ? pty_flush_input(object) : tty_flush_input();
				if (result) syscall_error(frame, errno_from_kernel(result));
				else frame->rax = 0;
				break;
			}
			if ((pty_is_object(object) || fd_object_is_terminal(object)) &&
			    (frame->rsi >= TCGETS_REQUEST && frame->rsi <= 0x5404U)) {
				struct os_termios attributes;
				bool set = frame->rsi != TCGETS_REQUEST;
				struct address_space *space = vmm_active_address_space();
				int result;

				if ((set && !vmm_copy_from_user(space, &attributes,
				    frame->rdx, sizeof(attributes))) ||
				    (!set && !vmm_validate_user_write(space, frame->rdx,
				    sizeof(attributes)))) {
					syscall_error(frame, EFAULT);
					break;
				}
				if (frame->rsi == 0x5404U) {
					result = pty_is_object(object) ? pty_flush_input(object) : tty_flush_input();
					if (result) { syscall_error(frame, errno_from_kernel(result)); break; }
				}
				if (pty_is_object(object))
					result = pty_termios(object, &attributes, set);
				else if (set)
					result = tty_set_raw_mode((attributes.local_flags & 2U) == 0);
				else {
					/* The native console exposes canonical/echo or raw input;
					 * PTYs retain their more complete termios implementation. */
					attributes = (struct os_termios){
						.input_flags = tty_raw_mode() ? 0 : 0x100U,
						.output_flags = 5U,
						.control_flags = 0xb0U,
						.local_flags = tty_raw_mode() ? 0 : 0xbU,
						.control_chars = {3, 28, 127, 21, 4, 0, 1}
					};
					result = 0;
				}
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				if (!set && !vmm_copy_to_user(space, frame->rdx,
				    &attributes, sizeof(attributes)))
					syscall_error(frame, EFAULT);
				else
					frame->rax = 0;
				break;
			}
			if (!fd_object_is_terminal(object)) {
				syscall_error(frame, ENOTTY);
				break;
			}
			if (frame->rsi == TIOCSRAW_REQUEST ||
			    frame->rsi == TIOCCANON_REQUEST) {
				if (pty_is_object(object)) {
					int result = pty_set_raw(object, frame->rsi == TIOCSRAW_REQUEST);
					if (result != 0) syscall_error(frame, errno_from_kernel(result));
					else frame->rax = 0;
					break;
				}
				int result = tty_set_raw_mode(frame->rsi == TIOCSRAW_REQUEST);
				if (result != 0)
					syscall_error(frame, errno_from_kernel(result));
				else
					frame->rax = 0;
				break;
			}
			if (frame->rsi != TIOCGWINSZ_REQUEST) {
				syscall_error(frame, ENOTTY);
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdx, sizeof(window))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!tty_dimensions(&window.rows, &window.columns) ||
			    !vmm_copy_to_user(vmm_active_address_space(), frame->rdx,
			    &window, sizeof(window))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
