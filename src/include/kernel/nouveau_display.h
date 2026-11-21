/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_DISPLAY_H
#define KERNEL_NOUVEAU_DISPLAY_H
#include "kernel/nouveau_edid.h"
#include "abi/framebuffer.h"

#define NOUVEAU_DISPLAY_HEADS 4U
#define NOUVEAU_DISPLAY_SORS 8U
#define NOUVEAU_DISPLAY_PLAN_WORDS 24U
#define NOUVEAU_GP102_CORE_CLASS 0x987dU

struct nouveau_head_bank {
	uint32_t output, control, raster, sync_end, blank_end, blank_start, blank2;
	uint32_t clock, clock_config, image_offset, image_size, storage, params, dma;
	uint32_t viewport_in, viewport_out;
	uint16_t width, height, htotal, vtotal;
	uint32_t refresh_millihz;
	bool timing_valid;
};
struct nouveau_head_snapshot {
	struct nouveau_head_bank arm, asy;
	uint16_t scan_x, scan_y;
	uint32_t supervisor_mask;
};
struct nouveau_sor_snapshot { uint32_t arm, asy; };
struct nouveau_base_surface {
	uint32_t offset_lo, offset_hi, size, storage, params;
};
struct nouveau_base_snapshot {
	uint32_t control, push, present, dma[2];
	struct nouveau_base_surface arm[2], asy[2];
};
struct nouveau_display_snapshot {
	uint32_t capabilities, head_count, head_mask, sor_mask;
	uint32_t instance, core_control, core_push, core_limit, core_target, user_put;
	uint32_t interrupt, supervisor, supervisor_enable, supervisor_control, bios_handoff;
	uint32_t route[4][2];
	struct nouveau_head_snapshot heads[NOUVEAU_DISPLAY_HEADS];
	struct nouveau_sor_snapshot sors[NOUVEAU_DISPLAY_SORS];
	uint32_t vidmem;
	uint32_t bar1_instance, bar3_instance, bar_status;
	struct nouveau_base_snapshot base[NOUVEAU_DISPLAY_HEADS];
};

typedef bool (*nouveau_display_reader)(void *context, uint32_t reg, uint32_t *value);
/* Pure read-only NVKM state capture. No write callback is accepted. */
bool nouveau_display_snapshot(nouveau_display_reader read, void *context,
	struct nouveau_display_snapshot *snapshot);

enum nouveau_display_plan_status {
	NOUVEAU_PLAN_OK, NOUVEAU_PLAN_NO_TIMING, NOUVEAU_PLAN_BAD_TIMING,
	NOUVEAU_PLAN_NO_ROUTE, NOUVEAU_PLAN_AMBIGUOUS_ROUTE,
	NOUVEAU_PLAN_NO_HEAD, NOUVEAU_PLAN_UNSUPPORTED_OUTPUT,
	NOUVEAU_PLAN_FRAMEBUFFER_MISMATCH
};
struct nouveau_display_plan {
	uint32_t core_class, words[NOUVEAU_DISPLAY_PLAN_WORDS];
	uint8_t count, connector, head, sor, link;
	struct nouveau_edid_mode mode;
};
/* Build the timing/output/scaler portion only. This is not a submit-ready
 * transaction: core instance/RAMHT, surface context, notifier and supervisor
 * lifetime must be implemented before any UPDATE or channel activation. */
enum nouveau_display_plan_status nouveau_display_plan(
	const struct nouveau_display_snapshot *snapshot,
	const struct nouveau_dcb_output *output, const struct nouveau_edid_mode *mode,
	const struct os_framebuffer_info *framebuffer, struct nouveau_display_plan *plan);
void nouveau_display_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, const struct nouveau_dcb_output *output,
	const struct nouveau_edid *edid);
#define NOUVEAU_SURFACE_PLAN_WORDS 9U
struct nouveau_surface_plan { uint32_t words[NOUVEAU_SURFACE_PLAN_WORDS]; };
/* CPU-only pitch/RGB surface methods. Address is relative to a VRAM context
 * spanning [0,vram_size). This neither allocates VRAM nor grants ownership. */
bool nouveau_display_surface_plan(unsigned head, uint64_t address, uint64_t vram_size,
	uint32_t dma_handle, const struct os_framebuffer_info *fb,
	struct nouveau_surface_plan *plan);
#endif
