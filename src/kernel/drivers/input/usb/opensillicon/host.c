#include "xhci.h"
#include "abi/input.h"
#include "arch/x86_64/pci.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/smp.h"
#include "kernel/terminal.h"
#include "kernel/timer.h"
#include "kernel/tty.h"
#include "kernel/usb_host.h"

#include "ux_api.h"
#include "ux_system.h"
#include "ux_utility.h"
#include "ux_host_class_hid.h"
#include "ux_host_class_hid_keyboard.h"
#include "ux_host_class_hid_mouse.h"
#include "ux_host_class_hub.h"
#include "ux_host_class_storage.h"

#define USBX_POOL_SIZE (128U * 1024U)
#define USB_PCI_CLASS_SERIAL_BUS 0x0cU
#define USB_PCI_SUBCLASS_USB 0x03U
#define X86_FLAGS_INTERRUPT_ENABLE (1UL << 9)

static unsigned int controller_count;
static struct pci_device_info xhci_devices[UX_MAX_HCD];
static size_t xhci_count;
static bool initialized;
static bool active;
static UINT last_error_context, last_error_code;

/* USB HID Keyboard/Keypad usages indexed by usage ID. Bit 7 encodes E0. */
static const uint8_t keyboard_scancodes[0xe8] = {
	[0x04] = 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
	0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f,
	0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
	[0x1e] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	[0x28] = 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b,
	0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3a,
	[0x3a] = 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
	[0x46] = 0xb7, 0x46, 0xff,
	[0x49] = 0xd2, 0xc7, 0xc9, 0xd3, 0xcf, 0xd1, 0xcd, 0xcb, 0xd0, 0xc8,
	[0x53] = 0x45, 0xb5, 0x37, 0x4a, 0x4e, 0x9c, 0x4f, 0x50, 0x51, 0x4b,
	0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53, 0x56, 0xdd,
	[0xe0] = 0x1d, 0x2a, 0x38, 0xdb, 0x9d, 0x36, 0xb8, 0xdc,
};

#define USB_KEY_QUEUE_SIZE 256U
#define USB_KEY_STATE_SIZE ((sizeof(keyboard_scancodes) + 7U) / 8U)

static struct usb_keyboard {
	UX_HOST_CLASS_HID_KEYBOARD *instance;
	uint8_t down[USB_KEY_STATE_SIZE];
} keyboards[UX_MAX_DEVICES];
static struct terminal_key_event key_queue[USB_KEY_QUEUE_SIZE];
static unsigned int key_head, key_count;
static bool repeat_active;
static unsigned int repeat_usage;
static uint64_t repeat_deadline;
static struct terminal_key_event repeat_event;

/* USB sends changes in held keys; unlike PS/2 it has no hardware typematic.
 * Only the text console consumes this repeat stream. X owns its own repeat. */
static bool repeatable_usage(unsigned int usage)
{
	return (usage >= 0x04 && usage <= 0x38) ||
	    (usage >= 0x49 && usage <= 0x52) ||
	    (usage >= 0x54 && usage <= 0x63);
}

static void
keyboard_event(struct usb_keyboard *keyboard, unsigned int usage, bool pressed)
{
	uint8_t code = keyboard_scancodes[usage];
	uint8_t mask = 1U << (usage % 8);
	unsigned int byte = usage / 8;
	if (code == 0 || ((keyboard->down[byte] & mask) != 0) == pressed)
		return;
	keyboard->down[byte] ^= mask;
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++)
		if (&keyboards[i] != keyboard && (keyboards[i].down[byte] & mask) != 0)
			return;

	if (pressed && repeatable_usage(usage)) {
		repeat_active = true;
		repeat_usage = usage;
		repeat_event = (struct terminal_key_event){
		    code & 0x7fU, (code & 0x80U) != 0, true};
		repeat_deadline = timer_monotonic_ns() + UINT64_C(500000000);
	} else if (!pressed && repeat_active && repeat_usage == usage) {
		repeat_active = false;
	}

	if (key_count == USB_KEY_QUEUE_SIZE) {
		key_head = (key_head + 1) % USB_KEY_QUEUE_SIZE;
		key_count--;
	}
	key_queue[(key_head + key_count) % USB_KEY_QUEUE_SIZE] =
	    (struct terminal_key_event){code & 0x7fU, (code & 0x80U) != 0, pressed};
	key_count++;
}

