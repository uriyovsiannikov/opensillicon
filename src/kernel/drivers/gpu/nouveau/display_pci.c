/* SPDX-License-Identifier: BSD-2-Clause
 * Read-only GP107 display-state capture and timing-block preparation. */
#include "kernel/nouveau_display.h"
#include "kernel/nouveau_memory.h"
#include "kernel/nouveau_vram.h"
#include "kernel/framebuffer_device.h"
#include "kernel/log.h"

struct display_region { uint32_t offset, size; };
static const struct display_region regions[] = {
	{0x001000,0x1000},{0x022000,0x1000},{0x100000,0x1000},{0x610000,0x2000},{0x612000,0x1000},
	{0x616000,0x2000},{0x619000,0x1000},{0x640000,0x5000},{0x660000,0x5000}
};
#define REGION_COUNT (sizeof(regions)/sizeof(regions[0]))
struct mapped_display {
	struct pci_mmio_lease leases[REGION_COUNT];
	volatile uint8_t *addresses[REGION_COUNT];
};
static bool read_register(void *context,uint32_t reg,uint32_t *value)
{
	struct mapped_display *map=context;
	if(reg&3) return false;
	for(size_t i=0;i<REGION_COUNT;i++) {
		if(reg>=regions[i].offset && reg-regions[i].offset<regions[i].size) {
			*value=*(volatile uint32_t *)(map->addresses[i]+reg-regions[i].offset);
			return true;
		}
	}
	return false;
}
static void hex(uint64_t v,unsigned digits)
{
	static const char chars[]="0123456789abcdef"; char text[17];
	for(unsigned i=0;i<digits;i++) text[i]=chars[(v>>((digits-i-1)*4))&15];
	text[digits]=0;klog(text);
}
static void bank_log(const char *label,const struct nouveau_head_bank *b)
{
	klog(label); klog(" raster=0x");hex(b->raster,8);
	klog(" sync=0x");hex(b->sync_end,8);
	klog(" blank_end=0x");hex(b->blank_end,8);
	klog(" blank_start=0x");hex(b->blank_start,8);
	klog(" clock=0x");hex(b->clock,8);klog("\n");
	if(b->timing_valid) {
		klog("nouveau: DISP current ");klog_u64(b->width);klog("x");klog_u64(b->height);
		klog(" refresh_millihz=");klog_u64(b->refresh_millihz);
		klog(" totals=");klog_u64(b->htotal);klog("x");klog_u64(b->vtotal);klog("\n");
	}
}
static const char *plan_status(enum nouveau_display_plan_status status)
{
	switch(status) {
	case NOUVEAU_PLAN_OK:return "timing block built (not submitted)";
	case NOUVEAU_PLAN_NO_TIMING:return "exact progressive DTD required";
	case NOUVEAU_PLAN_BAD_TIMING:return "invalid/unsupported timing";
	case NOUVEAU_PLAN_NO_ROUTE:return "active TMDS route not found";
	case NOUVEAU_PLAN_AMBIGUOUS_ROUTE:return "ambiguous route/owner";
	case NOUVEAU_PLAN_NO_HEAD:return "active head not found";
	case NOUVEAU_PLAN_UNSUPPORTED_OUTPUT:return "output requires additional link support";
	case NOUVEAU_PLAN_FRAMEBUFFER_MISMATCH:return "framebuffer handoff/resize required";
	}
	return "unknown";
}

