#include "xhci.h"

#include "arch/x86_64/pci_resource.h"
#include "kernel/dma.h"
#include "kernel/log.h"
#include "kernel/timer.h"

#include "ux_api.h"
#include "ux_system.h"

#define PCI_COMMAND 0x04U
#define PCI_VENDOR_INTEL 0x8086U
#define PCI_MEMORY_ENABLE (1U << 1)
#define PCI_BUS_MASTER (1U << 2)

#define XHCI_HCSPARAMS1 0x04U
#define XHCI_HCSPARAMS2 0x08U
#define XHCI_HCCPARAMS1 0x10U
#define XHCI_DBOFF 0x14U
#define XHCI_RTSOFF 0x18U
#define XHCI_USBCMD 0x00U
#define XHCI_USBSTS 0x04U
#define XHCI_PAGESIZE 0x08U
#define XHCI_CRCR 0x18U
#define XHCI_DCBAAP 0x30U
#define XHCI_CONFIG 0x38U
#define XHCI_PORTSC 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_PORT_CONNECTED (1U << 0)
#define XHCI_PORT_ENABLED (1U << 1)
#define XHCI_PORT_RESET (1U << 4)
#define XHCI_PORT_CONNECT_CHANGE (1U << 17)
#define XHCI_PORT_CHANGE_BITS (0x7fU << 17)
#define XHCI_PORT_POWER (1U << 9)
#define XHCI_PORT_PRESERVE (XHCI_PORT_POWER | (3U << 14) | (7U << 25))
#define XHCI_IMAN 0x20U
#define XHCI_ERSTSZ 0x28U
#define XHCI_ERSTBA 0x30U
#define XHCI_ERDP 0x38U

#define XHCI_RUN 1U
#define XHCI_RESET (1U << 1)
#define XHCI_HALTED 1U
#define XHCI_FATAL_STATUS ((1U << 2) | (1U << 12))
#define XHCI_NOT_READY (1U << 11)
#define XHCI_LEGACY_BIOS (1U << 16)
#define XHCI_LEGACY_OS (1U << 24)
#define XHCI_LEGACY_PRESERVE ((7U << 1) | (0xffU << 5) | (7U << 17))
#define XHCI_LEGACY_CLEAR_STATUS (7U << 29)

#define XHCI_PAGE 4096U
#define XHCI_RING_COUNT 64U
#define XHCI_TRB_CYCLE 1U
#define XHCI_TRB_LINK (6U << 10)
#define XHCI_LINK_TOGGLE (1U << 1)
#define XHCI_ENABLE_SLOT (9U << 10)
#define XHCI_DISABLE_SLOT (10U << 10)
#define XHCI_CONFIGURE_ENDPOINT (12U << 10)
#define XHCI_RESET_ENDPOINT (14U << 10)
#define XHCI_STOP_ENDPOINT (15U << 10)
#define XHCI_SET_TR_DEQUEUE (16U << 10)
#define XHCI_ADDRESS_DEVICE (11U << 10)
#define XHCI_EVALUATE_CONTEXT (13U << 10)
#define XHCI_NOOP (23U << 10)
#define XHCI_NORMAL (1U << 10)
#define XHCI_SETUP (2U << 10)
#define XHCI_DATA (3U << 10)
#define XHCI_STATUS (4U << 10)
#define XHCI_TRB_ISP (1U << 2)
#define XHCI_TRB_IOC (1U << 5)
#define XHCI_TRB_IMMEDIATE (1U << 6)
#define XHCI_TRB_IN (1U << 16)
#define XHCI_SETUP_DATA_OUT (2U << 16)
#define XHCI_SETUP_DATA_IN (3U << 16)
#define XHCI_EVENT_TRANSFER 32U
#define XHCI_EVENT_COMMAND 33U
#define XHCI_COMPLETION_SUCCESS 1U
#define XHCI_COMPLETION_STALL 6U
#define XHCI_COMPLETION_SHORT 13U
#define XHCI_EHB (1U << 3)
#define USB_GET_DESCRIPTOR 6U
#define USB_DEVICE_DESCRIPTOR 1U
#define USB_CONFIGURATION_DESCRIPTOR 2U
#define USB_DEVICE_LENGTH 18U
#define USB_CONFIGURATION_LENGTH 9U
#define XHCI_TIMEOUT_NS UINT64_C(1000000000)

struct xhci_trb {
	uint64_t parameter;
	uint32_t status;
	uint32_t control;
};

struct xhci_erst_entry {
	uint64_t address;
	uint32_t count;
	uint32_t reserved;
};

/* All controller structures are in a single coherent, page-aligned buffer. */
struct xhci_dma_layout {
	_Alignas(64) uint64_t dcbaa[256];
	_Alignas(64) struct xhci_trb commands[XHCI_RING_COUNT];
	_Alignas(64) struct xhci_trb events[XHCI_RING_COUNT];
	_Alignas(64) struct xhci_erst_entry erst;
};

_Static_assert(sizeof(struct xhci_trb) == 16, "xHCI TRB size");
_Static_assert(sizeof(struct xhci_erst_entry) == 16, "xHCI ERST entry size");
_Static_assert(sizeof(struct xhci_dma_layout) <= XHCI_PAGE + XHCI_PAGE,
    "xHCI DMA layout fits in two pages");
_Static_assert(offsetof(struct xhci_dma_layout, events) +
    sizeof(((struct xhci_dma_layout *)0)->events) <= XHCI_PAGE,
    "xHCI event segment fits within one page");

/* Each active root port has a separate DMA workspace. */
struct xhci_device_dma {
	_Alignas(XHCI_PAGE) uint32_t input[33 * 16];
	_Alignas(XHCI_PAGE) uint32_t output[32 * 16];
	_Alignas(XHCI_PAGE) struct xhci_trb control_ring[XHCI_RING_COUNT];
	_Alignas(XHCI_PAGE) uint8_t data[XHCI_PAGE];
};

struct xhci_endpoint_dma {
	_Alignas(XHCI_PAGE) struct xhci_trb ring[XHCI_RING_COUNT];
	_Alignas(XHCI_PAGE) uint8_t data[XHCI_PAGE];
};

struct xhci_endpoint {
	struct dma_buffer allocation;
	struct xhci_endpoint_dma *memory;
	uint64_t address;
	unsigned int id;
	unsigned int index;
	unsigned int cycle;
	uint64_t trb;
	UX_TRANSFER *request;
	struct xhci_trb event;
	bool done;
	bool stopped;
	uint64_t reports;
};

struct xhci_port {
	unsigned int slot;
	/* DCI 2 through 31 cover all non-control endpoint addresses. */
	struct xhci_endpoint endpoints[30];
	unsigned int packet_size;
	unsigned int control_index;
	unsigned int control_cycle;
	bool connected;
	bool reset_pending;
	bool reset_recovering;
	uint64_t reset_started;
	unsigned int last_command;
	unsigned int last_completion;
	unsigned int last_control_request;
	unsigned int last_control_value;
	unsigned int last_control_completion;
	unsigned int last_endpoint_address, last_endpoint_status;
};

