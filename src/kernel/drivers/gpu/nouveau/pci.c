/* OpenSillicon PCI transport for the initial NVKM identification port.
 * SPDX-License-Identifier: BSD-2-Clause */
#include "kernel/nouveau.h"
#include "arch/x86_64/pci_resource.h"
#include "kernel/log.h"
#include "kernel/nouveau_edid.h"

#define NV_PMC_BOOT_0 0x000000U
#define NV_PMC_BOOT_1 0x000004U
#define NV_PEXTDEV_BOOT_0 0x101000U

bool
nouveau_matches_device(const struct pci_device_info *device)
{
	return device != NULL && device->vendor_id == 0x10de &&
	    (device->header_type & 0x7fU) == 0 && device->class_code == 3 &&
	    (device->subclass == 0 || device->subclass == 2);
}

static enum nouveau_probe_status
probe_internal(const struct pci_device_info *device,
	struct nouveau_probe_result *result, bool bios_enabled, bool edid_enabled, bool display_enabled)
{
	struct pci_device_lease claim = {0};
	struct pci_mmio_lease boot_mapping = {0}, strap_mapping = {0};
	struct pci_bar_resource bar;
	volatile void *address;
	enum nouveau_probe_status status = NOUVEAU_PROBE_NOT_NVIDIA;

	if (result == NULL)
		return NOUVEAU_PROBE_INVALID_ARGUMENT;
	*result = (struct nouveau_probe_result){0};
	if (!nouveau_matches_device(device))
		return status;
	/* Powered-down/unassigned devices need a later power-management port. */
	if ((device->command & 2U) == 0)
		return NOUVEAU_PROBE_MEMORY_DISABLED;
	if (!pci_device_claim(device, &claim))
		return NOUVEAU_PROBE_BUSY;
	status = NOUVEAU_PROBE_BAR_INVALID;
	if (!pci_bar_probe_size(&claim, 0, &bar) ||
	    (bar.type != PCI_BAR_MEMORY32 && bar.type != PCI_BAR_MEMORY64) ||
	    bar.aperture_size < NV_PEXTDEV_BOOT_0 + sizeof(uint32_t))
		goto out;
	result->bar0_base = bar.base;
	result->bar0_size = bar.aperture_size;
	status = NOUVEAU_PROBE_MAP_FAILED;
	if (!pci_mmio_map_readonly(&claim, &bar, NV_PMC_BOOT_0, 8,
	    &boot_mapping) || !pci_mmio_lease_address(&boot_mapping, &address))
		goto out;
	volatile uint32_t *boot = address;
	result->boot1 = boot[NV_PMC_BOOT_1 / sizeof(uint32_t)];
	/* Unlike Linux's constructor, this stage never writes the endian switch. */
	status = NOUVEAU_PROBE_ENDIAN_UNSUPPORTED;
	if (result->boot1 == 0x01000001)
		goto out;
	status = NOUVEAU_PROBE_UNKNOWN_CHIP;
	if (!nouveau_identify(boot[0], &result->identity))
		goto out;
	status = NOUVEAU_PROBE_VGPU_UNSUPPORTED;
	if (result->identity.card_type >= TU100 && (result->boot1 & 0x30000))
		goto out;
	status = NOUVEAU_PROBE_MAP_FAILED;
	if (!pci_mmio_map_readonly(&claim, &bar, NV_PEXTDEV_BOOT_0, 4,
	    &strap_mapping) || !pci_mmio_lease_address(&strap_mapping, &address))
		goto out;
	result->strap = *(volatile uint32_t *)address;
	result->crystal_khz = nouveau_crystal_khz(&result->identity, result->strap);
	if (bios_enabled && result->identity.chipset == 0x137) {
		klog("nouveau: GP107 VBIOS capture (PRAMIN, then PROM)\n");
		result->bios_attempted = true;
		result->bios_status = nouveau_bios_probe(&claim, &bar, &result->bios);
		nouveau_bios_log(result->bios_status, &result->bios);
		if (edid_enabled && result->bios_status == NOUVEAU_BIOS_OK)
			nouveau_ddc_probe_log(&claim, &bar, &result->bios, display_enabled);
	}
	status = NOUVEAU_PROBE_OK;
out:
	/* Do not discard ownership if the resource manager cannot unmap a lease. */
	if ((strap_mapping.generation != 0 && !pci_mmio_release(&strap_mapping)) ||
	    (boot_mapping.generation != 0 && !pci_mmio_release(&boot_mapping)) ||
	    !pci_device_release(&claim)) {
		klog("nouveau: PCI resource cleanup failed\n");
		kernel_halt();
	}
	return status;
}

enum nouveau_probe_status
nouveau_probe(const struct pci_device_info *device,
	struct nouveau_probe_result *result)
{
	return probe_internal(device, result, true, true, true);
}

static void
log_hex(uint64_t value, unsigned int digits)
{
	static const char hex[] = "0123456789abcdef";
	char text[17];
	for (unsigned int i = 0; i < digits; i++)
		text[i] = hex[(value >> ((digits - i - 1) * 4)) & 15];
	text[digits] = '\0';
	klog(text);
}

