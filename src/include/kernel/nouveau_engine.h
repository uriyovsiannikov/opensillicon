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
	uint32_t saved_instance,saved_firmware,saved_supervisor,saved_interrupts[3];
	bool snapshot_valid,firmware_touched,interrupts_touched,instance_touched;
	struct nouveau_engine_cap caps[20];
	uint8_t cap_count;
	bool caps_ready,caps_touched;
	/* Exact rejecting read for BUSY, captured before any rollback/re-read.
	 * Channel controls use mask 0x801e0003; firmware uses 0x3. */
	uint32_t blocked_reg,blocked_value,blocked_mask;
	bool drain_attempted;
	enum nouveau_engine_status drain_status;
};
/* Caller holds exclusive PCI/engine ownership for the entire lifetime.
 * Requires all 17 GP102 channel controls inactive before driver ownership;
 * a disabled core's activity may drain after indicated firmware request/ack.
 * Never steals allocated/connected/PUT-enabled core or another busy channel.
 * W1C supervisor acknowledgement cannot be recreated during rollback. */
enum nouveau_engine_status nouveau_engine_take(struct nouveau_engine *,const struct nouveau_engine_io *);
/* GF119 core fini for an already disabled firmware core only. Requires
 * allocation/connect/PUT disabled and every other channel idle. Reasserts
 * the disabled requests and waits for activity to drain; never restarts or
 * changes descriptors/PUT/instance. Caller owns PCI throughout. */
enum nouveau_engine_status nouveau_engine_drain_disabled_core(const struct nouveau_engine_io *);
/* Copy GP102 head/SOR capabilities to EVO mirrors with snapshots/readback.
 * Required before bind. GP102 has no DAC engine; no legacy DAC writes. */
enum nouveau_engine_status nouveau_engine_capabilities(struct nouveau_engine *);
/* Pin a verified, uploaded 64KiB instance BEFORE writing 0x610010.
 * Requires successful capability preparation. Does not start channels,
 * enable IRQs or submit UPDATE. */
enum nouveau_engine_status nouveau_engine_bind(struct nouveau_engine *,struct nouveau_upload *);
/* Restore old instance/readback, then unpin and restore uploaded VRAM.
 * Keep firmware held and capabilities/IRQ snapshots intact for later restore.
 * Failure keeps the engine/upload quarantined and retryable. */
enum nouveau_engine_status nouveau_engine_unbind(struct nouveau_engine *);
/* Requires ALL channels inactive when driver resources/mirrors/IRQs changed.
 * Firmware-only failed handoff restores request even if its core stays busy.
 * Restore instance/readback before unpin,
 * restore uploaded VRAM, then saved interrupt masks and firmware request bit. On failure keep the
 * transaction quarantined. Upload stays pinned until old instance read-back;
 * after unpin, upload rollback state still prevents premature free. */
enum nouveau_engine_status nouveau_engine_restore(struct nouveau_engine *);
/* Native reversible handoff/capability probe. Does not bind an instance,
 * upload VRAM, start a channel, submit commands or change display timing. */
void nouveau_engine_probe_log(const struct pci_device_lease *,const struct pci_bar_resource *);
struct nouveau_display_snapshot;
struct nouveau_upload_probe_layout {
	const struct nouveau_display_snapshot *snapshot;
	uint64_t vram,scanout_sample,scanout_size;
	unsigned head;
	/* Experimental core start/stop is opt-in; native boot leaves this false. */
	bool test_core_channel;
};
void nouveau_engine_probe_upload_log(const struct pci_device_lease *,const struct pci_bar_resource *,
	const struct nouveau_upload_probe_layout *);
/* Start with a quiescent unbound engine; transiently bind pinned upload,
 * unbind/readback, then restore VRAM/window before returning firmware.
 * Irrecoverable cleanup halts. Never starts channels or submits methods. */
void nouveau_instance_upload_probe_log(const struct pci_device_lease *,const struct pci_bar_resource *,
	struct nouveau_engine *,const struct nouveau_upload_probe_layout *);
#endif