struct xhci {
	volatile uint8_t *mmio;
	size_t length;
	size_t op;
	size_t runtime;
	size_t doorbells;
	struct xhci_dma_layout *memory;
	uint64_t dma_address;
	struct xhci_device_dma *devices;
	uint64_t devices_address;
	struct xhci_port ports[UX_MAX_ROOTHUB_PORT];
	unsigned int port_count;
	unsigned int slot_count;
	unsigned int context_words;
	unsigned int command_index;
	unsigned int event_index;
	unsigned int command_cycle;
	unsigned int event_cycle;
};

/* BSP ownership extends from PCI attachment through host task execution. */
static struct xhci controllers[UX_MAX_HCD];

static bool
range_fits(const struct xhci *xhci, size_t offset, size_t length)
{
	return offset <= xhci->length && length <= xhci->length - offset;
}

static uint32_t
read32(const struct xhci *xhci, size_t offset)
{
	return *(volatile uint32_t *)(xhci->mmio + offset);
}

static void
write32(const struct xhci *xhci, size_t offset, uint32_t value)
{
	*(volatile uint32_t *)(xhci->mmio + offset) = value;
}

static void
write64(const struct xhci *xhci, size_t offset, uint64_t value)
{
	write32(xhci, offset, (uint32_t)value);
	write32(xhci, offset + 4, (uint32_t)(value >> 32));
}

static bool
wait_register(const struct xhci *xhci, size_t offset, uint32_t mask,
    uint32_t expected)
{
	uint64_t start = timer_monotonic_ns();
	do {
		uint32_t value = read32(xhci, offset);
		if (value == UINT32_MAX)
			return false;
		if ((value & mask) == expected)
			return true;
		timer_wait_for_interrupt();
	} while (timer_monotonic_ns() - start < XHCI_TIMEOUT_NS);
	return false;
}

static bool
take_ownership(const struct xhci *xhci, uint32_t hcc)
{
	size_t offset = (size_t)(hcc >> 16) * 4;
	/* Next pointers are relative forward offsets in dwords. */
	while (offset != 0 && range_fits(xhci, offset, 4)) {
		uint32_t cap = read32(xhci, offset);
		if (cap == UINT32_MAX) {
			klog("OpenSillicon: xHCI extended capability is unreadable\n");
			return false;
		}
		if ((cap & 0xffU) == 1U) {
			if (!range_fits(xhci, offset, 8))
				return false;
			write32(xhci, offset, cap | XHCI_LEGACY_OS);
			if (!wait_register(xhci, offset, XHCI_LEGACY_BIOS, 0)) {
				cap = read32(xhci, offset);
				if (cap == UINT32_MAX) {
					klog("OpenSillicon: xHCI legacy capability became unreadable\n");
					return false;
				}
				/* Some firmware leaves BIOS-owned set after ExitBootServices.
				 * Reclaim it after the bounded cooperative handoff. */
				klog("OpenSillicon: xHCI BIOS ownership timeout; reclaiming controller\n");
				write32(xhci, offset, (cap | XHCI_LEGACY_OS) & ~XHCI_LEGACY_BIOS);
			}
			uint32_t legacy = read32(xhci, offset + 4);
			if (legacy == UINT32_MAX)
				return false;
			write32(xhci, offset + 4,
			    (legacy & XHCI_LEGACY_PRESERVE) | XHCI_LEGACY_CLEAR_STATUS);
			if (!wait_register(xhci, offset, XHCI_LEGACY_BIOS | XHCI_LEGACY_OS,
			    XHCI_LEGACY_OS)) {
				klog("OpenSillicon: xHCI ownership semaphore did not accept OS ownership\n");
				return false;
			}
			return true;
		}
		unsigned int next = (cap >> 8) & 0xffU;
		if (next == 0)
			return true;
		offset += (size_t)next * 4;
	}
	if (offset != 0)
		klog("OpenSillicon: xHCI extended capability points outside BAR0\n");
	return offset == 0;
}

static bool
reset_controller(const struct xhci *xhci, bool intel)
{
	if (!wait_register(xhci, xhci->op + XHCI_USBSTS, XHCI_NOT_READY, 0))
		return false;
	write32(xhci, xhci->op + XHCI_USBCMD, 0);
	if (!wait_register(xhci, xhci->op + XHCI_USBSTS, XHCI_HALTED, XHCI_HALTED))
		return false;
	write32(xhci, xhci->op + XHCI_USBCMD, XHCI_RESET);
	if (intel) {
		/* Intel xHCI may hang if MMIO is accessed within 1 ms of HCRST. */
		uint64_t start = timer_monotonic_ns();
		while (timer_monotonic_ns() - start < UINT64_C(1000000))
			__asm__ volatile ("pause");
	}
	return wait_register(xhci, xhci->op + XHCI_USBCMD, XHCI_RESET, 0) &&
	    wait_register(xhci, xhci->op + XHCI_USBSTS, XHCI_NOT_READY, 0);
}

static bool
pci_command(const struct pci_device_info *device, uint16_t command)
{
	uint32_t verified;
	return pci_config_write(device->segment, device->bus, device->device,
	    device->function, PCI_COMMAND, 16, command) &&
	    pci_config_read(device->segment, device->bus, device->device,
	    device->function, PCI_COMMAND, 16, &verified) &&
	    verified == command;
}

static bool
take_event(struct xhci *xhci, struct xhci_trb *result)
{
	volatile struct xhci_trb *event = &xhci->memory->events[xhci->event_index];
	uint32_t control = event->control;
	if ((control & XHCI_TRB_CYCLE) != xhci->event_cycle)
		return false;

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	*result = (struct xhci_trb){event->parameter, event->status, control};
	if (++xhci->event_index == XHCI_RING_COUNT) {
		xhci->event_index = 0;
		xhci->event_cycle ^= 1U;
	}
	uint64_t base = xhci->dma_address + offsetof(struct xhci_dma_layout, events);
	write64(xhci, xhci->runtime + XHCI_ERDP,
	    (base + xhci->event_index * sizeof(*event)) | XHCI_EHB);
	return true;
}

static void
record_transfer_event(struct xhci *xhci, const struct xhci_trb *event)
{
	if (((event->control >> 10) & 0x3fU) != XHCI_EVENT_TRANSFER)
		return;

	for (unsigned int i = 0; i < xhci->port_count; i++) {
		struct xhci_port *port = &xhci->ports[i];
		unsigned int dci = (event->control >> 16) & 0x1fU;
		if ((event->control >> 24) != port->slot || dci < 2)
			continue;
		struct xhci_endpoint *endpoint = &port->endpoints[dci - 2];
		if (endpoint->request == NULL || endpoint->done || event->parameter != endpoint->trb)
			continue;
		endpoint->event = *event;
		endpoint->done = true;
		return;
	}
}