struct bootfb_bar3 { bool found;uint64_t offset,size; };
static void boot_framebuffer_log(const struct pci_device_lease *claim,
	const struct os_framebuffer_info *fb,struct bootfb_bar3 *location)
{
	uint64_t physical,size,offset;
	if(!framebuffer_device_boot_physical(&physical,&size)) {
		klog("nouveau: BOOTFB physical metadata unavailable\n");return;
	}
	klog("nouveau: BOOTFB CPU physical=0x");hex(physical,16);
	klog(" bytes=");klog_u64(size);klog(" pitch=");klog_u64(fb->pitch);
	klog(" bpp=");klog_u64(fb->bits_per_pixel);klog("\n");
	/* GF100/GM107 BAR code uses PCI index 3 for the second GPU aperture
	 * (called BAR2 by NVKM). Indices 2 and 4 are high halves of 64-bit BARs. */
	const unsigned indices[]={1,3};
	bool matched=false;
	for(unsigned i=0;i<2;i++) {
		unsigned index=indices[i];struct pci_bar_resource resource;
		klog("nouveau: BOOTFB PCI BAR");klog_u64(index);
		if(!pci_bar_probe_size(claim,index,&resource)) {
			klog(" sizing unavailable\n");continue;
		}
		klog("=0x");hex(resource.base,16);
		klog(" aperture=");klog_u64(resource.aperture_size);
		if(nouveau_framebuffer_bar_offset(&resource,physical,size,&offset)) {
			matched=true;
			if(index==3 && resource.aperture_size==0x2000000)
				*location=(struct bootfb_bar3){.found=true,.offset=offset,.size=size};
			klog(" contains boot framebuffer offset=0x");hex(offset,16);
			klog(" (GPU MMU translation required)");
		} else klog(" boot framebuffer outside aperture");
		klog("\n");
	}
	if(!matched) klog("nouveau: BOOTFB not contained in GPU PCI BAR1/BAR3\n");
}

