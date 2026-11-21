/* SPDX-License-Identifier: BSD-2-Clause
 * Inspect firmware instance memory without binding objects or writing VRAM. */
#include "kernel/nouveau_vram.h"
#include "kernel/heap.h"
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
static void context_log(const uint32_t *image,unsigned channel,uint32_t handle)
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
	if((obj[0]==9 || obj[0]==0x49) && !obj[3] && !obj[4] && !obj[5] && obj[1]<=obj[2]) {
		klog(" VRAM start=0x");hex((uint64_t)obj[1]<<8,10);
		klog(" limit=0x");hex(((uint64_t)obj[2]<<8)|255,10);
	} else klog(" unsupported DMA object");
	klog("\n");
}
void nouveau_firmware_instance_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,const struct nouveau_display_snapshot *s,uint64_t size)
{
	uint64_t base=(uint64_t)(s->instance&0xffffff00U)<<8;
	/* Display instance register format programmed by gf119_disp_init. A
	 * non-VRAM or differently aligned firmware layout must not be guessed. */
	if((s->instance&255)!=9 || !size || base>=size || NOUVEAU_INSTANCE_SIZE>size-base) {
		klog("nouveau: FWINST unknown/absent VRAM instance register=0x");hex(s->instance,8);klog("\n");return;
	}
	struct pci_mmio_lease leases[2]={{0}};
	struct pramin_mapping mapping={0};uint32_t *image=NULL;
	volatile void *address;
	if(!pci_mmio_map(claim,bar,0x1700,4,&leases[0]) ||
	    !pci_mmio_lease_address(&leases[0],&address)) goto out;
	mapping.control=address;
	if(!pci_mmio_map_readonly(claim,bar,0x700000,0x100000,&leases[1]) ||
	    !pci_mmio_lease_address(&leases[1],&address)) goto out;
	mapping.aperture=address;
	image=kheap_alloc(NOUVEAU_INSTANCE_SIZE);
	if(!image) goto out;
	struct nouveau_pramin_io io={.context=&mapping,.read32=read32,.write32=write32};
	enum nouveau_vram_status status=nouveau_vram_read(&io,size,base,image,NOUVEAU_INSTANCE_SIZE);
	if(status==NOUVEAU_VRAM_RESTORE_FAILED) {
		klog("nouveau: FWINST PRAMIN restore failed\n");kernel_halt();
	}
	if(status!=NOUVEAU_VRAM_OK) {
		klog("nouveau: FWINST capture failed\n");goto out;
	}
	klog("nouveau: FWINST captured base=0x");hex(base,10);
	klog(" bytes=65536; PRAMIN restored; header=");
	for(unsigned i=0;i<4;i++) {hex(image[i],8);klog(i==3?"\n":" ");}
	for(unsigned i=0;i<s->head_count && i<NOUVEAU_DISPLAY_HEADS;i++) {
		context_log(image,0,s->heads[i].arm.dma);
		for(unsigned j=0;j<2;j++) context_log(image,i+1,s->base[i].dma[j]);
	}
out:
	if(!image) klog("nouveau: FWINST mapping/allocation unavailable\n");
	/* Free only after nouveau_vram_read has restored the GPU window. */
	if(image && !kheap_free(image)) {klog("nouveau: FWINST snapshot release failed\n");kernel_halt();}
	for(unsigned i=2;i>0;i--) if(leases[i-1].generation && !pci_mmio_release(&leases[i-1])) {
		klog("nouveau: FWINST MMIO release failed\n");kernel_halt();
	}
}
