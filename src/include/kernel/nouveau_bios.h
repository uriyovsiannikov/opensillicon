/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_BIOS_H
#define KERNEL_NOUVEAU_BIOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arch/x86_64/pci_resource.h"
#include "kernel/nouveau_pll.h"

#define NOUVEAU_BIOS_LIMIT 0x100000U
#define NOUVEAU_BIOS_MAX_IMAGES 64U
#define NOUVEAU_DCB_MAX_OUTPUTS 32U
#define NOUVEAU_DCB_MAX_CONNECTORS 32U
#define NOUVEAU_CCB_MAX_ENTRIES 32U

enum nouveau_bios_status {
	NOUVEAU_BIOS_OK,
	NOUVEAU_BIOS_UNSUPPORTED,
	NOUVEAU_BIOS_MAP_FAILED,
	NOUVEAU_BIOS_DISPLAY_DISABLED,
	NOUVEAU_BIOS_VRAM_UNAVAILABLE,
	NOUVEAU_BIOS_NO_MEMORY,
	NOUVEAU_BIOS_WINDOW_FAILED,
	NOUVEAU_BIOS_ROM_INVALID,
	NOUVEAU_BIOS_BIT_INVALID,
	NOUVEAU_BIOS_DCB_MISSING,
	NOUVEAU_BIOS_DCB_INVALID,
	NOUVEAU_BIOS_CONNECTOR_INVALID
};

struct nouveau_bios_image {
	uint32_t size;
	uint16_t vendor, device;
	uint8_t type;
	bool last;
};

enum nouveau_bios_capture_error {
	NOUVEAU_ROM_CAPTURE_OK,
	NOUVEAU_ROM_READ_FAILED,
	NOUVEAU_ROM_HEADER_INVALID,
	NOUVEAU_ROM_IMAGE_TOO_LARGE,
	NOUVEAU_ROM_IMAGE_LIMIT
};

enum nouveau_rom_header_error {
	NOUVEAU_ROM_HEADER_OK,
	NOUVEAU_ROM_HEADER_SHORT,
	NOUVEAU_ROM_SIGNATURE,
	NOUVEAU_ROM_PCIR_POINTER,
	NOUVEAU_ROM_PCIR_SIGNATURE,
	NOUVEAU_ROM_PCIR_LENGTH,
	NOUVEAU_ROM_NPDE_LENGTH,
	NOUVEAU_ROM_IMAGE_LENGTH
};

struct nouveau_bios_capture_info {
	enum nouveau_bios_capture_error error;
	enum nouveau_rom_header_error header_error;
	uint32_t offset, valid_size, header_bytes;
	uint32_t declared_size;
	uint16_t pcir_offset, pcir_length;
	uint32_t pcir_magic;
	uint8_t image_index, first_bytes[32];
	bool complete;
};

struct nouveau_dcb_output {
	uint32_t connection, configuration;
	uint8_t index, type, i2c, heads, connector, bus, location, or_mask;
	uint8_t link, external_device, dp_lanes, dp_bandwidth;
};

struct nouveau_dcb_connector {
	uint8_t type, location, hpd, dp, di, sr, lcdid;
};

struct nouveau_i2c_entry {
	uint32_t raw;
	uint8_t type, drive, aux, share;
};

enum nouveau_bios_source {
	NOUVEAU_BIOS_SOURCE_NONE,
	NOUVEAU_BIOS_SOURCE_PRAMIN,
	NOUVEAU_BIOS_SOURCE_PROM
};

