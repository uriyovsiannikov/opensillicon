/* Host tests: no actual PCI config accesses or GPU writes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau.h"
#include "arch/x86_64/pci_resource.h"

static uint32_t boot_regs[2], strap_reg;
static unsigned claims, maps, claim_calls, map_calls, address_calls;
static unsigned fail_map, fail_address;
static bool fail_claim, fail_bar;
static uint64_t bar_size;
static enum pci_bar_type bar_type;
static unsigned bios_calls;
static unsigned ddc_calls;
static enum nouveau_bios_status bios_mock;
static struct pci_device_info gpu = {
	.vendor_id = 0x10de, .device_id = 0x1c82,
	.class_code = 3, .command = 2, .bars = {0xe0000000}
};

static void reset(void)
{
	assert(claims == 0 && maps == 0);
	claim_calls = map_calls = address_calls = 0;
	bios_calls = 0;
	ddc_calls = 0; bios_mock = NOUVEAU_BIOS_ROM_INVALID;
	fail_map = fail_address = 0;
	fail_claim = fail_bar = false;
	bar_size = 0x1000000;
	bar_type = PCI_BAR_MEMORY32;
	boot_regs[0] = 0x137000a1;
	boot_regs[1] = 0;
	strap_reg = 0x00400000;
}

bool pci_device_claim(const struct pci_device_info *d, struct pci_device_lease *l)
{
	assert(d == &gpu);
	claim_calls++;
	if (fail_claim) return false;
	assert(claims++ == 0);
	l->generation = 1;
	return true;
}

bool pci_device_release(struct pci_device_lease *l)
{
	assert(l->generation == 1 && maps == 0 && claims == 1);
	claims--;
	l->generation = 0;
	return true;
}

bool pci_bar_probe_size(const struct pci_device_lease *l, unsigned int index,
	struct pci_bar_resource *bar)
{
	assert(claims == 1 && l->generation == 1 && index == 0);
	if (fail_bar) return false;
	*bar = (struct pci_bar_resource){ .type = bar_type,
	    .base = 0xe0000000, .aperture_size = bar_size };
	return true;
}

bool pci_mmio_map_readonly(const struct pci_device_lease *d,
	const struct pci_bar_resource *bar, uint64_t off, size_t len,
	struct pci_mmio_lease *l)
{
	assert(claims == 1 && d->generation == 1 && bar->base == 0xe0000000);
	assert((off == 0 && len == 8) || (off == 0x101000 && len == 4));
	assert(off + len <= bar->aperture_size);
	map_calls++;
	if (map_calls == fail_map) return false;
	l->generation = 1;
	l->address = (uintptr_t)(off == 0 ? boot_regs : &strap_reg);
	maps++;
	return true;
}

bool pci_mmio_lease_address(const struct pci_mmio_lease *l, volatile void **p)
{
	assert(l->generation == 1);
	address_calls++;
	if (address_calls == fail_address) return false;
	*p = (volatile void *)(uintptr_t)l->address;
	return true;
}

bool pci_mmio_release(struct pci_mmio_lease *l)
{
	assert(l->generation == 1 && maps > 0);
	maps--;
	l->generation = 0;
	return true;
}

bool pci_enumerate(pci_device_callback cb, void *ctx) { return cb(&gpu, ctx); }
void klog(const char *s) { (void)s; }
void klog_u64(uint64_t v) { (void)v; }
_Noreturn void kernel_halt(void) { abort(); }

enum nouveau_bios_status nouveau_bios_probe(const struct pci_device_lease *c,
	const struct pci_bar_resource *b, struct nouveau_bios_report *r)
{
	assert(c->generation == 1 && claims == 1 && maps == 2);
	assert(b->aperture_size == bar_size);
	*r = (struct nouveau_bios_report){0};
	bios_calls++;
	return bios_mock; /* optional BIOS failure is non-fatal */
}

void nouveau_ddc_probe_log(const struct pci_device_lease *c,
	const struct pci_bar_resource *b, const struct nouveau_bios_report *r, bool display_enabled)
{
	assert(c->generation==1 && claims==1 && maps==2 && b->aperture_size==bar_size);
	(void)r; (void)display_enabled; ddc_calls++;
}

static struct nouveau_probe_result probe(enum nouveau_probe_status expected)
{
	struct nouveau_probe_result r;
	assert(nouveau_probe(&gpu, &r) == expected);
	assert(claims == 0 && maps == 0);
	return r;
}

