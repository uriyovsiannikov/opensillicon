/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_ENGINE_H
#define KERNEL_NOUVEAU_ENGINE_H
#include "kernel/nouveau_upload.h"
#include "arch/x86_64/pci.h"
enum nouveau_engine_state {
	NOUVEAU_ENGINE_EMPTY, NOUVEAU_ENGINE_QUIESCENT, NOUVEAU_ENGINE_BOUND,
	NOUVEAU_ENGINE_QUARANTINED
};
enum nouveau_engine_status {
	NOUVEAU_ENGINE_OK, NOUVEAU_ENGINE_INVALID, NOUVEAU_ENGINE_BUSY,
	NOUVEAU_ENGINE_IO_FAILED, NOUVEAU_ENGINE_TIMEOUT
};
struct nouveau_engine_io {
	void *context;
	bool (*read32)(void *,uint32_t,uint32_t *);
	bool (*write32)(void *,uint32_t,uint32_t);
	bool (*time_us)(void *,uint64_t *);
	bool (*delay_us)(void *,unsigned);
};
struct nouveau_engine_cap { uint32_t reg,saved,value; };
struct nouveau_engine {
	enum nouveau_engine_state state;
	const struct nouveau_engine_io *io;
	struct nouveau_upload *instance;
	uint32_t saved_instance,saved_firmware,saved_interrupts[3];
	bool snapshot_valid,firmware_touched,interrupts_touched,instance_touched;
	struct nouveau_engine_cap caps[20];
	uint8_t cap_count;
	bool caps_ready,caps_touched;
};
/* Caller holds exclusive PCI/engine ownership for the entire lifetime.
 * Requires all 17 GP102 channel controls inactive; never steals an active
 * channel. Performs GF119 firmware request/ack and masks interrupts.
 * W1C supervisor acknowledgement cannot be recreated during rollback. */
enum nouveau_engine_status nouveau_engine_take(struct nouveau_engine *,const struct nouveau_engine_io *);
/* Copy GP102 head/SOR capabilities to EVO mirrors with snapshots/readback.
 * Required before bind. GP102 has no DAC engine; no legacy DAC writes. */
enum nouveau_engine_status nouveau_engine_capabilities(struct nouveau_engine *);
/* Pin a verified, uploaded 64KiB instance BEFORE writing 0x610010.
 * Requires successful capability preparation. Does not start channels,
 * enable IRQs or submit UPDATE. */
enum nouveau_engine_status nouveau_engine_bind(struct nouveau_engine *,struct nouveau_upload *);
/* Requires ALL channels inactive. Restore instance/readback before unpin,
 * restore uploaded VRAM, then saved interrupt masks and firmware request bit. On failure keep the
 * transaction quarantined. Upload stays pinned until old instance read-back;
 * after unpin, upload rollback state still prevents premature free. */
enum nouveau_engine_status nouveau_engine_restore(struct nouveau_engine *);
/* Native reversible handoff/capability probe. Does not bind an instance,
 * upload VRAM, start a channel, submit commands or change display timing. */
void nouveau_engine_probe_log(const struct pci_device_lease *,const struct pci_bar_resource *);
#endif