static bool
wait_event(struct xhci *xhci, unsigned int type, unsigned int slot,
    struct xhci_trb *result)
{
	uint64_t start = timer_monotonic_ns();
	do {
		if (!take_event(xhci, result)) {
			if ((read32(xhci, xhci->op + XHCI_USBSTS) & XHCI_FATAL_STATUS) != 0)
				return false;
			timer_wait_for_interrupt();
			continue;
		}
		record_transfer_event(xhci, result);
		if (((result->control >> 10) & 0x3fU) != type)
			continue;
		if (type != XHCI_EVENT_TRANSFER ||
		    (result->control >> 24 == slot && ((result->control >> 16) & 0x1fU) == 1U))
			return true;
	} while (timer_monotonic_ns() - start < XHCI_TIMEOUT_NS);
	return false;
}

static bool
submit_command(struct xhci *xhci, uint32_t command, uint64_t input,
    unsigned int *slot)
{
	unsigned int index = xhci->command_index;
	uint64_t base = xhci->dma_address + offsetof(struct xhci_dma_layout, commands);
	volatile struct xhci_trb *trb = &xhci->memory->commands[index];
	uint32_t cycle = xhci->command_cycle;

	if (index == XHCI_RING_COUNT - 2) {
		volatile struct xhci_trb *link = &xhci->memory->commands[index + 1];
		link->parameter = base;
		link->status = 0;
		link->control = XHCI_TRB_LINK | XHCI_LINK_TOGGLE | cycle;
		xhci->command_index = 0;
		xhci->command_cycle ^= 1U;
	} else {
		xhci->command_index++;
	}

	trb->parameter = input;
	trb->status = 0;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	trb->control = command | cycle;
	__asm__ volatile ("mfence" : : : "memory");
	write32(xhci, xhci->doorbells, 0);

	struct xhci_trb event;
	bool received = wait_event(xhci, XHCI_EVENT_COMMAND, 0, &event);
	unsigned int slot_id = command >> 24;
	for (unsigned int i = 0; i < xhci->port_count; i++) {
		if (slot_id != 0 && xhci->ports[i].slot == slot_id) {
			xhci->ports[i].last_command = (command >> 10) & 0x3fU;
			xhci->ports[i].last_completion = received ? event.status >> 24 : 0;
		}
	}
	if (!received || event.parameter != base + index * sizeof(*trb) ||
	    (event.status >> 24) != XHCI_COMPLETION_SUCCESS)
		return false;
	if (slot != NULL)
		*slot = event.control >> 24;
	return true;
}

static uint64_t
port_dma(const struct xhci *xhci, unsigned int index)
{
	return xhci->devices_address + index * sizeof(struct xhci_device_dma);
}

static size_t
port_register(const struct xhci *xhci, unsigned int index)
{
	return xhci->op + XHCI_PORTSC + index * XHCI_PORT_STRIDE;
}

static void
release_endpoint(struct xhci_endpoint *endpoint)
{
	if (endpoint->allocation.generation != 0) {
		if (!dma_descriptor_ring_unpin(endpoint->allocation) ||
		    !dma_free(endpoint->allocation)) {
			klog("OpenSillicon panic: could not release xHCI endpoint DMA\n");
			kernel_halt();
		}
	}
	*endpoint = (struct xhci_endpoint){0};
}

static void
stop_interrupt(struct xhci *xhci, struct xhci_port *port, struct xhci_endpoint *endpoint)
{
	if (endpoint->id != 0 && !endpoint->stopped)
		(void)submit_command(xhci, XHCI_STOP_ENDPOINT | (port->slot << 24) |
		    (endpoint->id << 16), 0, NULL);
	endpoint->stopped = true;
	endpoint->request = NULL;
	endpoint->done = false;
}

static void
stop_port_transfers(struct xhci *xhci, struct xhci_port *port)
{
	for (unsigned int i = 0; i < 30; i++)
		stop_interrupt(xhci, port, &port->endpoints[i]);
}

static bool
disable_slot(struct xhci *xhci, struct xhci_port *port)
{
	if (port->slot == 0)
		return true;
	if (!submit_command(xhci, XHCI_DISABLE_SLOT | (port->slot << 24), 0, NULL))
		return false;
	xhci->memory->dcbaa[port->slot] = 0;
	port->slot = 0;
	for (unsigned int i = 0; i < 30; i++)
		release_endpoint(&port->endpoints[i]);
	return true;
}

static bool
address_device(struct xhci *xhci, unsigned int index)
{
	struct xhci_port *port = &xhci->ports[index];
	struct xhci_device_dma *device = &xhci->devices[index];
	uint64_t address = port_dma(xhci, index);
	uint64_t ring = address + offsetof(struct xhci_device_dma, control_ring);
	unsigned int speed = (read32(xhci, port_register(xhci, index)) >> 10) & 0xfU;
	if (speed < 1 || speed > 3 || port->slot != 0)
		return false;

	unsigned int slot;
	if (!submit_command(xhci, XHCI_ENABLE_SLOT, 0, &slot) ||
	    slot == 0 || slot > xhci->slot_count)
		return false;
	port->slot = slot;

	uint8_t *bytes = (uint8_t *)device;
	for (size_t i = 0; i < sizeof(*device); i++)
		bytes[i] = 0;

	unsigned int stride = xhci->context_words;
	uint32_t *endpoint = device->input + 2 * stride;
	port->packet_size = speed == 3 ? 64 : 8;
	port->control_index = 0;
	port->control_cycle = 1;
	device->input[1] = 3; /* Add slot and endpoint zero contexts. */
	device->input[stride] = (1U << 27) | (speed << 20);
	device->input[stride + 1] = (index + 1) << 16;
	endpoint[1] = (port->packet_size << 16) | (4U << 3) | (3U << 1);
	endpoint[2] = (uint32_t)ring | 1U;
	endpoint[3] = (uint32_t)(ring >> 32);
	endpoint[4] = 8;
	xhci->memory->dcbaa[slot] = address + offsetof(struct xhci_device_dma, output);
	return submit_command(xhci, XHCI_ADDRESS_DEVICE | (slot << 24), address, NULL);
}

static bool
update_packet_size(struct xhci *xhci, unsigned int index, unsigned int packet)
{
	struct xhci_port *port = &xhci->ports[index];
	if (packet == port->packet_size)
		return true;
	if (packet != 8 && packet != 16 && packet != 32 && packet != 64)
		return false;

	struct xhci_device_dma *device = &xhci->devices[index];
	/* Evaluate only EP0, using its current hardware context. */
	for (unsigned int i = 0; i < 33 * xhci->context_words; i++)
		device->input[i] = 0;
	device->input[1] = 2;
	uint32_t *endpoint = device->input + 2 * xhci->context_words;
	for (unsigned int i = 0; i < xhci->context_words; i++)
		endpoint[i] = device->output[xhci->context_words + i];
	endpoint[1] = (endpoint[1] & 0xffffU) | (packet << 16);
	if (!submit_command(xhci, XHCI_EVALUATE_CONTEXT | (port->slot << 24),
	    port_dma(xhci, index), NULL))
		return false;
	port->packet_size = packet;
	return true;
}

