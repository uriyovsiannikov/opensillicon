/* SPDX-License-Identifier: BSD-2-Clause
 * Inspect firmware instance memory without binding objects or writing VRAM. */
#include "kernel/nouveau_vram.h"
#include "kernel/heap.h"
#include "kernel/framebuffer_device.h"
#include "kernel/log.h"
struct pramin_mapping { volatile uint32_t *control, *aperture; };
static bool read32(void *context,uint32_t reg,uint32_t *value)
{
	struct pramin_mapping *m=context;
	if(reg==0x1700) {*value=*m->control;return true;}
	if(reg<0x700000 || reg>=0x800000 || (reg&3)) return false;
	*value=m->aperture[(reg-0x700000)/4];return true;
}
static bool write32(void *context,uint32_t reg,uint32_t value)
{
	struct pramin_mapping *m=context;
	if(reg!=0x1700) return false;
	*m->control=value;return true;
}
static void hex(uint64_t value,unsigned digits)
{
	static const char chars[]="0123456789abcdef";char buffer[17];
	for(unsigned i=0;i<digits;i++) buffer[i]=chars[(value>>((digits-i-1)*4))&15];
	buffer[digits]=0;klog(buffer);
}
static void context_log(const uint32_t *image,unsigned channel,uint32_t handle,uint64_t size)
{
	if(!handle) return;
	uint32_t offset;
	klog("nouveau: FWCTX channel=");klog_u64(channel);klog(" handle=0x");hex(handle,8);
	if(!nouveau_ramht_lookup(image,channel,handle,&offset)) {
		klog(" not found in 4KiB RAMHT layout\n");return;
	}
	const uint32_t *obj=image+offset/4;
	klog(" object=0x");hex(offset,4);klog(" flags=0x");hex(obj[0],8);
	/* Only known pitch VRAM objects. Other formats are diagnostic data. */
	if((obj[0]==9 || obj[0]==0x49) && !obj[3] && !obj[4] && !obj[5] && obj[1]<=obj[2] &&
	    (((uint64_t)obj[2]<<8)|255)<size) {
		klog(" VRAM start=0x");hex((uint64_t)obj[1]<<8,10);
		klog(" limit=0x");hex(((uint64_t)obj[2]<<8)|255,10);
	} else klog(" unsupported DMA object");
	klog("\n");
}

/* Map/read/restore/unmap before allowing the caller to parse firmware data. */
static uint32_t *firmware_read(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,uint64_t size,uint64_t base,size_t length)
{
	struct pci_mmio_lease leases[2]={{0}};
	struct pramin_mapping mapping={0};uint32_t *image=NULL;
	bool captured=false;
	volatile void *address;
	if(!pci_mmio_map(claim,bar,0x1700,4,&leases[0]) ||
	    !pci_mmio_lease_address(&leases[0],&address)) goto out;
	mapping.control=address;
	if(!pci_mmio_map_readonly(claim,bar,0x700000,0x100000,&leases[1]) ||
	    !pci_mmio_lease_address(&leases[1],&address)) goto out;
	mapping.aperture=address;
	image=kheap_alloc(length);
	if(!image) goto out;
	struct nouveau_pramin_io io={.context=&mapping,.read32=read32,.write32=write32};
	enum nouveau_vram_status status=nouveau_vram_read(&io,size,base,image,length);
	if(status==NOUVEAU_VRAM_RESTORE_FAILED) {
		klog("nouveau: firmware PRAMIN restore failed\n");kernel_halt();
	}
	captured=status==NOUVEAU_VRAM_OK;
out:
	for(unsigned i=2;i>0;i--) if(leases[i-1].generation && !pci_mmio_release(&leases[i-1])) {
		klog("nouveau: firmware MMIO release failed\n");kernel_halt();
	}
	if(image && !captured) {
		if(!kheap_free(image)) {klog("nouveau: firmware snapshot release failed\n");kernel_halt();}
		image=NULL;
	}
	return image;
}
static void firmware_free(uint32_t *image)
{
	if(!kheap_free(image)) {klog("nouveau: firmware snapshot release failed\n");kernel_halt();}
}

void nouveau_firmware_instance_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,const struct nouveau_display_snapshot *s,uint64_t size)
{
	uint64_t base=(uint64_t)(s->instance&0xffffff00U)<<8;
	/* Display instance register format programmed by gf119_disp_init. */
	if((s->instance&255)!=9 || !size || base>=size || NOUVEAU_INSTANCE_SIZE>size-base) {
		klog("nouveau: FWINST unknown/absent VRAM instance register=0x");hex(s->instance,8);klog("\n");return;
	}
	uint32_t *image=firmware_read(claim,bar,size,base,NOUVEAU_INSTANCE_SIZE);
	if(!image) {klog("nouveau: FWINST capture unavailable\n");return;}
	klog("nouveau: FWINST captured base=0x");hex(base,10);
	klog(" bytes=65536; PRAMIN restored; header=");
	for(unsigned i=0;i<4;i++) {hex(image[i],8);klog(i==3?"\n":" ");}
	for(unsigned i=0;i<s->head_count && i<NOUVEAU_DISPLAY_HEADS;i++) {
		context_log(image,0,s->heads[i].arm.dma,size);
		for(unsigned j=0;j<2;j++) context_log(image,i+1,s->base[i].dma[j],size);
	}
	firmware_free(image);
}

