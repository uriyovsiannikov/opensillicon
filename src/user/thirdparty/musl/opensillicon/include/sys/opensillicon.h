#ifndef _SYS_OPENSILLICON_H
#define _SYS_OPENSILLICON_H

#include <abi/framebuffer.h>
#include <abi/input.h>
#include <abi/power.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native extensions return zero on success, or -1 with errno on failure.
 * Module loading returns no ID; unloading takes the registered module name. */
int os_module_load(const char *path);
int os_module_unload(const char *name);
int os_power_control(unsigned int command);
int os_framebuffer_info(struct os_framebuffer_info *info);
int os_framebuffer_present_rect(const struct os_framebuffer_rect *rect);
/* Input polls return 1 for one event, 0 if empty, or -1 with errno. */
int os_keyboard_poll(struct os_keyboard_event *event);
int os_mouse_poll(struct os_mouse_event *event);

#ifdef __cplusplus
}
#endif

#endif