static const char *
probe_status_name(enum nouveau_probe_status status)
{
	switch (status) {
	case NOUVEAU_PROBE_OK: return "identified";
	case NOUVEAU_PROBE_NOT_NVIDIA: return "not an NVIDIA display device";
	case NOUVEAU_PROBE_MEMORY_DISABLED: return "PCI memory decoding disabled";
	case NOUVEAU_PROBE_BUSY: return "PCI claim unavailable";
	case NOUVEAU_PROBE_BAR_INVALID: return "BAR0 sizing/validation failed";
	case NOUVEAU_PROBE_MAP_FAILED: return "BAR0 mapping failed";
	case NOUVEAU_PROBE_ENDIAN_UNSUPPORTED: return "big-endian GPU unsupported";
	case NOUVEAU_PROBE_UNKNOWN_CHIP: return "unknown or inaccessible chip";
	case NOUVEAU_PROBE_VGPU_UNSUPPORTED: return "vGPU unsupported";
	case NOUVEAU_PROBE_INVALID_ARGUMENT: return "invalid argument";
	}
	return "unknown status";
}

struct boot_probe_options { bool bios, edid, display; };
static bool
probe_device(const struct pci_device_info *device, void *context)
{
	const struct boot_probe_options *options = context;
	if (!nouveau_matches_device(device))
		return true;
	struct nouveau_probe_result result;
	enum nouveau_probe_status status = probe_internal(device, &result, options->bios, options->edid, options->display);
	klog("nouveau: ");
	log_hex(device->segment, 4); klog(":");
	log_hex(device->bus, 2); klog(":");
	log_hex(device->device, 2); klog(".");
	log_hex(device->function, 1); klog(" PCI 10de:");
	log_hex(device->device_id, 4); klog(" ");
	klog(probe_status_name(status));
	if (status == NOUVEAU_PROBE_OK) {
		klog(" "); klog(result.identity.name); klog(" NV");
		log_hex(result.identity.chipset, 3);
		klog(" BOOT0=0x"); log_hex(result.identity.boot0, 8);
		klog(" BAR0=0x"); log_hex(result.bar0_base, 16);
		klog(" size=0x"); log_hex(result.bar0_size, 16);
		klog(" crystal_khz="); klog_u64(result.crystal_khz);
		klog(result.identity.chipset == 0x137 ?
		    " [GP107 target; probe only]" : " [probe only]");
	}
	klog("\n");
	return true;
}

void
nouveau_boot_probe(bool bios_enabled, bool edid_enabled, bool display_enabled)
{
	struct boot_probe_options options = {bios_enabled, edid_enabled, display_enabled};
	klog("nouveau: NVKM identification probe (no modesetting)\n");
	if (!pci_enumerate(probe_device, &options))
		klog("nouveau: PCI enumeration failed\n");
}

static const char *bios_status_name(enum nouveau_bios_status status)
{
	switch (status) {
	case NOUVEAU_BIOS_OK: return "validated";
	case NOUVEAU_BIOS_UNSUPPORTED: return "unsupported source";
	case NOUVEAU_BIOS_MAP_FAILED: return "MMIO mapping failed";
	case NOUVEAU_BIOS_DISPLAY_DISABLED: return "display disabled by fuse";
	case NOUVEAU_BIOS_VRAM_UNAVAILABLE: return "VBIOS VRAM window unavailable";
	case NOUVEAU_BIOS_NO_MEMORY: return "snapshot allocation failed";
	case NOUVEAU_BIOS_WINDOW_FAILED: return "source control programming failed";
	case NOUVEAU_BIOS_ROM_INVALID: return "invalid/truncated ROM or image chain";
	case NOUVEAU_BIOS_BIT_INVALID: return "invalid BIT table";
	case NOUVEAU_BIOS_DCB_MISSING: return "DCB not found";
	case NOUVEAU_BIOS_DCB_INVALID: return "invalid/unsupported DCB table";
	case NOUVEAU_BIOS_CONNECTOR_INVALID: return "invalid connector table/reference";
	}
	return "unknown status";
}

static const char *connector_name(uint8_t type)
{
	switch (type) {
	case 0x00: return "VGA";
	case 0x30: return "DVI-I";
	case 0x31: return "DVI-D";
	case 0x40: case 0x41: return "LVDS";
	case 0x46: return "DP";
	case 0x47: return "eDP";
	case 0x48: return "mini-DP";
	case 0x60: case 0x61: case 0x63: return "HDMI";
	case 0x71: return "USB-C";
	default: return "unknown";
	}
}

