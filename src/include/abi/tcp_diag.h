#ifndef OPENSILLICON_ABI_TCP_DIAG_H
#define OPENSILLICON_ABI_TCP_DIAG_H

#include <stdint.h>

#define OS_TCP_DIAG 0x4f01U

/* Read-only OpenSillicon TCP socket snapshot (IPPROTO_TCP). */
struct os_tcp_diag {
	uint32_t state;
	uint32_t receive_window;
	uint32_t receive_queued;
	uint32_t receive_advanced;
	uint32_t out_of_order_blocks;
	uint32_t rejected_segments;
	uint32_t ack_sent;
	uint32_t ack_failed;
	uint32_t last_ack_window;
};

#endif
