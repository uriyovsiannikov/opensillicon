/*
 * Copyright 2012, 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* PRAMIN/PROM acquisition adapted from nvkm/subdev/bios/shadow{ramin,rom}.c,
 * nvkm/subdev/pci/{base,nv40,gp100}.c, Linux v6.12.
 * Copyright 2012 Red Hat Inc. SPDX-License-Identifier: MIT */
#include "kernel/nouveau_bios.h"
#include "kernel/heap.h"
#include "kernel/log.h"

struct bios_mmio_reader {
	volatile uint32_t *window;
};

static bool
read_mmio_rom(void *context, uint32_t offset, uint8_t *data, size_t length)
{
	struct bios_mmio_reader *reader = context;
	if (offset > NOUVEAU_BIOS_LIMIT || length > NOUVEAU_BIOS_LIMIT - offset ||
	    (offset & 3) || (length & 3))
		return false;
	for (size_t i = 0; i < length; i += 4) {
		uint32_t value = reader->window[(offset + i) / 4];
		data[i] = value; data[i + 1] = value >> 8;
		data[i + 2] = value >> 16; data[i + 3] = value >> 24;
	}
	return true;
}

static bool
map(const struct pci_device_lease *claim, const struct pci_bar_resource *bar,
	uint32_t offset, size_t size, bool writable, struct pci_mmio_lease *lease,
	volatile uint32_t **pointer)
{
	volatile void *address;
	bool ok = writable ? pci_mmio_map(claim, bar, offset, size, lease) :
	    pci_mmio_map_readonly(claim, bar, offset, size, lease);
	if (!ok || !pci_mmio_lease_address(lease, &address))
		return false;
	*pointer = address;
	return true;
}

enum nouveau_bios_status
nouveau_bios_pramin_probe(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, struct nouveau_bios_report *report)
{
	struct pci_mmio_lease leases[4] = {{0}};
	volatile uint32_t *fuse = NULL, *bios_pointer = NULL, *control = NULL;
	struct bios_mmio_reader reader = {0};
	uint8_t *data = NULL;
	uint32_t saved_window = 0;
	uint32_t fuse_value = 0, pointer = 0, target_window = 0;
	uint8_t acquisition_flags = 0;
	bool changed = false;
	struct nouveau_bios_capture_info capture = {0};
	enum nouveau_bios_status status = NOUVEAU_BIOS_MAP_FAILED;
	if (report == NULL)
		return NOUVEAU_BIOS_ROM_INVALID;
	*report = (struct nouveau_bios_report){0};
	if (claim == NULL || bar == NULL || bar->aperture_size < 0x800000)
		return status;
	if (!map(claim, bar, 0x021c04, 4, false, &leases[0], &fuse))
		goto out;
	status = NOUVEAU_BIOS_DISPLAY_DISABLED;
	fuse_value = *fuse;
	acquisition_flags |= 1;
	if (fuse_value & 1)
		goto out;
	status = NOUVEAU_BIOS_MAP_FAILED;
	if (!map(claim, bar, 0x619f04, 4, false, &leases[1], &bios_pointer))
		goto out;
	pointer = *bios_pointer;
	acquisition_flags |= 2;
	status = NOUVEAU_BIOS_VRAM_UNAVAILABLE;
	if (!(pointer & 8) || (pointer & 3) != 1)
		goto out;
	status = NOUVEAU_BIOS_MAP_FAILED;
	if (!map(claim, bar, 0x001700, 4, true, &leases[2], &control) ||
	    !map(claim, bar, 0x700000, NOUVEAU_BIOS_LIMIT, false,
	    &leases[3], &reader.window))
		goto out;
	status = NOUVEAU_BIOS_NO_MEMORY;
	data = kheap_alloc(NOUVEAU_BIOS_LIMIT);
	if (data == NULL)
		goto out;
	saved_window = *control;
	acquisition_flags |= 4;
	uint64_t address = (uint64_t)(pointer & 0xffffff00U) << 8;
	if (address == 0)
		address = ((uint64_t)saved_window << 16) + 0xf0000;
	uint64_t target = address >> 16;
	status = NOUVEAU_BIOS_WINDOW_FAILED;
	if (target > UINT32_MAX)
		goto out;
	target_window = (uint32_t)target;
	/* The only GPU register written in this stage. Restore on every exit,
	 * including failed read-back and malformed ROM. Read-back flushes the PCI
	 * posted write before touching the PRAMIN aperture. */
	changed = true;
	*control = target_window;
	if (*control != target)
		goto out;
	size_t size;
	status = NOUVEAU_BIOS_ROM_INVALID;
	bool captured = nouveau_bios_capture_ex(read_mmio_rom, &reader, data,
	    NOUVEAU_BIOS_LIMIT, &size, &capture);
	/* Parsing does not need the window. Minimize time with a changed register. */
	*control = saved_window;
	if (*control != saved_window) {
		klog("nouveau: PRAMIN window restoration failed\n");
		kernel_halt();
	}
	changed = false;
	if (captured)
		status = nouveau_bios_parse(data, size, report);
out:
	if (changed) {
		*control = saved_window;
		if (*control != saved_window) {
			klog("nouveau: PRAMIN window restoration failed\n");
			kernel_halt();
		}
	}
	for (size_t i = 4; i > 0; i--) {
		if (leases[i - 1].generation && !pci_mmio_release(&leases[i - 1])) {
			klog("nouveau: VBIOS MMIO cleanup failed\n");
			kernel_halt();
		}
	}
	if (data != NULL && !kheap_free(data)) {
		klog("nouveau: VBIOS snapshot cleanup failed\n");
		kernel_halt();
	}
	report->display_fuse = fuse_value;
	report->vram_pointer = pointer;
	report->window_before = saved_window;
	report->window_target = target_window;
	report->acquisition_flags = acquisition_flags;
	report->capture = capture;
	report->source = NOUVEAU_BIOS_SOURCE_PRAMIN;
	return status;
}

