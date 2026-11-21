/* SPDX-License-Identifier: BSD-2-Clause */
#include "kernel/nouveau_edid.h"
#include "kernel/log.h"
#include "kernel/timer.h"
#include "kernel/nouveau_display.h"

struct native_io { volatile uint32_t *regs; };
static uint32_t read_reg(void *context, uint32_t reg)
{
	struct native_io *io = context;
	return io->regs[(reg - 0xd000) / 4];
}
static void write_reg(void *context, uint32_t reg, uint32_t value)
{
	struct native_io *io = context;
	io->regs[(reg - 0xd000) / 4] = value;
}
static bool delay_us(void *context, unsigned int us)
{
	(void)context;
	uint64_t start = timer_monotonic_ns();
	for (unsigned i = 0; i < 1000000; i++) {
		if (timer_monotonic_ns() - start >= (uint64_t)us * 1000) return true;
		__asm__ volatile("pause");
	}
	return false;
}
static bool precise_clock(void)
{
	uint64_t start = timer_monotonic_ns();
	for (unsigned i = 0; i < 4096; i++) {
		uint64_t delta = timer_monotonic_ns() - start;
		if (delta) return delta < 1000000;
		__asm__ volatile("pause");
	}
	return false;
}
static void hex(uint32_t v, unsigned digits)
{
	static const char chars[] = "0123456789abcdef";
	char text[9];
	for (unsigned i=0; i<digits; i++) text[i]=chars[(v>>((digits-i-1)*4))&15];
	text[digits]=0; klog(text);
}
static const char *status_name(enum nouveau_ddc_status s)
{
	switch (s) {
	case NOUVEAU_DDC_OK: return "read";
	case NOUVEAU_DDC_UNSUPPORTED: return "unsupported bus";
	case NOUVEAU_DDC_NO_SINK: return "AUX sink not detected";
	case NOUVEAU_DDC_BUSY: return "bus busy";
	case NOUVEAU_DDC_NACK: return "NACK/no EDID response";
	case NOUVEAU_DDC_TIMEOUT: return "timeout";
	case NOUVEAU_DDC_IO: return "transaction error";
	case NOUVEAU_DDC_RESTORE: return "register restoration failed";
	case NOUVEAU_DDC_EDID_INVALID: return "invalid EDID header/checksum/version";
	}
	return "unknown";
}

void nouveau_ddc_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, const struct nouveau_bios_report *bios,
	bool display_enabled)
{
	klog("nouveau: CCB v"); hex(bios->i2c_version,2);
	klog(" offset=0x"); hex(bios->i2c_offset,4);
	klog(" entries="); klog_u64(bios->i2c_count);
	klog(bios->i2c_valid ? " valid\n" : " missing/invalid; EDID skipped\n");
	if (!bios->i2c_valid) return;
	if (!precise_clock()) { klog("nouveau: EDID skipped: microsecond clock unavailable\n"); return; }
	struct pci_mmio_lease lease = {0};
	volatile void *address;
	if (!pci_mmio_map(claim,bar,0xd000,4096,&lease) ||
	    !pci_mmio_lease_address(&lease,&address)) {
		klog("nouveau: EDID MMIO mapping failed\n");
		goto out;
	}
	struct native_io native = {.regs = address};
	struct nouveau_ddc_io io = {&native,read_reg,write_reg,delay_us};
	uint16_t connected = 0;
	/* Try DP before the alternate TMDS entry sharing its connector. */
	for (unsigned pass=0; pass<2; pass++) {
		for (unsigned i=0; i<bios->output_count; i++) {
			const struct nouveau_dcb_output *o=&bios->outputs[i];
			bool aux = o->type == 6;
			if ((pass == 0) != aux || (o->type != 6 && o->type != 2 && o->type != 3 && o->type != 0) ||
			    o->location != 0 || o->i2c == 15 || o->i2c >= bios->i2c_count ||
			    (connected & (1U << o->connector))) continue;
			const struct nouveau_i2c_entry *bus=&bios->i2c[o->i2c];
			klog("nouveau: EDID connector="); klog_u64(o->connector);
			klog(aux ? " AUX" : " I2C");
			klog(" ccb="); klog_u64(o->i2c);
			klog(" drive="); hex(bus->drive,2); klog(" aux="); hex(bus->aux,2);
			klog(" share="); hex(bus->share,2); klog("\n");
			struct nouveau_ddc_result result;
			nouveau_ddc_read(&io,bus,aux,&result);
			klog("nouveau: EDID result="); klog(status_name(result.status));
			klog(" bytes="); klog_u64(result.bytes);
			klog(" ctrl=0x"); hex(result.last_control,8);
			klog(" status=0x"); hex(result.last_status,8); klog("\n");
			if (result.status == NOUVEAU_DDC_RESTORE) kernel_halt();
			if (result.status == NOUVEAU_DDC_EDID_INVALID) {
				klog("nouveau: EDID header:");
				for (unsigned j=0; j<8; j++) { klog(" "); hex(result.data[j],2); }
				klog(" checksum-byte=0x"); hex(result.data[127],2); klog("\n");
			}
			if (result.status != NOUVEAU_DDC_OK) continue;
			struct nouveau_edid edid;
			if (!nouveau_edid_parse(result.data,result.bytes,&edid)) continue;
			connected |= (uint16_t)(1U << o->connector);
			klog("nouveau: monitor "); klog(edid.manufacturer); klog(" ");
			klog(edid.name[0] ? edid.name : "(unnamed)");
			klog(" product=0x"); hex(edid.product,4);
			klog(" EDID "); klog_u64(edid.version); klog("."); klog_u64(edid.revision);
			klog(" modes="); klog_u64(edid.mode_count);
			if (edid.truncated || result.extensions_unread) klog(" extensions-partial");
			if (edid.bad_extensions) klog(" invalid-extension");
			if (result.segment_reset_failed) klog(" segment-reset-failed");
			if (edid.modes_truncated) klog(" mode-list-capped");
			klog("\n");
			for (unsigned j=0; j<edid.mode_count; j++) {
				const struct nouveau_edid_mode *m=&edid.modes[j];
				klog("nouveau: mode "); klog_u64(m->width); klog("x"); klog_u64(m->height);
				klog(m->interlaced ? "i " : "p ");
				klog_u64(m->refresh_millihz / 1000); klog(".");
				char fraction[4] = {(char)('0'+m->refresh_millihz/100%10),
				    (char)('0'+m->refresh_millihz/10%10),(char)('0'+m->refresh_millihz%10),0};
				klog(fraction); klog(" Hz");
				if (m->preferred) klog(" preferred");
				klog("\n");
			}
			if (display_enabled) nouveau_display_probe_bios_log(claim,bar,o,&edid,bios);
		}
	}
out:
	if (lease.generation && !pci_mmio_release(&lease)) {
		klog("nouveau: EDID MMIO cleanup failed\n"); kernel_halt();
	}
}
