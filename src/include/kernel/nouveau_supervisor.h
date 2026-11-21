/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_SUPERVISOR_H
#define KERNEL_NOUVEAU_SUPERVISOR_H
#include <stdbool.h>
#include <stdint.h>
enum nouveau_supervisor_action {
	NOUVEAU_SUPERVISOR_SNAPSHOT, NOUVEAU_SUPERVISOR_DETACH1,
	NOUVEAU_SUPERVISOR_DETACH2, NOUVEAU_SUPERVISOR_PLL,
	NOUVEAU_SUPERVISOR_ATTACH2, NOUVEAU_SUPERVISOR_ATTACH3
};
enum nouveau_supervisor_state { NOUVEAU_SUPERVISOR_IDLE, NOUVEAU_SUPERVISOR_PROCESSING, NOUVEAU_SUPERVISOR_FAILED };
enum nouveau_supervisor_status {
	NOUVEAU_SUPERVISOR_OK, NOUVEAU_SUPERVISOR_NO_EVENT,
	NOUVEAU_SUPERVISOR_INVALID, NOUVEAU_SUPERVISOR_IO_FAILED,
	NOUVEAU_SUPERVISOR_ACTION_FAILED, NOUVEAU_SUPERVISOR_AMBIGUOUS
};
struct nouveau_supervisor_io {
	void *context;
	bool (*read32)(void *,uint32_t,uint32_t *);
	bool (*write32)(void *,uint32_t,uint32_t);
	/* Must implement the actual IED scripts, routing and PLL operation.
	 * Returning true from a no-op handler is NOT valid. SNAPSHOT uses head
	 * 255 and captures all armed/async state, other actions use head 0..3. */
	bool (*action)(void *,enum nouveau_supervisor_action,unsigned head);
};
struct nouveau_supervisor {
	enum nouveau_supervisor_state state;
	uint32_t pending,head_masks[4];
	uint8_t head_mask;
	bool acknowledged;
};
/* Poll/process one GF119 supervisor event under exclusive engine ownership.
 * Snapshot all masks BEFORE W1C acknowledgement. On failure do not complete
 * the supervisor or replay actions: engine recovery must stop/unbind first.
 * Multiple simultaneous stage bits are rejected before acknowledgement.
 * No event loop, allocation, IRQ enabling, channel activation or modeset. */
enum nouveau_supervisor_status nouveau_supervisor_step(struct nouveau_supervisor *,
	const struct nouveau_supervisor_io *);
#endif