static void
keyboard_change(ULONG event, UX_HOST_CLASS_HID_CLIENT *client)
{
	if (client->ux_host_class_hid_client_handler != ux_host_class_hid_keyboard_entry)
		return;

	UX_HOST_CLASS_HID_KEYBOARD *instance = client->ux_host_class_hid_client_local_instance;
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++) {
		struct usb_keyboard *keyboard = &keyboards[i];
		if (event == UX_HID_CLIENT_INSERTION && keyboard->instance == NULL) {
			ux_host_class_hid_keyboard_ioctl(instance,
			    UX_HID_KEYBOARD_IOCTL_DISABLE_KEYS_DECODE, NULL);
			keyboard->instance = instance;
			klog("OpenSillicon: USBX keyboard ready\n");
			return;
		}
		if (event == UX_HID_CLIENT_REMOVAL && keyboard->instance == instance) {
			for (unsigned int usage = 0; usage < sizeof(keyboard_scancodes); usage++)
				keyboard_event(keyboard, usage, false);
			keyboard->instance = NULL;
			return;
		}
	}
}

#define USB_MOUSE_QUEUE_SIZE 64U

static struct usb_mouse {
	UX_HOST_CLASS_HID_MOUSE *instance;
	SLONG x, y;
	uint8_t buttons;
} mice[UX_MAX_DEVICES];
static struct os_mouse_event mouse_queue[USB_MOUSE_QUEUE_SIZE];
static unsigned int mouse_head, mouse_count;
static uint8_t mouse_buttons;

static void
mouse_event(int16_t dx, int16_t dy)
{
	uint8_t buttons = 0;
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++)
		buttons |= mice[i].buttons;
	if (dx == 0 && dy == 0 && buttons == mouse_buttons)
		return;
	mouse_buttons = buttons;
	if (mouse_count != 0) {
		struct os_mouse_event *last = &mouse_queue[
		    (mouse_head + mouse_count - 1) % USB_MOUSE_QUEUE_SIZE];
		int32_t total_x = (int32_t)last->dx + dx;
		int32_t total_y = (int32_t)last->dy + dy;
		if (last->buttons == buttons && total_x >= INT16_MIN && total_x <= INT16_MAX &&
		    total_y >= INT16_MIN && total_y <= INT16_MAX) {
			last->dx = (int16_t)total_x;
			last->dy = (int16_t)total_y;
			return;
		}
	}
	if (mouse_count == USB_MOUSE_QUEUE_SIZE) {
		mouse_head = (mouse_head + 1) % USB_MOUSE_QUEUE_SIZE;
		mouse_count--;
	}
	mouse_queue[(mouse_head + mouse_count) % USB_MOUSE_QUEUE_SIZE] =
	    (struct os_mouse_event){ .dx = dx, .dy = dy, .buttons = buttons };
	mouse_count++;
}

static void
mouse_change(ULONG event, UX_HOST_CLASS_HID_CLIENT *client)
{
	if (client->ux_host_class_hid_client_handler != ux_host_class_hid_mouse_entry)
		return;

	UX_HOST_CLASS_HID_MOUSE *instance = client->ux_host_class_hid_client_local_instance;
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++) {
		struct usb_mouse *mouse = &mice[i];
		if (event == UX_HID_CLIENT_INSERTION && mouse->instance == NULL) {
			*mouse = (struct usb_mouse){ .instance = instance };
			klog("OpenSillicon: USBX mouse ready\n");
			return;
		}
		if (event == UX_HID_CLIENT_REMOVAL && mouse->instance == instance) {
			*mouse = (struct usb_mouse){0};
			mouse_event(0, 0);
			return;
		}
	}
}

static void
mouse_update(void)
{
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++) {
		struct usb_mouse *mouse = &mice[i];
		if (mouse->instance == NULL)
			continue;
		SLONG x, y;
		ULONG buttons;
		if (ux_host_class_hid_mouse_position_get(mouse->instance, &x, &y) != UX_SUCCESS ||
		    ux_host_class_hid_mouse_buttons_get(mouse->instance, &buttons) != UX_SUCCESS)
			continue;
		int32_t dx = (int32_t)((uint32_t)x - (uint32_t)mouse->x);
		int32_t dy = (int32_t)((uint32_t)y - (uint32_t)mouse->y);
		mouse->x = x;
		mouse->y = y;
		mouse->buttons = buttons & 7U;
		mouse_event(dx < INT16_MIN ? INT16_MIN : dx > INT16_MAX ? INT16_MAX : dx,
		    dy < INT16_MIN ? INT16_MIN : dy > INT16_MAX ? INT16_MAX : dy);
	}
}