static unsigned int
descriptor_word(const uint8_t *data)
{
	return data[0] | ((unsigned int)data[1] << 8);
}

/* USBX uses lengths from the configuration header to walk the descriptors. */
static bool
valid_configuration(const uint8_t *data, unsigned int length)
{
	if (length < USB_CONFIGURATION_LENGTH || data[0] != USB_CONFIGURATION_LENGTH ||
	    data[1] != USB_CONFIGURATION_DESCRIPTOR ||
	    descriptor_word(data + 2) < USB_CONFIGURATION_LENGTH)
		return false;
	if (length == USB_CONFIGURATION_LENGTH)
		return true;
	if (descriptor_word(data + 2) != length)
		return false;
	for (unsigned int offset = USB_CONFIGURATION_LENGTH; offset < length;) {
		if (length - offset < 3 || data[offset] < 3 || data[offset] > length - offset)
			return false;
		unsigned int minimum = 3;
		switch (data[offset + 1]) {
		case 4: minimum = 9; break; /* Interface */
		case 5: minimum = 7; break; /* Endpoint */
		case 11: minimum = 8; break; /* Interface association */
		case 0x21: minimum = 6; break; /* HID or other class descriptor */
		}
		if (data[offset] < minimum)
			return false;
		offset += data[offset];
	}
	return true;
}

static UINT
control_request(struct xhci *xhci, unsigned int index, UX_TRANSFER *request,
    unsigned int *actual)
{
	struct xhci_port *port = &xhci->ports[index];
	struct xhci_device_dma *device = &xhci->devices[index];
	port->last_control_request = request->ux_transfer_request_function;
	port->last_control_value = request->ux_transfer_request_value;
	port->last_control_completion = 0;
	unsigned int length = request->ux_transfer_request_requested_length;
	bool in = (request->ux_transfer_request_type & UX_REQUEST_IN) != 0;
	uint8_t *data = request->ux_transfer_request_data_pointer;
	if (port->slot == 0 || length > sizeof(device->data) || (length != 0 && data == NULL))
		return UX_TRANSFER_ERROR;

	uint64_t address = port_dma(xhci, index);
	uint64_t ring = address + offsetof(struct xhci_device_dma, control_ring);
	if (!update_packet_size(xhci, index, request->ux_transfer_request_endpoint->
	    ux_endpoint_descriptor.wMaxPacketSize))
		return UX_TRANSFER_ERROR;
	if (!in)
		for (unsigned int i = 0; i < length; i++)
			device->data[i] = data[i];

	unsigned int count = length == 0 ? 2 : 3;
	struct xhci_trb td[3] = {
		{
			(uint64_t)(request->ux_transfer_request_type & 0xffU) |
			((uint64_t)(request->ux_transfer_request_function & 0xffU) << 8) |
			((uint64_t)(request->ux_transfer_request_value & 0xffffU) << 16) |
			((uint64_t)(request->ux_transfer_request_index & 0xffffU) << 32) |
			((uint64_t)length << 48),
			8, XHCI_SETUP | XHCI_TRB_IMMEDIATE
		}
	};
	if (length != 0) {
		td[0].control |= in ? XHCI_SETUP_DATA_IN : XHCI_SETUP_DATA_OUT;
		td[1] = (struct xhci_trb){
			address + offsetof(struct xhci_device_dma, data),
			length, XHCI_DATA | (in ? XHCI_TRB_IN | XHCI_TRB_ISP : 0)
		};
	}
	td[count - 1].control = XHCI_STATUS | XHCI_TRB_IOC |
	    (length == 0 || !in ? XHCI_TRB_IN : 0);

	unsigned int positions[3];
	unsigned int first_cycle = port->control_cycle;
	for (unsigned int i = 0; i < count; i++) {
		unsigned int pos = port->control_index;
		unsigned int cycle = port->control_cycle;
		positions[i] = pos;
		td[i].control |= cycle;
		if (pos == XHCI_RING_COUNT - 2) {
			device->control_ring[pos + 1] = (struct xhci_trb){
				ring, 0, XHCI_TRB_LINK | XHCI_LINK_TOGGLE | cycle
			};
			port->control_index = 0;
			port->control_cycle ^= 1U;
		} else {
			port->control_index++;
		}
		if (i != 0)
			device->control_ring[pos] = td[i];
	}
	volatile struct xhci_trb *setup = &device->control_ring[positions[0]];
	setup->parameter = td[0].parameter;
	setup->status = td[0].status;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	setup->control = td[0].control | first_cycle;
	__asm__ volatile ("mfence" : : : "memory");
	write32(xhci, xhci->doorbells + port->slot * 4, 1);

	*actual = length;
	struct xhci_trb event;
	if (!wait_event(xhci, XHCI_EVENT_TRANSFER, port->slot, &event))
		return UX_TRANSFER_ERROR;
	port->last_control_completion = event.status >> 24;
	if (length != 0 && in && event.parameter == ring + positions[1] * sizeof(*setup) &&
	    (event.status >> 24) == XHCI_COMPLETION_SHORT) {
		unsigned int remaining = event.status & 0xffffffU;
		if (remaining > length ||
		    !wait_event(xhci, XHCI_EVENT_TRANSFER, port->slot, &event))
			return UX_TRANSFER_ERROR;
		*actual -= remaining;
	}
	if ((event.status >> 24) == XHCI_COMPLETION_STALL) {
		uint64_t dequeue = ring + port->control_index * sizeof(*setup);
		uint32_t endpoint = (port->slot << 24) | (1U << 16);
		if (submit_command(xhci, XHCI_RESET_ENDPOINT | endpoint, 0, NULL) &&
		    submit_command(xhci, XHCI_SET_TR_DEQUEUE | endpoint,
		    dequeue | port->control_cycle, NULL))
			return UX_TRANSFER_STALLED;
		return UX_TRANSFER_ERROR;
	}
	if (event.parameter != ring + positions[count - 1] * sizeof(*setup) ||
	    event.status != (XHCI_COMPLETION_SUCCESS << 24))
		return UX_TRANSFER_ERROR;
	if (in && request->ux_transfer_request_type == 0x80U &&
	    request->ux_transfer_request_function == USB_GET_DESCRIPTOR) {
		unsigned int type = request->ux_transfer_request_value >> 8;
		if (type == USB_DEVICE_DESCRIPTOR &&
		    (*actual < 8 || device->data[0] != USB_DEVICE_LENGTH ||
		    device->data[1] != USB_DEVICE_DESCRIPTOR))
			return UX_TRANSFER_ERROR;
		if (type == USB_CONFIGURATION_DESCRIPTOR &&
		    !valid_configuration(device->data, *actual))
			return UX_TRANSFER_ERROR;
	}
	if (in)
		for (unsigned int i = 0; i < *actual; i++)
			data[i] = device->data[i];
	return UX_SUCCESS;
}

