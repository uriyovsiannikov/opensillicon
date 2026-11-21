/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_EDID_H
#define KERNEL_NOUVEAU_EDID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/nouveau_bios.h"

#define NOUVEAU_EDID_MAX_BLOCKS 4U
#define NOUVEAU_EDID_MAX_MODES 32U
struct nouveau_edid_mode {
	uint16_t width, height;
	uint32_t refresh_millihz, clock_khz;
	bool interlaced, preferred;
	/* Exact progressive DTD timing; enumerated standard/CTA modes may lack it. */
	uint16_t hsync_start, hsync_end, htotal;
	uint16_t vsync_start, vsync_end, vtotal;
	bool detailed, hsync_positive, vsync_positive;
};
struct nouveau_edid {
	char manufacturer[4], name[14];
	uint16_t product;
	uint32_t serial;
	uint8_t version, revision, declared_extensions, bad_extensions;
	uint8_t blocks, mode_count;
	bool truncated, modes_truncated;
	struct nouveau_edid_mode modes[NOUVEAU_EDID_MAX_MODES];
};
bool nouveau_edid_parse(const uint8_t *data, size_t size, struct nouveau_edid *edid);

enum nouveau_ddc_status {
	NOUVEAU_DDC_OK, NOUVEAU_DDC_UNSUPPORTED, NOUVEAU_DDC_NO_SINK,
	NOUVEAU_DDC_BUSY, NOUVEAU_DDC_NACK, NOUVEAU_DDC_TIMEOUT,
	NOUVEAU_DDC_IO, NOUVEAU_DDC_RESTORE, NOUVEAU_DDC_EDID_INVALID
};
/* Registers use absolute BAR0 offsets. Delay failure cancels the probe.
 * Pure transport is injectable so tests exercise protocol and cleanup. */
struct nouveau_ddc_io {
	void *context;
	uint32_t (*read)(void *context, uint32_t reg);
	void (*write)(void *context, uint32_t reg, uint32_t value);
	bool (*delay_us)(void *context, unsigned int usec);
};
struct nouveau_ddc_result {
	enum nouveau_ddc_status status;
	uint16_t bytes;
	uint32_t last_control, last_status;
	bool extensions_unread, segment_reset_failed;
	uint8_t data[128 * NOUVEAU_EDID_MAX_BLOCKS];
};
void nouveau_ddc_read(const struct nouveau_ddc_io *io,
	const struct nouveau_i2c_entry *entry, bool aux, struct nouveau_ddc_result *result);
void nouveau_ddc_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, const struct nouveau_bios_report *bios,
	bool display_enabled);
#endif