static UINT
device_change(ULONG event, UX_HOST_CLASS *class, VOID *instance)
{
	(void)class;
	if (event == UX_HID_CLIENT_INSERTION || event == UX_HID_CLIENT_REMOVAL) {
		keyboard_change(event, instance);
		mouse_change(event, instance);
	} else if (event == UX_DEVICE_DISCONNECTION) {
		klog("OpenSillicon: USBX device disconnected\n");
	} else if (event == UX_DEVICE_CONNECTION) {
		UX_DEVICE *device = instance;
		if (device->ux_device_descriptor.bNumConfigurations != 0 &&
		    device->ux_device_first_configuration != UX_NULL) {
			klog("OpenSillicon: USBX enumerated VID ");
			klog_u64(device->ux_device_descriptor.idVendor);
			klog(" PID ");
			klog_u64(device->ux_device_descriptor.idProduct);
			klog(" root port ");
			klog_u64(UX_DEVICE_PORT_LOCATION_GET(device) + 1U);
			klog("\n");
		}
	}
	return UX_SUCCESS;
}

void
usb_host_poll(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	if ((cpu != NULL && !cpu->bsp) || !active)
		return;

	ux_system_tasks_run();
	mouse_update();
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++) {
		struct usb_keyboard *keyboard = &keyboards[i];
		ULONG usage, state;
		while (keyboard->instance != NULL &&
		    ux_host_class_hid_keyboard_key_get(keyboard->instance, &usage, &state) == UX_SUCCESS) {
			if (usage < sizeof(keyboard_scancodes))
				keyboard_event(keyboard, usage, (state & UX_HID_KEYBOARD_STATE_KEY_UP) == 0);
		}
	}
}

static void
report_text(const char *text, size_t length)
{
	tty_write(text, length);
	serial_write(text, length);
}

static void
report_value(const char *label, uint64_t value, bool end_line)
{
	char buffer[20];
	size_t length = sizeof(buffer);
	do {
		buffer[--length] = '0' + value % 10;
		value /= 10;
	} while (value != 0);
	size_t label_length = 0;
	while (label[label_length] != '\0')
		label_length++;
	report_text(label, label_length);
	report_text(" = ", 3);
	report_text(buffer + length, sizeof(buffer) - length);
	if (end_line)
		report_text("\n", 1);
}

static VOID
record_error(UINT level, UINT context, UINT code)
{
	(void)level;
	last_error_context = context;
	last_error_code = code;
}

void
usb_host_input_diagnostics(void)
{
	unsigned int keyboard_count = 0;
	for (unsigned int i = 0; i < UX_MAX_DEVICES; i++)
		keyboard_count += keyboards[i].instance != NULL;
	static const char message[] = "\nUSB input diagnostics (decimal values)\n";
	report_text(message, sizeof(message) - 1);
	xhci_input_diagnostics(report_value);
	UX_HOST_CLASS *hid_class = UX_NULL;
	if (_ux_system_host != UX_NULL &&
	    ux_host_stack_class_get(_ux_system_host_class_hid_name, &hid_class) == UX_SUCCESS) {
		UX_HOST_CLASS_HID *hid = hid_class->ux_host_class_first_instance;
		while (hid != NULL) {
			report_value("HID interface", hid->ux_host_class_hid_interface->
			    ux_interface_descriptor.bInterfaceNumber, false);
			report_value(" state", hid->ux_host_class_hid_enum_state, false);
			report_value(" status", hid->ux_host_class_hid_status, true);
			hid = hid->ux_host_class_hid_next_instance;
		}
	}
	report_value("active USB host", active, false);
	report_value(" ready USB keyboards", keyboard_count, true);
	report_value("USBX last error context", last_error_context, false);
	report_value(" code", last_error_code, true);
}

bool
usb_host_available(void)
{
	/* Input devices may be plugged in after the host becomes active. */
	return active;
}

size_t
usb_host_controller_count(void)
{
	return controller_count;
}

size_t
usb_host_device_snapshot(struct usb_host_device_info *entries, size_t capacity)
{
	size_t count = 0;
	static const char keyboard_name[] = "HID keyboard";
	static const char mouse_name[] = "HID mouse";
	if (entries == NULL)
		return 0;
	for (unsigned int i = 0; i < UX_MAX_DEVICES && count < capacity; i++) {
		if (keyboards[i].instance != NULL) {
			for (size_t j = 0; j < sizeof(keyboard_name); j++)
				entries[count].type[j] = keyboard_name[j];
			entries[count++].index = i;
		}
		if (mice[i].instance != NULL && count < capacity) {
			for (size_t j = 0; j < sizeof(mouse_name); j++)
				entries[count].type[j] = mouse_name[j];
			entries[count++].index = i;
		}
	}
	return count;
}