enum nouveau_bios_pll_status {
	NOUVEAU_BIOS_PLL_ABSENT, NOUVEAU_BIOS_PLL_OK,
	NOUVEAU_BIOS_PLL_UNSUPPORTED, NOUVEAU_BIOS_PLL_INVALID
};
enum nouveau_bios_pll_error {
	NOUVEAU_PLL_ERROR_NONE, NOUVEAU_PLL_ERROR_DUPLICATE_C,
	NOUVEAU_PLL_ERROR_BIT_PAYLOAD, NOUVEAU_PLL_ERROR_TABLE_HEADER,
	NOUVEAU_PLL_ERROR_TABLE_SPAN, NOUVEAU_PLL_ERROR_DUPLICATE_TYPE,
	NOUVEAU_PLL_ERROR_LIMITS_POINTER, NOUVEAU_PLL_ERROR_REGISTER,
	NOUVEAU_PLL_ERROR_LIMITS
};
struct nouveau_bios_vpll {
	uint32_t reg;
	struct nouveau_pll_limits limits;
	bool valid;
};
struct nouveau_bios_report {
	uint32_t rom_size, image0_size, imaged_addr, bit_offset, bmp_offset;
	uint32_t pll_table_offset,crystal_khz;
	uint8_t pll_version;
	enum nouveau_bios_pll_status pll_status;
	enum nouveau_bios_pll_error pll_error;
	uint32_t pll_record_offset;
	uint8_t pll_header[4],pll_record[11],pll_limits_bytes[14];
	struct nouveau_bios_vpll vpll[4];
	uint16_t vendor, device, dcb_offset, connector_offset;
	uint8_t image_count, dcb_version, dcb_entries, output_count;
	uint8_t connector_version, connector_count, version[5];
	bool checksum_valid, version_valid, chain_complete;
	struct nouveau_bios_capture_info capture;
	enum nouveau_bios_source source;
	bool fallback_attempted;
	enum nouveau_bios_status pramin_status;
	struct nouveau_bios_capture_info pramin_capture;
	uint32_t prom_control_before, prom_control_target;
	/* Acquisition diagnostics; pure parsers leave these zero. */
	uint32_t display_fuse, vram_pointer, window_before, window_target;
	uint8_t acquisition_flags;
	uint16_t i2c_offset;
	uint8_t i2c_version, i2c_count;
	bool i2c_valid;
	struct nouveau_i2c_entry i2c[NOUVEAU_CCB_MAX_ENTRIES];
	struct nouveau_dcb_output outputs[NOUVEAU_DCB_MAX_OUTPUTS];
	struct nouveau_dcb_connector connectors[NOUVEAU_DCB_MAX_CONNECTORS];
};

typedef bool (*nouveau_bios_reader)(void *context, uint32_t offset,
	uint8_t *data, size_t length);

/* Pure parsers. Header parsing needs the PCIR/NPDE header, not the full image. */
bool nouveau_bios_image_header(const uint8_t *data, size_t size,
	struct nouveau_bios_image *image);
bool nouveau_bios_capture(nouveau_bios_reader reader, void *context,
	uint8_t *data, size_t capacity, size_t *size);
/* A complete, bounded prefix can be useful when VRAM lacks later ROM images.
 * success means at least one full image; info.complete reports the last flag. */
bool nouveau_bios_capture_ex(nouveau_bios_reader reader, void *context,
	uint8_t *data, size_t capacity, size_t *size,
	struct nouveau_bios_capture_info *info);
enum nouveau_bios_status nouveau_bios_parse(const uint8_t *data, size_t size,
	struct nouveau_bios_report *report);

/* Boot-only GP107 sources; caller owns the sized BAR0 claim. */
enum nouveau_bios_status nouveau_bios_pramin_probe(
	const struct pci_device_lease *claim, const struct pci_bar_resource *bar,
	struct nouveau_bios_report *report);
enum nouveau_bios_status nouveau_bios_prom_probe(
	const struct pci_device_lease *claim, const struct pci_bar_resource *bar,
	struct nouveau_bios_report *report);
enum nouveau_bios_status nouveau_bios_probe(
	const struct pci_device_lease *claim, const struct pci_bar_resource *bar,
	struct nouveau_bios_report *report);
void nouveau_bios_log(enum nouveau_bios_status status,
	const struct nouveau_bios_report *report);

#endif
