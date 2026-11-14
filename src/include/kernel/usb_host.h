#ifndef KERNEL_USB_HOST_H
#define KERNEL_USB_HOST_H

#include <stdbool.h>

struct os_mouse_event;
struct pci_device_info;
struct terminal_key_event;

/* BSP boot hooks. PCI discovery does not claim or touch the controller. */
void usb_host_record_controller(const struct pci_device_info *device);
bool usb_host_init(void);
/* Run USBX tasks at BSP kernel safe points outside the timer ISR. */
void usb_host_poll(void);
bool usb_host_available(void);
void usb_host_input_diagnostics(void);
bool usb_host_keyboard_poll(struct terminal_key_event *event);
bool usb_host_mouse_poll(struct os_mouse_event *event);

#endif