bool
usb_host_keyboard_poll(struct terminal_key_event *event)
{
	usb_host_poll();
	if (key_count == 0)
		return false;
	*event = key_queue[key_head];
	key_head = (key_head + 1) % USB_KEY_QUEUE_SIZE;
	key_count--;
	return true;
}

bool
usb_host_keyboard_repeat_poll(struct terminal_key_event *event)
{
	uint64_t now = timer_monotonic_ns();
	if (!repeat_active || now < repeat_deadline)
		return false;
	*event = repeat_event;
	/* Do not replay a backlog after a long-running command or stalled reader. */
	repeat_deadline = now + UINT64_C(33000000);
	return true;
}

bool
usb_host_mouse_poll(struct os_mouse_event *event)
{
	usb_host_poll();
	if (mouse_count == 0)
		return false;
	*event = mouse_queue[mouse_head];
	mouse_head = (mouse_head + 1) % USB_MOUSE_QUEUE_SIZE;
	mouse_count--;
	return true;
}

void
usb_host_record_controller(const struct pci_device_info *device)
{
	if (device->class_code != USB_PCI_CLASS_SERIAL_BUS ||
	    device->subclass != USB_PCI_SUBCLASS_USB)
		return;

	const char *kind;
	switch (device->programming_interface) {
	case 0x00: kind = "UHCI"; break;
	case 0x10: kind = "OHCI"; break;
	case 0x20: kind = "EHCI"; break;
	case 0x30:
		kind = "xHCI";
		if (xhci_count < UX_MAX_HCD)
			xhci_devices[xhci_count++] = *device;
		break;
	default: return;
	}

	controller_count++;
	klog("OpenSillicon: USB PCI host controller detected: ");
	klog(kind);
	klog("\n");
}

ALIGN_TYPE
_ux_utility_interrupt_disable(void)
{
	ALIGN_TYPE flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
}

VOID
_ux_utility_interrupt_restore(ALIGN_TYPE flags)
{
	if ((flags & X86_FLAGS_INTERRUPT_ENABLE) != 0)
		__asm__ volatile ("sti" : : : "memory");
}

ULONG
_ux_utility_time_get(void)
{
	return (ULONG)(timer_monotonic_ns() / (1000000000U / UX_PERIODIC_RATE));
}

bool
usb_host_init(void)
{
	if (initialized)
		return true;
	if (controller_count == 0) {
		klog("OpenSillicon: no USB PCI host controller found\n");
		return true;
	}

	/* A BSP-owned pool for the host core. An HCD must provide DMA storage
	 * separately, using the kernel DMA API for its hardware buffers. */
	void *pool = kheap_alloc(USBX_POOL_SIZE);
	if (pool == NULL)
		return false;

	UINT status = ux_system_initialize(pool, USBX_POOL_SIZE, UX_NULL, 0);
	if (status == UX_SUCCESS) {
		_ux_system->ux_system_error_callback_function = record_error;
		status = ux_host_stack_initialize(device_change);
	}
	if (status == UX_SUCCESS)
		status = ux_host_stack_class_register(_ux_system_host_class_hub_name,
		    ux_host_class_hub_entry);
	if (status == UX_SUCCESS)
		status = ux_host_stack_class_register(_ux_system_host_class_hid_name,
		    ux_host_class_hid_entry);
	if (status == UX_SUCCESS)
		status = ux_host_class_hid_client_register(
		    _ux_system_host_class_hid_client_keyboard_name, ux_host_class_hid_keyboard_entry);
	if (status == UX_SUCCESS)
		status = ux_host_class_hid_client_register(
		    _ux_system_host_class_hid_client_mouse_name, ux_host_class_hid_mouse_entry);
	if (status == UX_SUCCESS)
		status = ux_host_stack_class_register(_ux_system_host_class_storage_name,
		    ux_host_class_storage_entry);

	if (status != UX_SUCCESS) {
		klog("OpenSillicon: USBX initialization status ");
		klog_u64(status);
		klog("\n");
		/* Registration only allocates from this pool. No HCD, worker,
		 * callback or caller can retain a live reference on failure. */
		_ux_system = UX_NULL;
		_ux_system_host = UX_NULL;
		(void)kheap_free(pool);
		return false;
	}

	initialized = true;
	klog("OpenSillicon: USBX host core ready (Hub, HID, Storage)\n");
	for (size_t i = 0; i < xhci_count; i++)
		if (xhci_attach(&xhci_devices[i], (unsigned int)i))
			active = true;
	if (!active)
		klog("OpenSillicon: USB has no active host controller\n");
	return true;
}