static UINT
port_reset(struct xhci *xhci, unsigned int index)
{
	struct xhci_port *port = &xhci->ports[index];
	size_t offset = port_register(xhci, index);
	uint32_t status = read32(xhci, offset);
	if (status == UINT32_MAX || (status & XHCI_PORT_CONNECTED) == 0) {
		port->reset_pending = false;
		return UX_STATE_EXIT;
	}

	if (!port->reset_pending) {
		if (!disable_slot(xhci, port))
			return UX_STATE_EXIT;
		write32(xhci, offset, (status & XHCI_PORT_PRESERVE) | XHCI_PORT_RESET);
		port->reset_started = timer_monotonic_ns();
		port->reset_pending = true;
		port->reset_recovering = false;
		return UX_STATE_WAIT;
	}
	uint64_t now = timer_monotonic_ns();
	if ((status & XHCI_PORT_RESET) != 0) {
		if (now - port->reset_started < XHCI_TIMEOUT_NS)
			return UX_STATE_WAIT;
		port->reset_pending = false;
		return UX_STATE_ERROR;
	}
	if ((status & XHCI_PORT_ENABLED) == 0) {
		port->reset_pending = false;
		return UX_STATE_ERROR;
	}
	if (!port->reset_recovering) {
		port->reset_recovering = true;
		port->reset_started = now;
	}
	/* USB 2 devices require at least 10 ms recovery after reset ends. */
	if (now - port->reset_started < UINT64_C(10000000))
		return UX_STATE_WAIT;
	port->reset_pending = false;
	return UX_STATE_NEXT;
}

static UINT
port_status(struct xhci *xhci, unsigned int index)
{
	uint32_t status = read32(xhci, port_register(xhci, index));
	if (status == UINT32_MAX)
		return UX_PORT_INDEX_UNKNOWN;
	unsigned int speed = (status >> 10) & 0xfU;
	if ((status & XHCI_PORT_CONNECTED) == 0 || speed > 3)
		return 0;
	return UX_PS_CCS | ((status & XHCI_PORT_ENABLED) != 0 ? UX_PS_PES : 0) |
	    (speed == 3 ? UX_PS_DS_HS : speed == 2 ? UX_PS_DS_LS : UX_PS_DS_FS);
}

static unsigned int
endpoint_id(const UX_ENDPOINT *endpoint)
{
	unsigned int address = endpoint->ux_endpoint_descriptor.bEndpointAddress;
	return 2U * (address & 0xfU) + ((address & UX_ENDPOINT_DIRECTION) != 0);
}

static unsigned int
highest_endpoint(const struct xhci_port *port, unsigned int excluded)
{
	unsigned int highest = 1;
	for (unsigned int i = 0; i < 30; i++)
		if (port->endpoints[i].id != excluded && port->endpoints[i].id > highest)
			highest = port->endpoints[i].id;
	return highest;
}

static UINT
create_interrupt_endpoint(struct xhci *xhci, unsigned int index, UX_ENDPOINT *endpoint)
{
	struct xhci_port *port = &xhci->ports[index];
	struct xhci_device_dma *device = &xhci->devices[index];
	const UX_ENDPOINT_DESCRIPTOR *descriptor = &endpoint->ux_endpoint_descriptor;
	unsigned int address = descriptor->bEndpointAddress;
	unsigned int packet = descriptor->wMaxPacketSize;
	unsigned int interval = descriptor->bInterval;
	unsigned int speed = (read32(xhci, port_register(xhci, index)) >> 10) & 0xfU;
	if (port->slot == 0 || (address & 0x7fU) == 0 || (address & 0x7fU) > 15 ||
	    speed < 1 || speed > 3 ||
	    (descriptor->bmAttributes & UX_MASK_ENDPOINT_TYPE) != UX_INTERRUPT_ENDPOINT ||
	    packet == 0 || packet > (speed == 3 ? 1024U : speed == 2 ? 8U : 64U) ||
	    interval == 0 || (speed == 3 && interval > 16))
		return UX_FUNCTION_NOT_SUPPORTED;

	unsigned int dci = endpoint_id(endpoint);
	struct xhci_endpoint *ring = &port->endpoints[dci - 2];
	if (ring->id != 0)
		return UX_ERROR;
	struct dma_buffer_info info;
	if (!dma_alloc(sizeof(struct xhci_endpoint_dma), XHCI_PAGE,
	    (read32(xhci, XHCI_HCCPARAMS1) & 1U) != 0 ? 64 : 32, &ring->allocation))
		return UX_MEMORY_INSUFFICIENT;
	if (!dma_buffer_query(ring->allocation, &info) ||
	    !dma_descriptor_ring_pin(ring->allocation)) {
		(void)dma_free(ring->allocation);
		*ring = (struct xhci_endpoint){0};
		return UX_ERROR;
	}
	ring->memory = info.cpu_virtual_address;
	ring->address = info.device_address;
	ring->id = dci;
	ring->cycle = 1;

	if (speed == 3)
		interval--;
	else
		interval = 3U + (31U - (unsigned int)__builtin_clz(interval));
	unsigned int stride = xhci->context_words;
	uint32_t *input = device->input;
	for (unsigned int i = 0; i < 33 * stride; i++)
		input[i] = 0;
	input[1] = 1U | (1U << dci);
	for (unsigned int i = 0; i < stride; i++)
		input[stride + i] = device->output[i];
	input[stride] = (input[stride] & 0x07ffffffU) | (highest_endpoint(port, 0) << 27);

	unsigned int type = (address & UX_ENDPOINT_DIRECTION) != 0 ? 7U : 3U;
	uint32_t *context = input + (dci + 1U) * stride;
	context[0] = interval << 16;
	context[1] = (packet << 16) | (type << 3) | (3U << 1);
	context[2] = (uint32_t)ring->address | 1U;
	context[3] = (uint32_t)(ring->address >> 32);
	context[4] = (packet << 16) | packet;
	if (!submit_command(xhci, XHCI_CONFIGURE_ENDPOINT | (port->slot << 24),
	    port_dma(xhci, index), NULL)) {
		/* A timed-out command may still have installed this context.
		 * Keep DMA pinned until Disable Slot proves it can be released. */
		ring->stopped = true;
		return UX_ERROR;
	}
	endpoint->ux_endpoint_ed = ring;
	endpoint->ux_endpoint_transfer_request.ux_transfer_request_maximum_length = packet;
	return UX_SUCCESS;
}

