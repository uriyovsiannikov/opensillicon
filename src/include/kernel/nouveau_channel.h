/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_CHANNEL_H
#define KERNEL_NOUVEAU_CHANNEL_H
#include <stdbool.h>
#include <stdint.h>
/* GP102 core (0), base (1..4), overlay (5..8). PIO channels unsupported. */
enum nouveau_channel_state {
	NOUVEAU_CHANNEL_EMPTY, NOUVEAU_CHANNEL_PREPARED, NOUVEAU_CHANNEL_RUNNING,
	NOUVEAU_CHANNEL_STOPPED, NOUVEAU_CHANNEL_QUARANTINED
};
enum nouveau_channel_status {
	NOUVEAU_CHANNEL_OK, NOUVEAU_CHANNEL_INVALID, NOUVEAU_CHANNEL_BUSY,
	NOUVEAU_CHANNEL_IO_FAILED, NOUVEAU_CHANNEL_TIMEOUT,
	NOUVEAU_CHANNEL_RESOURCE_FAILED
};
struct nouveau_channel_io {
	void *context;
	bool (*read32)(void *,uint32_t reg,uint32_t *value);
	bool (*write32)(void *,uint32_t reg,uint32_t value);
	bool (*time_us)(void *,uint64_t *value);
	bool (*delay_us)(void *,unsigned value);
	/* acquire must establish device ownership of a real HOST push allocation
	 * and uploaded instance/RAMHT/notifier. Caller owns display/PCI lifecycle.
	 * false must leave resources CPU-owned. release is permitted only after
	 * the channel is demonstrably stopped. */
	bool (*acquire)(void *);
	bool (*release)(void *);
};
struct nouveau_channel {
	enum nouveau_channel_state state;
	uint32_t push, saved_put, prepared_bytes;
	uint8_t id;
	bool resources_held;
};
/* Does not perform IO. Zero-initialize first. HOST push must be 4KiB aligned. */
bool nouveau_channel_prepare(struct nouveau_channel *,unsigned id,uint32_t push, uint32_t prepared_bytes);
/* Requires an already inactive channel and an owning engine lifecycle.
 * Starts with PUT=0; this does not submit staged timing/surface methods.
 * Any failure after acquiring resources quarantines the channel. */
enum nouveau_channel_status nouveau_channel_start(struct nouveau_channel *,
	const struct nouveau_channel_io *);
/* Publish a forward-only byte offset in a prepared 4KiB push buffer.
 * Caller must have finalized/flushed all visible words before acquire.
 * No ring wrapping or CPU editing while device-owned is supported.
 * This can execute GPU methods: never call it from a discovery probe. */
enum nouveau_channel_status nouveau_channel_kick(struct nouveau_channel *,
	const struct nouveau_channel_io *, uint32_t put_bytes);
/* Bounded GF119 stop sequence, retaining resources on IO/timeout. Retryable
 * from quarantine. No blind register restoration, IRQ enabling or UPDATE. */
enum nouveau_channel_status nouveau_channel_stop(struct nouveau_channel *,
	const struct nouveau_channel_io *);
/* Release only after confirmed stop. Callback failure preserves ownership. */
enum nouveau_channel_status nouveau_channel_release(struct nouveau_channel *,
	const struct nouveau_channel_io *);
#endif
