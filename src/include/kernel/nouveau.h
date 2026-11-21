#ifndef KERNEL_NOUVEAU_H
#define KERNEL_NOUVEAU_H

#include <stdbool.h>
#include <stdint.h>
#include "arch/x86_64/pci.h"
#include "kernel/nouveau_bios.h"

/* NVKM architecture ordering is significant to the strap decoder. */
enum nouveau_card_type {
	NV_04 = 0x04, NV_10 = 0x10, NV_11 = 0x11, NV_20 = 0x20,
	NV_30 = 0x30, NV_40 = 0x40, NV_50 = 0x50, NV_C0 = 0xc0,
	NV_E0 = 0xe0, GM100 = 0x110, GP100 = 0x130, GV100 = 0x140,
	TU100 = 0x160, GA100 = 0x170, AD100 = 0x190
};

struct nouveau_identity {
	uint32_t boot0;
	uint16_t chipset;
	uint8_t chiprev;
	enum nouveau_card_type card_type;
	const char *name;
};

enum nouveau_probe_status {
	NOUVEAU_PROBE_INVALID_ARGUMENT,
	NOUVEAU_PROBE_OK,
	NOUVEAU_PROBE_NOT_NVIDIA,
	NOUVEAU_PROBE_MEMORY_DISABLED,
	NOUVEAU_PROBE_BUSY,
	NOUVEAU_PROBE_BAR_INVALID,
	NOUVEAU_PROBE_MAP_FAILED,
	NOUVEAU_PROBE_ENDIAN_UNSUPPORTED,
	NOUVEAU_PROBE_UNKNOWN_CHIP,
	NOUVEAU_PROBE_VGPU_UNSUPPORTED
};

struct nouveau_probe_result {
	struct nouveau_identity identity;
	uint64_t bar0_base;
	uint64_t bar0_size;
	uint32_t boot1;
	uint32_t strap;
	uint32_t crystal_khz;
	bool bios_attempted;
	enum nouveau_bios_status bios_status;
	struct nouveau_bios_report bios;
};

bool nouveau_identify(uint32_t boot0, struct nouveau_identity *identity);
uint32_t nouveau_crystal_khz(const struct nouveau_identity *identity,
	uint32_t strap);
bool nouveau_matches_device(const struct pci_device_info *device);
/* Synchronous, boot-only diagnostic. No persistent device ownership or DMA.
 * BAR sizing temporarily changes PCI config and verifies its restoration.
 * Result is initialized on every call. OK means chip identification succeeded;
 * optional GP107 VBIOS errors are separate in bios_status when bios_attempted. */
enum nouveau_probe_status nouveau_probe(const struct pci_device_info *device,
	struct nouveau_probe_result *result);
void nouveau_boot_probe(bool bios_enabled, bool edid_enabled, bool display_enabled);

#endif