enum nouveau_bios_status
nouveau_bios_prom_probe(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, struct nouveau_bios_report *report)
{
	struct pci_mmio_lease control_lease = {0}, rom_lease = {0};
	volatile uint32_t *control = NULL;
	struct bios_mmio_reader reader = {0};
	struct nouveau_bios_capture_info capture = {0};
	uint8_t *data = NULL;
	uint32_t saved = 0, target = 0;
	bool changed = false;
	enum nouveau_bios_status status = NOUVEAU_BIOS_MAP_FAILED;
	if (report == NULL)
		return NOUVEAU_BIOS_ROM_INVALID;
	*report = (struct nouveau_bios_report){0};
	if (claim == NULL || bar == NULL || bar->aperture_size < 0x400000)
		return status;
	/* gp100_pci uses nv40_pci_rd32/wr32: register 0x50 is at BAR0+0x88050.
	 * Do not enable the PCI expansion-ROM BAR or touch flash-programming regs. */
	if (!map(claim, bar, 0x088050, 4, true, &control_lease, &control) ||
	    !map(claim, bar, 0x300000, NOUVEAU_BIOS_LIMIT, false, &rom_lease,
	    &reader.window))
		goto out;
	status = NOUVEAU_BIOS_NO_MEMORY;
	data = kheap_alloc(NOUVEAU_BIOS_LIMIT);
	if (data == NULL)
		goto out;
	saved = *control;
	status = NOUVEAU_BIOS_WINDOW_FAILED;
	if (saved == UINT32_MAX)
		goto out;
	target = saved & ~1U;
	changed = true;
	*control = target;
	if (*control != target)
		goto out;
	size_t size;
	status = NOUVEAU_BIOS_ROM_INVALID;
	bool captured = nouveau_bios_capture_ex(read_mmio_rom, &reader, data,
	    NOUVEAU_BIOS_LIMIT, &size, &capture);
	/* Restore the exact original value. Upstream forces bit 0 on at fini;
	 * preserving firmware's initial state is appropriate for this boot probe. */
	*control = saved;
	if (*control != saved) {
		klog("nouveau: PROM shadow-control restoration failed\n");
		kernel_halt();
	}
	changed = false;
	if (captured)
		status = nouveau_bios_parse(data, size, report);
out:
	if (changed) {
		*control = saved;
		if (*control != saved) {
			klog("nouveau: PROM shadow-control restoration failed\n");
			kernel_halt();
		}
	}
	if ((rom_lease.generation && !pci_mmio_release(&rom_lease)) ||
	    (control_lease.generation && !pci_mmio_release(&control_lease))) {
		klog("nouveau: PROM MMIO cleanup failed\n");
		kernel_halt();
	}
	if (data != NULL && !kheap_free(data)) {
		klog("nouveau: PROM snapshot cleanup failed\n");
		kernel_halt();
	}
	report->source = NOUVEAU_BIOS_SOURCE_PROM;
	report->capture = capture;
	report->prom_control_before = saved;
	report->prom_control_target = target;
	return status;
}

enum nouveau_bios_status
nouveau_bios_probe(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, struct nouveau_bios_report *report)
{
	enum nouveau_bios_status status = nouveau_bios_pramin_probe(claim, bar, report);
	if (report == NULL || claim == NULL || bar == NULL ||
	    status == NOUVEAU_BIOS_OK || status == NOUVEAU_BIOS_NO_MEMORY)
		return status;
	struct nouveau_bios_capture_info prior_capture = report->capture;
	uint32_t fuse = report->display_fuse, pointer = report->vram_pointer;
	uint32_t before = report->window_before, target = report->window_target;
	uint8_t flags = report->acquisition_flags;
	klog("nouveau: PRAMIN unusable; trying PROM\n");
	enum nouveau_bios_status prom_status = nouveau_bios_prom_probe(claim, bar, report);
	report->fallback_attempted = true;
	report->pramin_status = status;
	report->pramin_capture = prior_capture;
	report->display_fuse = fuse; report->vram_pointer = pointer;
	report->window_before = before; report->window_target = target;
	report->acquisition_flags = flags;
	return prom_status;
}