void nouveau_display_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,const struct nouveau_dcb_output *output,
	const struct nouveau_edid *edid)
{
	struct mapped_display map={0};
	struct nouveau_display_snapshot s;
	klog("nouveau: DISP GP107 state capture (read-only)\n");
	for(size_t i=0;i<REGION_COUNT;i++) {
		volatile void *address;
		if(!pci_mmio_map_readonly(claim,bar,regions[i].offset,regions[i].size,&map.leases[i]) ||
		    !pci_mmio_lease_address(&map.leases[i],&address)) {
			klog("nouveau: DISP mapping failed offset=0x");hex(regions[i].offset,6);klog("\n");
			goto out;
		}
		/* Use only the registry-validated virtual address. */
		map.addresses[i]=address;
	}
	if(!nouveau_display_snapshot(read_register,&map,&s)) {
		klog("nouveau: DISP invalid/inaccessible capabilities count=0x");hex(s.head_count,8);
		klog(" caps=0x");hex(s.capabilities,8);klog("\n");goto out;
	}
	klog("nouveau: DISP core=987d heads=0x");hex(s.head_mask,1);
	klog(" sors=0x");hex(s.sor_mask,2);klog(" instance=0x");hex(s.instance,8);
	klog(" core_ctrl=0x");hex(s.core_control,8);klog(" push=0x");hex(s.core_push,8);
	klog(" put=0x");hex(s.user_put,8);klog("\n");
	uint64_t vram=0;
	klog("nouveau: MEM capacity_reg=0x");hex(s.vidmem,8);
	if(nouveau_gp102_vram_size(s.vidmem,&vram)) {
		klog(" total_bytes=");klog_u64(vram);
	} else klog(" capacity unknown");
	klog("\n");
	klog("nouveau: BARMAP BAR1_CTX=0x");hex(s.bar1_instance,8);
	klog(" BAR3_CTX=0x");hex(s.bar3_instance,8);klog(" status=0x");hex(s.bar_status,8);klog("\n");
	klog("nouveau: DISP supervisor=0x");hex(s.supervisor,8);
	klog(" enable=0x");hex(s.supervisor_enable,8);klog(" control=0x");hex(s.supervisor_control,8);
	klog(" handoff=0x");hex(s.bios_handoff,8);klog(" intr=0x");hex(s.interrupt,8);klog("\n");
	for(unsigned i=0;i<4;i++) {
		klog("nouveau: DISP ROUTE OR");klog_u64(i);klog(" A=0x");hex(s.route[i][0],8);
		klog(" B=0x");hex(s.route[i][1],8);klog("\n");
	}
	for(unsigned i=0;i<8;i++) if(s.sor_mask&(1U<<i)) {
		klog("nouveau: DISP SOR");klog_u64(i);klog(" arm=0x");hex(s.sors[i].arm,8);
		klog(" asy=0x");hex(s.sors[i].asy,8);klog("\n");
	}
	for(unsigned i=0;i<s.head_count;i++) if(s.head_mask&(1U<<i)) {
		const struct nouveau_head_snapshot *h=&s.heads[i];
		klog("nouveau: DISP HEAD");klog_u64(i);
		if(!h->arm.timing_valid && !h->asy.timing_valid && !h->supervisor_mask) {
			klog(" inactive\n");continue;
		}
		klog(" scan=");klog_u64(h->scan_x);
		klog(",");klog_u64(h->scan_y);klog(" super=0x");hex(h->supervisor_mask,8);klog("\n");
		const struct nouveau_base_snapshot *b=&s.base[i];
		klog("nouveau: BASE head=");klog_u64(i);klog(" ctrl=0x");hex(b->control,8);
		klog(" push=0x");hex(b->push,8);klog(" present=0x");hex(b->present,8);klog("\n");
		for(unsigned j=0;j<2;j++) {
			const struct nouveau_base_surface *surf=&b->arm[j];
			klog("nouveau: BASE image=");klog_u64(j);klog(" dma=0x");hex(b->dma[j],8);
			klog(" offset=0x");hex(surf->offset_hi,8);hex(surf->offset_lo,8);
			klog(" size=0x");hex(surf->size,8);klog(" storage=0x");hex(surf->storage,8);
			klog(" params=0x");hex(surf->params,8);klog("\n");
		}
		bank_log("nouveau: DISP arm",&h->arm);bank_log("nouveau: DISP asy",&h->asy);
		klog("nouveau: DISP scanout offset=0x");hex(h->arm.image_offset,8);
		klog(" size=0x");hex(h->arm.image_size,8);klog(" storage=0x");hex(h->arm.storage,8);
		klog(" params=0x");hex(h->arm.params,8);klog(" dma=0x");hex(h->arm.dma,8);klog("\n");
	}
	struct os_framebuffer_info fb;
	if(!framebuffer_device_info(&fb)) { klog("nouveau: MODEPLAN framebuffer unavailable\n");goto out; }
	uint64_t sample_candidate=UINT64_MAX;
	struct bootfb_bar3 location={0};
	boot_framebuffer_log(claim,&fb,&location);
	if(location.found) nouveau_scanout_probe_log(claim,bar,s.bar3_instance,
	    vram,location.offset,location.size,&sample_candidate);
	nouveau_firmware_instance_log(claim,bar,&s,vram);
	nouveau_firmware_bar_log(claim,bar,1,s.bar1_instance,vram);
	nouveau_firmware_bar_log(claim,bar,3,s.bar3_instance,vram);
	const struct nouveau_edid_mode *mode=NULL;
	for(unsigned i=0;i<edid->mode_count;i++) if(edid->modes[i].detailed) {
		if(mode==NULL || edid->modes[i].preferred) mode=&edid->modes[i];
		if(edid->modes[i].preferred) break;
	}
	if(mode==NULL) { klog("nouveau: MODEPLAN exact progressive DTD required\n");goto out; }
	klog("nouveau: MODEPLAN DTD ");klog_u64(mode->width);klog("x");klog_u64(mode->height);
	klog(" clock_khz=");klog_u64(mode->clock_khz);klog(" h=");klog_u64(mode->hsync_start);
	klog(",");klog_u64(mode->hsync_end);klog(",");klog_u64(mode->htotal);
	klog(" v=");klog_u64(mode->vsync_start);klog(",");klog_u64(mode->vsync_end);
	klog(",");klog_u64(mode->vtotal);klog("\n");
	struct nouveau_display_plan p;
	enum nouveau_display_plan_status status=nouveau_display_plan(&s,output,mode,&fb,&p);
	klog("nouveau: MODEPLAN ");klog(plan_status(status));
	if(status==NOUVEAU_PLAN_OK) {
		klog(" head=");klog_u64(p.head);klog(" sor=");klog_u64(p.sor);
		klog(" link=");klog_u64(p.link);klog(" dwords=");klog_u64(p.count);
	}
	klog("\n");
	if(status==NOUVEAU_PLAN_OK && sample_candidate!=UINT64_MAX) {
		struct nouveau_surface_plan surface;
		if(nouveau_display_surface_plan(p.head,sample_candidate,vram,
		    NOUVEAU_SURFACE_HANDLE,&fb,&surface)) {
			klog("nouveau: SURFACEPLAN sample candidate VRAM=0x");hex(sample_candidate,10);
			klog(" words=9 pitch=");klog_u64(fb.pitch);
			klog(" context=0x");hex(NOUVEAU_SURFACE_HANDLE,8);
			klog(" (CPU only; ownership/binding pending)\n");
		}
	}
	if(status==NOUVEAU_PLAN_OK && vram) {
		struct nouveau_display_staging staging={0};
		/* Conservative planning exclusions. They do not establish that the
		 * remaining VRAM is free of firmware state. No upload follows. */
		struct nouveau_vram_pool pool={0};
		struct nouveau_vram_range exclusions[2]={{0,0x4000000},{0,0x1000000}};
		bool pool_ready=false;
		if(vram>0x5000000) {
			exclusions[1].start=vram-0x1000000;
			pool_ready=nouveau_vram_pool_init(&pool,vram,exclusions,2);
		}
		enum nouveau_staging_status result=nouveau_display_staging_create(vram,&p,&staging);
		if(result==NOUVEAU_STAGING_CLEANUP_FAILED) {
			klog("nouveau: MEM staging cleanup failed\n");kernel_halt();
		}
		if(result==NOUVEAU_STAGING_OK) {
			klog("nouveau: MEM CPU staging: instance=65536 RAMHT=4096 notifier=4096 push=4096 HOST=0x");
			hex(staging.push_descriptor,8);klog(" (not bound)\n");
			if(sample_candidate!=UINT64_MAX) {
				if(nouveau_display_staging_surface(&staging,sample_candidate,vram,&fb)) {
					klog("nouveau: MEM surface appended; staged_words=");
					klog_u64(staging.staged_words);klog(" (CPU owned, not submitted)\n");
				} else klog("nouveau: MEM surface append rejected; timing buffer retained\n");
			}
			if(pool_ready && nouveau_display_staging_place(&staging,&pool)) {
				struct nouveau_vram_range range;
				if(!nouveau_vram_pool_query(staging.placement,&range)) kernel_halt();
				klog("nouveau: VRAMPLAN logical instance=0x");hex(range.start,10);
				klog(" bytes=");klog_u64(range.size);klog(" register=0x");hex(staging.planned_instance,8);
				klog(" (CPU image relocated; no upload/ownership)\n");
			} else klog("nouveau: VRAMPLAN placement unavailable\n");
			if(!nouveau_display_staging_release(&staging)) {
				klog("nouveau: MEM staging release failed\n");kernel_halt();
			}
			klog("nouveau: MEM staging released; VRAM ownership pending\n");
		} else {
			klog("nouveau: MEM staging unavailable status=");klog_u64(result);klog("\n");
		}
	}
	klog("nouveau: MODEPLAN submission pending: owned VRAM/upload, scanout binding, supervisor/PLL\n");
out:
	for(size_t i=REGION_COUNT;i>0;i--) if(map.leases[i-1].generation && !pci_mmio_release(&map.leases[i-1])) {
		klog("nouveau: DISP MMIO cleanup failed\n");kernel_halt();
	}
}