static UINT
interrupt_request(struct xhci *xhci, unsigned int index, UX_TRANSFER *request)
{
	struct xhci_port *port = &xhci->ports[index];
	unsigned int dci = endpoint_id(request->ux_transfer_request_endpoint);
	if (dci < 2 || dci > 31)
		return UX_STATE_ERROR;
	struct xhci_endpoint *ring = &port->endpoints[dci - 2];
	unsigned int length = request->ux_transfer_request_requested_length;
	if (ring->id != dci || ring->stopped ||
	    request->ux_transfer_request_data_pointer == NULL || length == 0 ||
	    length > request->ux_transfer_request_maximum_length || length > XHCI_PAGE) {
		request->ux_transfer_request_completion_code = UX_TRANSFER_ERROR;
		return UX_STATE_ERROR;
	}
	if (ring->request != NULL)
		return UX_STATE_WAIT;
	if (request->ux_transfer_request_status == UX_TRANSFER_STATUS_COMPLETED)
		return request->ux_transfer_request_completion_code == UX_SUCCESS ?
		    UX_STATE_NEXT : UX_STATE_ERROR;

	unsigned int pos = ring->index;
	unsigned int cycle = ring->cycle;
	if (pos == XHCI_RING_COUNT - 2) {
		ring->memory->ring[pos + 1] = (struct xhci_trb){
			ring->address, 0, XHCI_TRB_LINK | XHCI_LINK_TOGGLE | cycle
		};
		ring->index = 0;
		ring->cycle ^= 1U;
	} else {
		ring->index++;
	}
	if ((dci & 1U) == 0)
		for (unsigned int i = 0; i < length; i++)
			ring->memory->data[i] = request->ux_transfer_request_data_pointer[i];

	ring->request = request;
	ring->done = false;
	ring->trb = ring->address + pos * sizeof(struct xhci_trb);
	volatile struct xhci_trb *trb = &ring->memory->ring[pos];
	trb->parameter = ring->address + offsetof(struct xhci_endpoint_dma, data);
	trb->status = length;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	trb->control = XHCI_NORMAL | XHCI_TRB_IOC | XHCI_TRB_ISP | cycle;
	__asm__ volatile ("mfence" : : : "memory");
	write32(xhci, xhci->doorbells + port->slot * 4, dci);
	return UX_STATE_WAIT;
}

static void
finish_interrupt(struct xhci_endpoint *ring)
{
	UX_TRANSFER *request = ring->request;
	if (request == NULL || !ring->done)
		return;

	ring->request = NULL;
	ring->done = false;
	unsigned int code = ring->event.status >> 24;
	unsigned int remaining = ring->event.status & 0xffffffU;
	unsigned int length = request->ux_transfer_request_requested_length;
	bool success = (code == XHCI_COMPLETION_SUCCESS || code == XHCI_COMPLETION_SHORT) &&
	    remaining <= length;
	request->ux_transfer_request_actual_length = success ? length - remaining : 0;
	request->ux_transfer_request_completion_code = success ? UX_SUCCESS : UX_TRANSFER_ERROR;
	request->ux_transfer_request_status = UX_TRANSFER_STATUS_COMPLETED;
	if (success) {
		ring->reports++;
		if ((ring->id & 1U) != 0)
			for (unsigned int i = 0; i < length - remaining; i++)
				request->ux_transfer_request_data_pointer[i] = ring->memory->data[i];
	} else {
		ring->stopped = true;
	}
	if (request->ux_transfer_request_completion_function != NULL)
		request->ux_transfer_request_completion_function(request);
}

static UINT
destroy_interrupt_endpoint(struct xhci *xhci, unsigned int index, UX_ENDPOINT *endpoint)
{
	struct xhci_port *port = &xhci->ports[index];
	struct xhci_endpoint *ring = &port->endpoints[endpoint_id(endpoint) - 2];
	if (endpoint->ux_endpoint_ed != ring || ring->id == 0)
		return UX_SUCCESS;
	endpoint->ux_endpoint_ed = NULL;
	stop_interrupt(xhci, port, ring);

	struct xhci_device_dma *device = &xhci->devices[index];
	unsigned int stride = xhci->context_words;
	uint32_t *input = device->input;
	for (unsigned int i = 0; i < 33 * stride; i++)
		input[i] = 0;
	input[0] = 1U << ring->id;
	input[1] = 1U;
	for (unsigned int i = 0; i < stride; i++)
		input[stride + i] = device->output[i];
	input[stride] = (input[stride] & 0x07ffffffU) | (highest_endpoint(port, ring->id) << 27);
	if (!submit_command(xhci, XHCI_CONFIGURE_ENDPOINT | (port->slot << 24),
	    port_dma(xhci, index), NULL))
		return UX_ERROR;
	release_endpoint(ring);
	return UX_SUCCESS;
}

