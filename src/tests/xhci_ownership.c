#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../kernel/drivers/input/usb/opensillicon/xhci.c"

static uint64_t fake_time;
static unsigned int waits;
static bool release_bios;
static uint32_t registers[512];
static char diagnostics[1024];
static struct xhci *emulated;
static struct dma_buffer_info allocations[64];
static bool pinned[64];
static unsigned int allocated_count;

bool
dma_alloc(size_t size, size_t alignment, unsigned int bits, struct dma_buffer *buffer)
{
	(void)bits;
	unsigned int i;
	for (i = 0; i < 64 && allocations[i].cpu_virtual_address != NULL; i++)
		;
	assert(i < 64);
	void *memory = aligned_alloc(alignment, size);
	assert(memory != NULL);
	memset(memory, 0, size);
	allocations[i] = (struct dma_buffer_info){
		.cpu_virtual_address = memory, .device_address = (uintptr_t)memory,
		.allocated_size = size,
	};
	*buffer = (struct dma_buffer){.slot = i, .generation = 1};
	allocated_count++;
	return true;
}

bool
dma_buffer_query(struct dma_buffer buffer, struct dma_buffer_info *info)
{
	*info = allocations[buffer.slot];
	return true;
}

bool
dma_descriptor_ring_pin(struct dma_buffer buffer)
{
	pinned[buffer.slot] = true;
	return true;
}

bool
dma_descriptor_ring_unpin(struct dma_buffer buffer)
{
	assert(pinned[buffer.slot]);
	pinned[buffer.slot] = false;
	return true;
}

bool
dma_free(struct dma_buffer buffer)
{
	assert(!pinned[buffer.slot]);
	free(allocations[buffer.slot].cpu_virtual_address);
	allocations[buffer.slot] = (struct dma_buffer_info){0};
	allocated_count--;
	return true;
}

_Noreturn void
kernel_halt(void)
{
	abort();
}


uint64_t
timer_monotonic_ns(void)
{
	return fake_time;
}

void
timer_wait_for_interrupt(void)
{
	fake_time += UINT64_C(10000000);
	waits++;
	if (emulated != NULL) {
		unsigned int pos = emulated->command_index == 0 ?
		    XHCI_RING_COUNT - 2 : emulated->command_index - 1;
		struct xhci_trb *command = &emulated->memory->commands[pos];
		if (((command->control >> 10) & 0x3fU) == 12) {
			uint32_t *input = emulated->devices[0].input;
			uint32_t *output = emulated->devices[0].output;
			unsigned int stride = emulated->context_words;
			for (unsigned int dci = 0; dci < 32; dci++) {
				if ((input[1] & (1U << dci)) == 0)
					continue;
				memcpy(output + dci * stride, input + (dci + 1) * stride,
				    stride * sizeof(uint32_t));
			}
		}
		emulated->memory->events[emulated->event_index] = (struct xhci_trb){
			.parameter = emulated->dma_address +
			    offsetof(struct xhci_dma_layout, commands) + pos * sizeof(*command),
			.status = XHCI_COMPLETION_SUCCESS << 24,
			.control = (XHCI_EVENT_COMMAND << 10) | emulated->event_cycle | (1U << 24),
		};
	}
	if (release_bios)
		registers[16] &= ~XHCI_LEGACY_BIOS;
}

void
klog(const char *text)
{
	assert(strlen(diagnostics) + strlen(text) < sizeof(diagnostics));
	strcat(diagnostics, text);
}

