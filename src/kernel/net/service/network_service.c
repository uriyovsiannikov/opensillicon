#include <stdbool.h>
#include <stdint.h>

#include "kernel/errno.h"
#include "kernel/fd.h"
#include "kernel/network_service.h"
#include "kernel/spinlock.h"
#include "kernel/udp_socket.h"

static enum network_service_event event_queue[NETWORK_SERVICE_EVENT_CAPACITY];
static size_t event_head;
static size_t event_count;
static bool initialized;
static bool service_running;
static struct spinlock service_lock = SPINLOCK_INITIALIZER;

bool
network_service_init(void)
{
	uint64_t flags = spinlock_lock_irqsave(&service_lock);
	bool success = !initialized;
	if (success) {
		event_head = 0;
		event_count = 0;
		service_running = false;
		initialized = true;
	}
	spinlock_unlock_irqrestore(&service_lock, flags);
	return success;
}

bool
network_service_request(enum network_service_event event)
{
	uint64_t flags = spinlock_lock_irqsave(&service_lock);
	if (!initialized || event != NETWORK_SERVICE_POLL_RX ||
	    event_count == NETWORK_SERVICE_EVENT_CAPACITY) {
		spinlock_unlock_irqrestore(&service_lock, flags);
		return false;
	}
	event_queue[(event_head + event_count) %
	    NETWORK_SERVICE_EVENT_CAPACITY] = event;
	event_count++;
	spinlock_unlock_irqrestore(&service_lock, flags);
	return true;
}

bool
network_service_run(size_t budget, struct network_service_result *result)
{
	struct network_service_result local = {0};
	uint64_t flags = spinlock_lock_irqsave(&service_lock);
	if (result != NULL)
		*result = local;
	if (!initialized) {
		spinlock_unlock_irqrestore(&service_lock, flags);
		return false;
	}
	if (service_running) {
		local.busy = true;
		local.events_pending = event_count;
		if (result != NULL)
			*result = local;
		spinlock_unlock_irqrestore(&service_lock, flags);
		return true;
	}
	if (budget > NETWORK_SERVICE_MAX_EVENTS)
		budget = NETWORK_SERVICE_MAX_EVENTS;
	service_running = true;
	spinlock_unlock_irqrestore(&service_lock, flags);

	while (local.events_processed < budget) {
		enum network_service_event event;
		int poll_result;

		flags = spinlock_lock_irqsave(&service_lock);
		if (event_count == 0) {
			spinlock_unlock_irqrestore(&service_lock, flags);
			break;
		}
		event = event_queue[event_head];
		event_head = (event_head + 1) % NETWORK_SERVICE_EVENT_CAPACITY;
		event_count--;
		spinlock_unlock_irqrestore(&service_lock, flags);

		local.events_processed++;
		if (event != NETWORK_SERVICE_POLL_RX) {
			local.errors++;
			continue;
		}
		poll_result = udp_socket_poll();
		fd_readiness_wake();
		if (poll_result > 0)
			local.frames_processed++;
		else if (poll_result < 0 && poll_result != -ENETDOWN)
			local.errors++;
	}

	flags = spinlock_lock_irqsave(&service_lock);
	service_running = false;
	local.events_pending = event_count;
	spinlock_unlock_irqrestore(&service_lock, flags);
	if (result != NULL)
		*result = local;
	return true;
}