void nouveau_firmware_bar_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,unsigned pci_index,uint32_t reg,uint64_t size)
{
	if(pci_index!=1 && pci_index!=3) return;
	klog("nouveau: BARVM PCI=");klog_u64(pci_index);klog(" context=0x");hex(reg,8);
	/* gf100_bar_bar1_init/bar2_init: enable bit 31, optional BAR2 halve
	 * bit 30, 40-bit VRAM instance address >>12. Do not guess reserved bits. */
	if(!(reg&0x80000000U)) {
		if(!(reg&0x3fffffffU)) klog(" physical mode (no VM instance)\n");
		else klog(" physical mode; unsupported target/pointer fields\n");
		return;
	}
	if((reg&0x30000000U) || (pci_index==1 && (reg&0x40000000U))) {
		klog(" unsupported virtual target/flags\n");return;
	}
	uint64_t base=(uint64_t)(reg&0x0fffffffU)<<12;
	if(!size || size>(UINT64_C(1)<<40) || base>=size || 4096>size-base) {
		klog(" VRAM range invalid\n");return;
	}
	uint32_t *image=firmware_read(claim,bar,size,base,4096);
	if(!image) {klog(" capture unavailable\n");return;}
	/* gf100_vmm_join_: instance +200 is the PDB descriptor, +208 is
	 * inclusive virtual address limit. gp100 adds VER2 and page flags.
	 * Report raw descriptors; do not assume the firmware selected VER2. */
	uint64_t pdb=(uint64_t)image[0x200/4]|((uint64_t)image[0x204/4]<<32);
	uint64_t limit=(uint64_t)image[0x208/4]|((uint64_t)image[0x20c/4]<<32);
	klog(" instance=0x");hex(base,10);klog(" pdb_raw=0x");hex(pdb,16);
	klog(" va_limit=0x");hex(limit,16);klog("; PRAMIN restored\n");
	firmware_free(image);
}

struct scanout_mapping { struct pramin_mapping mapping;uint64_t vram; };
static bool scan_cpu(void *context,uint64_t offset,uint32_t *words,size_t count)
{
	(void)context;return framebuffer_device_boot_read(offset,words,count);
}
static bool scan_gpu(void *context,uint64_t address,uint32_t *words,size_t count)
{
	struct scanout_mapping *m=context;
	struct nouveau_pramin_io io={.context=&m->mapping,.read32=read32,.write32=write32};
	enum nouveau_vram_status status=nouveau_vram_read(&io,m->vram,address,words,count*4);
	if(status==NOUVEAU_VRAM_RESTORE_FAILED) {
		klog("nouveau: FBMATCH PRAMIN restore failed\n");kernel_halt();
	}
	return status==NOUVEAU_VRAM_OK;
}
static const char *match_name(enum nouveau_scanout_match status)
{
	switch(status) {
	case NOUVEAU_SCANOUT_SAMPLE_MATCH:return "nonuniform samples match (not ownership)";
	case NOUVEAU_SCANOUT_MISMATCH:return "mismatch";
	case NOUVEAU_SCANOUT_FLAT:return "uniform samples; inconclusive";
	case NOUVEAU_SCANOUT_UNSTABLE:return "pixels changed; inconclusive";
	case NOUVEAU_SCANOUT_IO_FAILED:return "read failed";
	case NOUVEAU_SCANOUT_INVALID:return "range invalid";
	}
	return "unknown";
}
void nouveau_scanout_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,uint32_t context,uint64_t vram,uint64_t offset,uint64_t span)
{
	/* Only the observed physical-mode GP107 32MiB aperture, upper half.
	 * Candidates are hypotheses, not translations derived from BAR offset. */
	if(context!=0x40000000 || vram<0x2000000 || vram>(UINT64_C(1)<<40) ||
	    offset<0x1000000 || offset>=0x2000000 || !span || span>0x2000000-offset) {
		klog("nouveau: FBMATCH physical BAR3 layout unsupported\n");return;
	}
	struct pci_mmio_lease leases[2]={{0}};
	struct scanout_mapping m={.vram=vram};volatile void *address;
	if(!pci_mmio_map(claim,bar,0x1700,4,&leases[0]) ||
	    !pci_mmio_lease_address(&leases[0],&address)) goto out;
	m.mapping.control=address;
	if(!pci_mmio_map_readonly(claim,bar,0x700000,0x100000,&leases[1]) ||
	    !pci_mmio_lease_address(&leases[1],&address)) goto out;
	m.mapping.aperture=address;
	struct nouveau_scanout_reader io={.context=&m,.cpu=scan_cpu,.gpu=scan_gpu};
	uint64_t candidates[]={0,offset,vram-0x2000000+offset};
	unsigned matches=0;
	for(unsigned i=0;i<3;i++) {
		enum nouveau_scanout_match status=nouveau_scanout_compare(&io,vram,candidates[i],span);
		klog("nouveau: FBMATCH candidate VRAM=0x");hex(candidates[i],10);
		klog(" ");klog(match_name(status));klog("\n");
		if(status==NOUVEAU_SCANOUT_SAMPLE_MATCH) matches++;
	}
	klog("nouveau: FBMATCH sample_matches=");klog_u64(matches);
	klog("; no VRAM writes or reservation\n");
out:
	for(unsigned i=2;i>0;i--) if(leases[i-1].generation && !pci_mmio_release(&leases[i-1])) {
		klog("nouveau: FBMATCH MMIO release failed\n");kernel_halt();
	}
}