int main(void)
{
	struct nouveau_identity id;
	assert(!nouveau_identify(0x137000a1, NULL));
	assert(nouveau_identify(0x137000a1, &id));
	assert(id.chipset == 0x137 && id.card_type == GP100 && id.chiprev == 0xa1);
	assert(strcmp(id.name, "GP107") == 0);
	assert(nouveau_crystal_khz(&id, 0) == 13500);
	assert(nouveau_crystal_khz(&id, 0x40) == 14318);
	assert(nouveau_crystal_khz(&id, 0x400000) == 27000);
	assert(nouveau_crystal_khz(&id, 0x400040) == 25000);
	assert(nouveau_identify(0x20004000, &id) && id.chipset == 4);
	assert(nouveau_identify(0x20104000, &id) && id.chipset == 5);
	assert(nouveau_identify(0x011000a1, &id) && id.card_type == NV_11);
	assert(id.chiprev == 0);
	assert(nouveau_crystal_khz(&id, 0x400040) == 14318);
	assert(nouveau_identify(0x010000a1, &id) && id.card_type == NV_10);
	assert(!nouveau_identify(0, &id) && id.name == NULL);
	assert(!nouveau_identify(UINT32_MAX, &id));
	assert(!nouveau_identify(0x170000a1, &id)); /* upstream opt-in GA100 */
	assert(!nouveau_identify(0x139000a1, &id)); /* hole inside Pascal family */
	assert(!nouveau_identify(0x150000a1, &id));
	assert(nouveau_crystal_khz(NULL, 0) == 0);
	assert(!nouveau_matches_device(NULL));
	reset();
	assert(nouveau_probe(&gpu, NULL) == NOUVEAU_PROBE_INVALID_ARGUMENT);
	assert(claim_calls == 0);
	struct nouveau_probe_result r;
	assert(nouveau_probe(NULL, &r) == NOUVEAU_PROBE_NOT_NVIDIA);
	gpu.vendor_id = 0x8086;
	probe(NOUVEAU_PROBE_NOT_NVIDIA);
	gpu.vendor_id = 0x10de;
	gpu.class_code = 4;
	probe(NOUVEAU_PROBE_NOT_NVIDIA);
	gpu.class_code = 3;
	gpu.header_type = 1;
	probe(NOUVEAU_PROBE_NOT_NVIDIA);
	gpu.header_type = 0;
	gpu.command = 0;
	probe(NOUVEAU_PROBE_MEMORY_DISABLED);
	assert(claim_calls == 0);
	gpu.command = 2;
	reset(); fail_claim = true; probe(NOUVEAU_PROBE_BUSY);
	reset(); fail_bar = true; probe(NOUVEAU_PROBE_BAR_INVALID);
	reset(); bar_size = 0x100000; probe(NOUVEAU_PROBE_BAR_INVALID);
	reset(); bar_type = PCI_BAR_IO; probe(NOUVEAU_PROBE_BAR_INVALID);
	for (unsigned i = 1; i <= 2; i++) {
		reset(); fail_map = i; probe(NOUVEAU_PROBE_MAP_FAILED);
		reset(); fail_address = i; probe(NOUVEAU_PROBE_MAP_FAILED);
	}
	reset(); boot_regs[1] = 0x01000001; probe(NOUVEAU_PROBE_ENDIAN_UNSUPPORTED);
	reset(); boot_regs[0] = UINT32_MAX; probe(NOUVEAU_PROBE_UNKNOWN_CHIP);
	reset(); boot_regs[0] = 0x139000a1; probe(NOUVEAU_PROBE_UNKNOWN_CHIP);
	reset(); boot_regs[0] = 0x167000a1; boot_regs[1] = 0x10000;
	probe(NOUVEAU_PROBE_VGPU_UNSUPPORTED);
	reset(); r = probe(NOUVEAU_PROBE_OK);
	assert(r.identity.chipset == 0x137 && r.crystal_khz == 27000);
	assert(r.bar0_size == 0x1000000 && r.strap == 0x400000);
	assert(r.bios_attempted && r.bios_status == NOUVEAU_BIOS_ROM_INVALID);
	assert(bios_calls == 1);
	assert(boot_regs[0] == 0x137000a1 && boot_regs[1] == 0);
	reset(); bar_type = PCI_BAR_MEMORY64; probe(NOUVEAU_PROBE_OK);
	reset(); nouveau_boot_probe(true,true,true); assert(claims == 0 && maps == 0 && bios_calls == 1);
	reset(); nouveau_boot_probe(false,false,false); assert(bios_calls == 0);
	reset(); bios_mock=NOUVEAU_BIOS_OK; nouveau_boot_probe(true,false,true);
	assert(bios_calls==1 && ddc_calls==0);
	reset(); bios_mock=NOUVEAU_BIOS_OK; nouveau_boot_probe(true,true,true);
	assert(bios_calls==1 && ddc_calls==1);
	reset(); nouveau_boot_probe(false,true,true); assert(bios_calls==0 && ddc_calls==0);
	puts("NOUVEAU_PASS: identification, straps, GP107 transport and failure cleanup");
	return 0;
}