int
main(void)
{
	struct xhci controller = {
		.mmio = (volatile uint8_t *)registers,
		.length = sizeof(registers),
	};
	assert(take_ownership(&controller, 0));

	/* A supported-protocol capability can precede the legacy capability. */
	registers[16] = 2U | (4U << 8);
	registers[20] = 1U;
	registers[21] = UINT32_C(0x1fffffff);
	assert(take_ownership(&controller, 16U << 16));
	assert(registers[20] == (1U | XHCI_LEGACY_OS));
	assert(registers[21] ==
	    ((UINT32_C(0x1fffffff) & XHCI_LEGACY_PRESERVE) | XHCI_LEGACY_CLEAR_STATUS));
	assert(waits == 0);

	/* Cooperative firmware releases the semaphore after the request. */
	registers[16] = 1U | XHCI_LEGACY_BIOS;
	release_bios = true;
	assert(take_ownership(&controller, 16U << 16));
	assert(waits == 1);
	assert(diagnostics[0] == '\0');

	/* Nonresponsive firmware requires reclaiming ownership after timeout. */
	registers[16] = 1U | XHCI_LEGACY_BIOS;
	registers[17] = 0;
	release_bios = false;
	waits = 0;
	fake_time = 0;
	assert(take_ownership(&controller, 16U << 16));
	assert(waits == 100);
	assert((registers[16] & (XHCI_LEGACY_OS | XHCI_LEGACY_BIOS)) == XHCI_LEGACY_OS);
	assert(strstr(diagnostics, "BIOS ownership timeout") != NULL);

	registers[16] = UINT32_MAX;
	assert(!take_ownership(&controller, 16U << 16));
	assert(!take_ownership(&controller, 512U << 16));
	registers[511] = 1U;
	assert(!take_ownership(&controller, 511U << 16));
	/* Port reset completion must be followed by a full recovery interval. */
	controller.op = 0x20;
	controller.port_count = 1;
	size_t port = port_register(&controller, 0) / sizeof(registers[0]);
	registers[port] = XHCI_PORT_CONNECTED | XHCI_PORT_POWER;
	fake_time = 0;
	assert(port_reset(&controller, 0) == UX_STATE_WAIT);
	assert((registers[port] & XHCI_PORT_RESET) != 0);
	registers[port] = XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED | XHCI_PORT_POWER;
	fake_time = UINT64_C(20000000);
	assert(port_reset(&controller, 0) == UX_STATE_WAIT);
	fake_time += UINT64_C(9999999);
	assert(port_reset(&controller, 0) == UX_STATE_WAIT);
	fake_time++;
	assert(port_reset(&controller, 0) == UX_STATE_NEXT);
	assert(!controller.ports[0].reset_pending);
	registers[port] = 0;
	assert(port_reset(&controller, 0) == UX_STATE_EXIT);
	/* A composite device can create IN endpoint 2 before IN and OUT endpoint 1. */
	controller.memory = calloc(1, sizeof(*controller.memory));
	controller.devices = aligned_alloc(XHCI_PAGE, sizeof(*controller.devices));
	assert(controller.memory != NULL && controller.devices != NULL);
	memset(controller.devices, 0, sizeof(*controller.devices));
	controller.devices_address = (uintptr_t)controller.devices;
	controller.dma_address = (uintptr_t)controller.memory;
	controller.doorbells = 0x500;
	controller.context_words = 8;
	controller.event_cycle = controller.command_cycle = 1;
	controller.ports[0].slot = 1;
	registers[port] = XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED | (1U << 10);
	emulated = &controller;
	UX_ENDPOINT endpoints[3] = {0};
	const unsigned int addresses[] = {0x82, 0x81, 0x01};
	for (unsigned int i = 0; i < 3; i++) {
		endpoints[i].ux_endpoint_descriptor = (UX_ENDPOINT_DESCRIPTOR){
			.bEndpointAddress = addresses[i], .bmAttributes = UX_INTERRUPT_ENDPOINT,
			.wMaxPacketSize = 8, .bInterval = 10,
		};
		assert(create_interrupt_endpoint(&controller, 0, &endpoints[i]) == UX_SUCCESS);
		assert((controller.devices->output[0] >> 27) == 5);
	}
	assert(allocated_count == 3);
	unsigned char payloads[3][8] = { {0}, {0}, {42} };
	for (unsigned int i = 0; i < 3; i++) {
		UX_TRANSFER *request = &endpoints[i].ux_endpoint_transfer_request;
		request->ux_transfer_request_endpoint = &endpoints[i];
		request->ux_transfer_request_data_pointer = payloads[i];
		request->ux_transfer_request_requested_length = 8;
		assert(interrupt_request(&controller, 0, request) == UX_STATE_WAIT);
		struct xhci_endpoint *ring = endpoints[i].ux_endpoint_ed;
		if (i == 2)
			assert(ring->memory->data[0] == 42);
		else
			ring->memory->data[0] = 10 + i;
		struct xhci_trb event = {
			.parameter = ring->trb, .status = XHCI_COMPLETION_SUCCESS << 24,
			.control = (XHCI_EVENT_TRANSFER << 10) | (ring->id << 16) | (1U << 24),
		};
		record_transfer_event(&controller, &event);
	}
	for (unsigned int i = 0; i < 3; i++) {
		struct xhci_endpoint *ring = endpoints[i].ux_endpoint_ed;
		finish_interrupt(ring);
		assert(ring->reports == 1);
		assert(interrupt_request(&controller, 0,
		    &endpoints[i].ux_endpoint_transfer_request) == UX_STATE_NEXT);
		assert(payloads[i][0] == (i == 2 ? 42 : 10 + i));
	}
	assert(destroy_interrupt_endpoint(&controller, 0, &endpoints[0]) == UX_SUCCESS);
	assert((controller.devices->output[0] >> 27) == 3);
	assert(allocated_count == 2);
	stop_port_transfers(&controller, &controller.ports[0]);
	assert(disable_slot(&controller, &controller.ports[0]));
	assert(allocated_count == 0);
	emulated = NULL;
	free(controller.memory);
	free(controller.devices);
	puts("xHCI handoff, reset and composite interrupt endpoints: PASS");
}