void nouveau_bios_log(enum nouveau_bios_status status,
	const struct nouveau_bios_report *r)
{
	if (r->fallback_attempted) {
		klog("nouveau: VBIOS PRAMIN "); klog(bios_status_name(r->pramin_status));
		if (r->pramin_capture.header_bytes) {
			klog(" signature=0x");
			log_hex((uint16_t)r->pramin_capture.first_bytes[0] |
			    (uint16_t)r->pramin_capture.first_bytes[1] << 8, 4);
		}
		klog("; PROM fallback\n");
	}
	klog("nouveau: VBIOS ");
	klog(r->source == NOUVEAU_BIOS_SOURCE_PROM ? "PROM " : "PRAMIN ");
	klog(bios_status_name(status));
	if (r->acquisition_flags & 1) {
		klog(" fuse=0x"); log_hex(r->display_fuse, 8);
	}
	if (r->acquisition_flags & 2) {
		klog(" pointer=0x"); log_hex(r->vram_pointer, 8);
	}
	if (r->acquisition_flags & 4) {
		klog(" window=0x"); log_hex(r->window_before, 8);
		klog("->0x"); log_hex(r->window_target, 8);
	}
	if (r->source == NOUVEAU_BIOS_SOURCE_PROM) {
		klog(" shadow=0x"); log_hex(r->prom_control_before, 8);
		klog("->0x"); log_hex(r->prom_control_target, 8);
	}
	klog("\n");
	if (r->capture.header_bytes || r->capture.error) {
		const struct nouveau_bios_capture_info *c = &r->capture;
		static const char *const errors[] = {
		    "complete", "read-failed", "header-invalid", "image-too-large", "image-limit"
		};
		static const char *const reasons[] = {
		    "ok", "short-header", "ROM-signature", "PCIR-pointer",
		    "PCIR-signature", "PCIR-length", "NPDE-length", "image-length"
		};
		klog("nouveau: ROM capture="); klog(errors[c->error]);
		if (c->error == NOUVEAU_ROM_HEADER_INVALID) {
			klog(" reason="); klog(reasons[c->header_error]);
		}
		klog(" image="); klog_u64(c->image_index);
		klog(" offset=0x"); log_hex(c->offset, 6);
		klog(" valid_bytes="); klog_u64(c->valid_size);
		klog("\n");
		if (status != NOUVEAU_BIOS_OK || !c->complete) {
			klog("nouveau: ROM PCIR offset=0x"); log_hex(c->pcir_offset, 4);
			klog(" length=0x"); log_hex(c->pcir_length, 4);
			klog(" magic=0x"); log_hex(c->pcir_magic, 8);
			klog(" header_bytes="); klog_u64(c->header_bytes); klog("\n");
			klog("nouveau: ROM declared_bytes="); klog_u64(c->declared_size); klog("\n");
			for (size_t i = 0; i < sizeof(c->first_bytes); i++) {
				if (i % 16 == 0) {
					klog("nouveau: ROM header+"); log_hex(i, 2); klog(":");
				}
				klog(" "); log_hex(c->first_bytes[i], 2);
				if (i % 16 == 15) klog("\n");
			}
		}
	}
	if (r->rom_size) {
		klog("nouveau: ROM bytes="); klog_u64(r->rom_size);
		klog(" images="); klog_u64(r->image_count);
		klog(" chain="); klog(r->chain_complete ? "complete" : "partial (valid prefix only)");
		klog(" legacy_checksum="); klog(r->checksum_valid ? "ok" : "mismatch");
		klog(" BIT=0x"); log_hex(r->bit_offset, 5);
		klog(" BMP=0x"); log_hex(r->bmp_offset, 5);
		if (r->version_valid) {
			klog(" version=");
			for (size_t i = 0; i < 5; i++) {
				if (i) klog(".");
				log_hex(r->version[i], 2);
			}
		}
		klog("\n");
	}
	if (status != NOUVEAU_BIOS_OK)
		return;
	klog("nouveau: DCB v"); log_hex(r->dcb_version, 2);
	klog(" outputs="); klog_u64(r->output_count);
	klog(" connectors="); klog_u64(r->connector_count);
	klog(" (firmware descriptions; connection state not probed)\n");
	for (size_t i = 0; i < r->output_count; i++) {
		const struct nouveau_dcb_output *o = &r->outputs[i];
		klog("nouveau: DCB["); klog_u64(o->index); klog("] type=0x");
		log_hex(o->type, 1);
		klog(" connector="); klog_u64(o->connector);
		if (r->connector_offset) {
			const struct nouveau_dcb_connector *c = &r->connectors[o->connector];
			klog(" "); klog(connector_name(c->type));
			klog("(0x"); log_hex(c->type, 2); klog(") hpd=0x");
			log_hex(c->hpd, 2);
		}
		klog(" heads=0x"); log_hex(o->heads, 1);
		klog(" or=0x"); log_hex(o->or_mask, 1);
		klog(" i2c=0x"); log_hex(o->i2c, 1);
		klog(" link=0x"); log_hex(o->link, 1);
		if (o->type == 6) {
			klog(" dp_lanes="); klog_u64(o->dp_lanes);
			klog(" dp_bw_code=0x"); log_hex(o->dp_bandwidth, 2);
		}
		klog("\n");
	}
}