static UINT
xhci_entry(UX_HCD *hcd, UINT operation, VOID *parameter)
{
	struct xhci *xhci = hcd->ux_hcd_controller_hardware;
	size_t index = (uintptr_t)parameter;

	switch (operation) {
	case UX_HCD_TASKS_RUN: {
		/* Drain events before dispatching callbacks, which may queue more I/O. */
		struct xhci_trb event;
		for (unsigned int i = 0; i < XHCI_RING_COUNT && take_event(xhci, &event); i++)
			record_transfer_event(xhci, &event);
		for (unsigned int i = 0; i < xhci->port_count; i++) {
			size_t offset = port_register(xhci, i);
			uint32_t status = read32(xhci, offset);
			if (status == UINT32_MAX)
				continue;
			bool connected = (port_status(xhci, i) & UX_PS_CCS) != 0;
			struct xhci_port *port = &xhci->ports[i];
			bool changed = (status & XHCI_PORT_CONNECT_CHANGE) != 0;
			if ((status & XHCI_PORT_CHANGE_BITS) != 0)
				write32(xhci, offset,
				    status & (XHCI_PORT_PRESERVE | XHCI_PORT_CHANGE_BITS));
			if (connected != port->connected) {
				port->connected = connected;
				port->reset_pending = false;
				hcd->ux_hcd_root_hub_signal[i] = 1;
			} else if (changed && connected) {
				/* A disconnect/reconnect can occur between polls. */
				port->reset_pending = false;
				hcd->ux_hcd_root_hub_signal[i] = 2;
			}
			if (connected && hcd->ux_hcd_root_hub_signal[i] == 0)
				for (unsigned int j = 0; j < 30; j++)
					finish_interrupt(&port->endpoints[j]);
		}
		return UX_SUCCESS;
	}
	case UX_HCD_GET_PORT_STATUS:
		return index < xhci->port_count ? port_status(xhci, index) : UX_PORT_INDEX_UNKNOWN;
	case UX_HCD_ENABLE_PORT:
	case UX_HCD_POWER_ON_PORT:
		if (index >= xhci->port_count)
			return UX_PORT_INDEX_UNKNOWN;
		if ((read32(xhci, port_register(xhci, index)) & XHCI_PORT_POWER) == 0)
			write32(xhci, port_register(xhci, index), XHCI_PORT_POWER);
		return UX_SUCCESS;
	case UX_HCD_RESET_PORT:
		return index < xhci->port_count ? port_reset(xhci, index) : UX_STATE_EXIT;
	case UX_HCD_CREATE_ENDPOINT:
	case UX_HCD_DESTROY_ENDPOINT: {
		UX_ENDPOINT *endpoint = parameter;
		UX_DEVICE *device = endpoint->ux_endpoint_device;
		if (UX_DEVICE_PARENT_GET(device) != UX_NULL)
			return UX_FUNCTION_NOT_SUPPORTED;
		index = UX_DEVICE_PORT_LOCATION_GET(device);
		if (index >= xhci->port_count)
			return UX_PORT_INDEX_UNKNOWN;
		if (endpoint->ux_endpoint_descriptor.bEndpointAddress != 0) {
			if (operation == UX_HCD_CREATE_ENDPOINT) {
				struct xhci_port *port = &xhci->ports[index];
				port->last_endpoint_address = endpoint->ux_endpoint_descriptor.bEndpointAddress;
				port->last_endpoint_status = create_interrupt_endpoint(xhci, index, endpoint);
				return port->last_endpoint_status;
			}
			return destroy_interrupt_endpoint(xhci, index, endpoint);
		}
		endpoint->ux_endpoint_transfer_request.ux_transfer_request_maximum_length = XHCI_PAGE;
		if (operation == UX_HCD_DESTROY_ENDPOINT) {
			struct xhci_port *port = &xhci->ports[index];
			stop_port_transfers(xhci, port);
			if (!disable_slot(xhci, port))
				return UX_ERROR;
		}
		return UX_SUCCESS;
	}
	case UX_HCD_TRANSFER_RUN: {
		UX_TRANSFER *request = parameter;
		UX_ENDPOINT *endpoint = request->ux_transfer_request_endpoint;
		UX_DEVICE *device = endpoint->ux_endpoint_device;
		index = UX_DEVICE_PORT_LOCATION_GET(device);
		UINT status = UX_TRANSFER_ERROR;
		unsigned int actual = 0;
		if (index < xhci->port_count && UX_DEVICE_PARENT_GET(device) == UX_NULL) {
			if (endpoint->ux_endpoint_descriptor.bEndpointAddress != 0)
				return interrupt_request(xhci, index, request);
			if (request->ux_transfer_request_function == UX_SET_ADDRESS &&
			    request->ux_transfer_request_type == 0 &&
			    request->ux_transfer_request_requested_length == 0)
				status = address_device(xhci, index) ? UX_SUCCESS : UX_TRANSFER_ERROR;
			else
				status = control_request(xhci, index, request, &actual);
			if (status == UX_TRANSFER_ERROR) {
				struct xhci_port *port = &xhci->ports[index];
				stop_port_transfers(xhci, port);
				(void)disable_slot(xhci, port);
			}
		}
		request->ux_transfer_request_actual_length = status == UX_SUCCESS ? actual : 0;
		request->ux_transfer_request_completion_code = status;
		request->ux_transfer_request_status = UX_TRANSFER_STATUS_COMPLETED;
		return status == UX_SUCCESS ? UX_STATE_NEXT : UX_STATE_ERROR;
	}
	case UX_HCD_TRANSFER_ABORT: {
		UX_TRANSFER *request = parameter;
		index = UX_DEVICE_PORT_LOCATION_GET(request->ux_transfer_request_endpoint->ux_endpoint_device);
		unsigned int dci = endpoint_id(request->ux_transfer_request_endpoint);
		if (index < xhci->port_count && dci >= 2 && dci <= 31) {
			struct xhci_port *port = &xhci->ports[index];
			if (port->endpoints[dci - 2].request == request)
				stop_interrupt(xhci, port, &port->endpoints[dci - 2]);
		}
		return UX_SUCCESS;
	}
	default:
		return UX_FUNCTION_NOT_SUPPORTED;
	}
}

static UINT
register_hcd(UX_HCD *hcd)
{
	struct xhci *xhci = &controllers[hcd->ux_hcd_io];
	hcd->ux_hcd_available_bandwidth = 6000U;
	hcd->ux_hcd_nb_root_hubs = xhci->port_count;
	hcd->ux_hcd_controller_hardware = xhci;
	hcd->ux_hcd_entry_function = xhci_entry;
	for (unsigned int i = 0; i < xhci->port_count; i++) {
		size_t offset = port_register(xhci, i);
		uint32_t status = read32(xhci, offset);
		if (status != UINT32_MAX && (status & XHCI_PORT_POWER) == 0)
			write32(xhci, offset, (status & XHCI_PORT_PRESERVE) | XHCI_PORT_POWER);
	}
	hcd->ux_hcd_status = UX_HCD_STATUS_OPERATIONAL;
	return UX_SUCCESS;
}

void
xhci_input_diagnostics(void (*write_value)(const char *, uint64_t, bool))
{
	for (unsigned int controller = 0; controller < UX_MAX_HCD; controller++) {
		struct xhci *xhci = &controllers[controller];
		if (xhci->mmio == NULL)
			continue;
		write_value("xHCI", controller, false);
		write_value(" USBSTS", read32(xhci, xhci->op + XHCI_USBSTS), true);
		for (unsigned int i = 0; i < xhci->port_count; i++) {
			struct xhci_port *port = &xhci->ports[i];
			uint32_t status = read32(xhci, port_register(xhci, i));
			if ((status & XHCI_PORT_CONNECTED) == 0 && port->last_command == 0)
				continue;
			write_value("port", i + 1U, false);
			write_value(" PORTSC", status, false);
			write_value(" slot", port->slot, false);
			write_value(" cmd", port->last_command, false);
			write_value(" cc", port->last_completion, true);
			write_value("request", port->last_control_request, false);
			write_value(" value", port->last_control_value, false);
			write_value(" cc", port->last_control_completion, false);
			write_value(" ep-address", port->last_endpoint_address, false);
			write_value(" create-status", port->last_endpoint_status, true);
			for (unsigned int j = 0; j < 30; j++) {
				struct xhci_endpoint *endpoint = &port->endpoints[j];
				if (endpoint->id == 0)
					continue;
				write_value(" endpoint", endpoint->id, false);
				write_value(" pending", endpoint->request != NULL, false);
				write_value(" stopped", endpoint->stopped, false);
				write_value(" reports", endpoint->reports, true);
			}
		}
	}
}

bool
xhci_attach(const struct pci_device_info *device, unsigned int controller)
{
	struct pci_device_lease claim = {0};
	struct pci_mmio_lease mmio = {0};
	struct pci_bar_resource bar;
	struct dma_buffer dma = {0};
	struct dma_buffer_info info;
	volatile void *address;
	static struct xhci xhci;
	xhci = (struct xhci){ .command_cycle = 1, .event_cycle = 1 };
	uint16_t memory_command = (device->command | PCI_MEMORY_ENABLE) & ~PCI_BUS_MASTER;
	bool command_changed = false, programmed = false, pinned = false;
	const char *stage = "BAR0 sizing";
	bool intel = device->vendor_id == PCI_VENDOR_INTEL;

	if (controller >= UX_MAX_HCD)
		return false;
	if (!dma_identity_addressing_available()) {
		klog("OpenSillicon: xHCI DMA unavailable under current Limine boot contract\n");
		return false;
	}
	if (!pci_device_claim(device, &claim)) {
		klog("OpenSillicon: xHCI PCI claim failed\n");
		return false;
	}
	if (!pci_bar_probe_size(&claim, 0, &bar) ||
	    bar.aperture_size < 0x20 || bar.aperture_size > SIZE_MAX)
		goto done;
	stage = "BAR0 mapping";
	if (!pci_mmio_map(&claim, &bar, 0, (size_t)bar.aperture_size, &mmio) ||
	    !pci_mmio_lease_address(&mmio, &address))
		goto done;

	xhci.mmio = address;
	xhci.length = mmio.length;
	stage = "PCI memory decode";
	command_changed = true;
	if (!pci_command(device, memory_command))
		goto done;

	stage = "capabilities";
	uint32_t cap = read32(&xhci, 0);
	uint32_t hcs1 = read32(&xhci, XHCI_HCSPARAMS1);
	uint32_t hcs2 = read32(&xhci, XHCI_HCSPARAMS2);
	uint32_t hcc = read32(&xhci, XHCI_HCCPARAMS1);
	unsigned int ports = hcs1 >> 24;
	unsigned int slots = hcs1 & 0xffU;
	xhci.op = cap & 0xffU;
	xhci.runtime = read32(&xhci, XHCI_RTSOFF) & ~31U;
	xhci.doorbells = read32(&xhci, XHCI_DBOFF) & ~3U;
	if (cap == UINT32_MAX || xhci.op < 0x20 || (xhci.op & 3U) != 0 ||
	    xhci.runtime < 0x20 || xhci.doorbells < 0x20 ||
	    slots == 0 || ports == 0 || ((hcs1 >> 8) & 0x7ffU) == 0 ||
	    !range_fits(&xhci, xhci.op, XHCI_PORTSC + ports * XHCI_PORT_STRIDE) ||
	    !range_fits(&xhci, xhci.runtime, XHCI_ERDP + 8) ||
	    !range_fits(&xhci, xhci.doorbells, (slots + 1) * 4))
		goto done;
	stage = "firmware ownership handoff";
	if (!take_ownership(&xhci, hcc))
		goto done;
	stage = "controller reset";
	if (!reset_controller(&xhci, intel))
		goto done;
	stage = "4 KiB page support";
	if ((read32(&xhci, xhci.op + XHCI_PAGESIZE) & 1U) == 0)
		goto done;

	stage = "DMA allocation";
	unsigned int scratchpads = ((hcs2 >> 21) & 0x1fU) * 32 + (hcs2 >> 27);
	size_t device_offset = 2 * XHCI_PAGE;
	xhci.port_count = ports < UX_MAX_ROOTHUB_PORT ? ports : UX_MAX_ROOTHUB_PORT;
	xhci.slot_count = slots;
	size_t array_offset = device_offset + xhci.port_count * sizeof(struct xhci_device_dma);
	size_t pages_offset = array_offset +
	    ((scratchpads * sizeof(uint64_t) + XHCI_PAGE - 1) & ~(size_t)(XHCI_PAGE - 1));
	if (!dma_alloc(pages_offset + scratchpads * XHCI_PAGE, XHCI_PAGE,
	    (hcc & 1U) != 0 ? 64 : 32, &dma) || !dma_buffer_query(dma, &info))
		goto done;

	xhci.memory = info.cpu_virtual_address;
	xhci.dma_address = info.device_address;
	xhci.context_words = (hcc & (1U << 2)) != 0 ? 16 : 8;
	xhci.devices = (struct xhci_device_dma *)((uint8_t *)xhci.memory + device_offset);
	xhci.devices_address = xhci.dma_address + device_offset;
	if (scratchpads != 0) {
		uint64_t *array = (uint64_t *)((uint8_t *)xhci.memory + array_offset);
		for (unsigned int i = 0; i < scratchpads; i++)
			array[i] = xhci.dma_address + pages_offset + i * XHCI_PAGE;
		xhci.memory->dcbaa[0] = xhci.dma_address + array_offset;
	}

	xhci.memory->erst.address = xhci.dma_address + offsetof(struct xhci_dma_layout, events);
	xhci.memory->erst.count = XHCI_RING_COUNT;
	stage = "DMA ring pin";
	if (!dma_descriptor_ring_pin(dma))
		goto done;
	pinned = true;
	programmed = true;
	stage = "PCI bus mastering";
	if (!pci_command(device, memory_command | PCI_BUS_MASTER))
		goto done;
	/* ERSTBA can cause the controller to fetch the segment table immediately. */
	write64(&xhci, xhci.op + XHCI_DCBAAP, xhci.dma_address);
	write64(&xhci, xhci.op + XHCI_CRCR,
	    (xhci.dma_address + offsetof(struct xhci_dma_layout, commands)) | 1U);
	write32(&xhci, xhci.op + XHCI_CONFIG, slots);
	write32(&xhci, xhci.runtime + XHCI_IMAN, 1U); /* Clear pending, keep IRQ disabled. */
	write32(&xhci, xhci.runtime + XHCI_ERSTSZ, 1U);
	write64(&xhci, xhci.runtime + XHCI_ERDP, xhci.memory->erst.address);
	write64(&xhci, xhci.runtime + XHCI_ERSTBA,
	    xhci.dma_address + offsetof(struct xhci_dma_layout, erst));

	write32(&xhci, xhci.op + XHCI_USBCMD, XHCI_RUN);
	unsigned int slot;
	stage = "run";
	if (!wait_register(&xhci, xhci.op + XHCI_USBSTS, XHCI_HALTED, 0))
		goto done;
	stage = "No Op and ring wrap";
	/* Exercise the command Link TRB and the event consumer cycle. */
	for (unsigned int i = 0; i < XHCI_RING_COUNT + 1; i++)
		if (!submit_command(&xhci, XHCI_NOOP, 0, NULL))
			goto done;
	stage = "slot commands";
	if (!submit_command(&xhci, XHCI_ENABLE_SLOT, 0, &slot) ||
	    slot == 0 || slot > slots ||
	    !submit_command(&xhci, XHCI_DISABLE_SLOT | (slot << 24), 0, NULL))
		goto done;

	controllers[controller] = xhci;
	stage = "USBX HCD registration";
	if (ux_host_stack_hcd_register((UCHAR *)"xhci", register_hcd, controller, 0) != UX_SUCCESS)
		goto done;
	klog("OpenSillicon: xHCI command ring verified and attached to USBX\n");
	return true;

done:
	klog("OpenSillicon: xHCI attach failed at ");
	klog(stage);
	klog(" (PCI ");
	klog_u64(device->bus);
	klog(":");
	klog_u64(device->device);
	klog(".");
	klog_u64(device->function);
	klog(")\n");
	/* Keep the claim, mapping and buffer if hardware cannot be quiesced. */
	if (programmed) {
		bool stopped = reset_controller(&xhci, intel);
		bool master_off = pci_command(device, memory_command);
		if (!stopped || !master_off) {
			klog("OpenSillicon: xHCI quiesce failed, resources retained\n");
			return false;
		}
	}
	if (command_changed)
		(void)pci_command(device, programmed ? device->command & ~PCI_BUS_MASTER : device->command);
	if (pinned)
		(void)dma_descriptor_ring_unpin(dma);
	if (dma.generation != 0)
		(void)dma_free(dma);
	if (mmio.generation != 0)
		(void)pci_mmio_release(&mmio);
	(void)pci_device_release(&claim);
	return false;
}
