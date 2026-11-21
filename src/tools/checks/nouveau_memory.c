/* Known NVKM encodings and native allocation rollback. No GPU required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau_memory.h"
static uint32_t image[NOUVEAU_INSTANCE_SIZE/4];
static struct dma_buffer_info buffers[2];
static unsigned allocations,queries,fail_alloc,fail_query,corrupt_query,fail_free;
static bool available=true;
bool dma_identity_addressing_available(void){return available;}
bool dma_alloc(size_t size,size_t alignment,unsigned bits,struct dma_buffer *b)
{
	assert(bits==40);allocations++;
	if(allocations==fail_alloc)return false;
	for(unsigned i=0;i<2;i++)if(!buffers[i].cpu_virtual_address){
		buffers[i]=(struct dma_buffer_info){.cpu_virtual_address=aligned_alloc(alignment,size),
		    .physical_address=0x200000+i*0x10000,.device_address=0x200000+i*0x10000,
		    .requested_size=size,.allocated_size=size,.owner=DMA_OWNER_CPU};
		assert(buffers[i].cpu_virtual_address);*b=(struct dma_buffer){.slot=i,.generation=1};return true;
	}
	abort();
}
bool dma_buffer_query(struct dma_buffer b,struct dma_buffer_info *info)
{
	assert(b.generation==1&&b.slot<2);queries++;
	if(queries==fail_query)return false;
	*info=buffers[b.slot];
	if(corrupt_query==1)info->device_address|=1;
	if(corrupt_query==2)info->device_address=UINT64_C(1)<<40;
	if(corrupt_query==3)info->owner=DMA_OWNER_DEVICE;
	if(corrupt_query==4)info->allocated_size=0;
	if(corrupt_query==5)info->cpu_virtual_address=NULL;
	return true;
}
bool dma_free(struct dma_buffer b)
{
	assert(b.generation==1&&b.slot<2&&buffers[b.slot].cpu_virtual_address);
	if(fail_free==(unsigned)b.slot+1)return false;
	free(buffers[b.slot].cpu_virtual_address);memset(&buffers[b.slot],0,sizeof(buffers[0]));return true;
}
static void reset(void)
{
	assert(!buffers[0].cpu_virtual_address&&!buffers[1].cpu_virtual_address);
	allocations=queries=fail_alloc=fail_query=corrupt_query=fail_free=0;available=true;
}
int main(void)
{
	uint64_t vram=0;uint32_t words[6],off=0,reg=0;
	struct pci_bar_resource bar={.type=PCI_BAR_MEMORY64,.base=0xb0000000,.aperture_size=0x10000000};
	uint64_t offset=0;
	assert(nouveau_framebuffer_bar_offset(&bar,0xb0100000,0x100000,&offset)&&offset==0x100000);
	assert(nouveau_framebuffer_bar_offset(&bar,0xbfffff00,256,&offset));
	assert(!nouveau_framebuffer_bar_offset(&bar,0xbfffff00,257,&offset));
	assert(!nouveau_framebuffer_bar_offset(&bar,0xc0000000,1,&offset));
	assert(!nouveau_framebuffer_bar_offset(&bar,0xafffffff,1,&offset));
	assert(!nouveau_framebuffer_bar_offset(&bar,0xb0000000,0,&offset));
	bar.aperture_size=0;assert(!nouveau_framebuffer_bar_offset(&bar,0xb0000000,1,&offset));
	bar.base=UINT64_MAX-15;bar.aperture_size=32;
	assert(!nouveau_framebuffer_bar_offset(&bar,bar.base,1,&offset));
	assert(nouveau_gp102_vram_size(0x108,&vram)&&vram==UINT64_C(0x100000000));
	assert(nouveau_gp102_vram_size(0x40000108,&vram)&&vram==UINT64_C(0xf0000000));
	assert(!nouveau_gp102_vram_size(0,&vram)&&!vram);
	assert(!nouveau_gp102_vram_size(UINT32_MAX,&vram));
	assert(nouveau_gp102_vram_size(0x108,&vram));
	assert(nouveau_vram_dma_object(words,0x123400,0x3456ff,false));
	const uint32_t expected[]={9,0x1234,0x3456,0,0,0};assert(!memcmp(words,expected,sizeof(words)));
	assert(!nouveau_vram_dma_object(words,1,0xffff,true));
	assert(!nouveau_vram_dma_object(words,0,0x10000,true));
	assert(!nouveau_vram_dma_object(words,0,(UINT64_C(1)<<40)|255,true));
	assert(nouveau_instance_prepare(image,sizeof(image),vram,3));
	assert(nouveau_ramht_lookup(image,0,NOUVEAU_SURFACE_HANDLE,&off)&&off==0x1000);
	assert(nouveau_ramht_lookup(image,4,NOUVEAU_NOTIFIER_HANDLE,&off)&&off==0x1020);
	assert(!nouveau_ramht_lookup(image,1,NOUVEAU_NOTIFIER_HANDLE,&off));
	assert(image[0x1020/4]==0x49&&image[0x1024/4]==0xf0&&image[0x1028/4]==0xff);
	for(unsigned i=0xf000/4;i<sizeof(image)/4;i++)assert(image[i]==0);
	assert(!nouveau_instance_relocate(image,sizeof(image),vram,0,65536,&reg));
	assert(!nouveau_instance_relocate(image,sizeof(image),vram,0x40001,65536,&reg));
	assert(!nouveau_instance_relocate(image,sizeof(image),vram,vram-0x100000,65536,&reg));
	assert(!nouveau_instance_relocate(image,sizeof(image),vram,0x40000,65535,&reg));
	assert(!nouveau_instance_relocate(image,sizeof(image),vram,0x40000,(size_t)vram,&reg));
	assert(nouveau_instance_relocate(image,sizeof(image),vram,0x40000,65536,&reg)&&reg==0x409);
	assert(image[0x1024/4]==0x4f0&&image[0x1028/4]==0x4ff);
	/* Collision at final slot must wrap to slot zero, without cross-channel aliasing. */
	memset(image,0,sizeof(image));
	assert(nouveau_ramht_insert(image,0,511,0x1000));
	assert(nouveau_ramht_insert(image,0,512,0x1020));
	assert(nouveau_ramht_insert(image,0,1022,0x1040)); /* 510 XOR 1 = 511 */
	assert(image[511*2]==511&&image[0]==1022&&image[1]==0x208001);
	assert(nouveau_ramht_lookup(image,0,1022,&off)&&off==0x1040);
	assert(!nouveau_ramht_insert(image,0,1022,0x1060));
	assert(nouveau_ramht_insert(image,1,1022,0x1060));
	assert(!nouveau_ramht_insert(image,9,123,0x1000));
	assert(!nouveau_ramht_insert(image,0,123,0x1001));
	assert(!nouveau_ramht_insert(image,0,123,0xf000));
	memset(image,0,sizeof(image));
	for(unsigned i=1;i<=512;i++)assert(nouveau_ramht_insert(image,0,i,0x1000));
	assert(!nouveau_ramht_insert(image,0,513,0x1000));
	struct nouveau_display_plan plan={.head=3,.core_class=0x987d,.count=24};
	for(unsigned i=0;i<24;i++)plan.words[i]=i+1;
	struct nouveau_display_staging s={0};reset();
	assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_OK);
	assert(s.push_descriptor==0x2103&&buffers[0].owner==DMA_OWNER_CPU&&buffers[1].owner==DMA_OWNER_CPU);
	assert(!memcmp(buffers[1].cpu_virtual_address,plan.words,sizeof(plan.words)));
	for(unsigned i=24;i<1024;i++)assert(((uint32_t *)buffers[1].cpu_virtual_address)[i]==0);
	struct os_framebuffer_info fb={.width=1920,.height=1080,.pitch=7680,.size=7680*1080,
	    .bits_per_pixel=32,.red_mask_size=8,.red_mask_shift=16,.green_mask_size=8,
	    .green_mask_shift=8,.blue_mask_size=8,.blue_mask_shift=0};
	assert(s.staged_words==24&&s.head==3);
	uint32_t saved[1024];memcpy(saved,buffers[1].cpu_virtual_address,sizeof(saved));
	assert(!nouveau_display_staging_surface(&s,1,vram,&fb));
	assert(s.staged_words==24&&!memcmp(saved,buffers[1].cpu_virtual_address,sizeof(saved)));
	fail_query=queries+1;
	assert(!nouveau_display_staging_surface(&s,0,vram,&fb));fail_query=0;
	assert(s.staged_words==24&&!memcmp(saved,buffers[1].cpu_virtual_address,sizeof(saved)));
	corrupt_query=3;
	assert(!nouveau_display_staging_surface(&s,0,vram,&fb));corrupt_query=0;
	uint32_t *obj=(uint32_t *)buffers[0].cpu_virtual_address+0x1000/4;
	obj[0]=0x49;assert(!nouveau_display_staging_surface(&s,0,vram,&fb));obj[0]=9;
	assert(nouveau_display_staging_surface(&s,0,vram,&fb)&&s.staged_words==33);
	assert(!memcmp(buffers[1].cpu_virtual_address,plan.words,sizeof(plan.words)));
	const uint32_t expected_surface[]={0x40d60,0,0x100d68,0x04380780,
	    0x01001e00,0xcf00,0xf0000001,0x40db0,0};
	assert(!memcmp((uint32_t *)buffers[1].cpu_virtual_address+24,expected_surface,sizeof(expected_surface)));
	for(unsigned i=33;i<1024;i++)assert(((uint32_t *)buffers[1].cpu_virtual_address)[i]==0);
	assert(!nouveau_display_staging_surface(&s,0,vram,&fb));
	assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_INVALID);
	assert(nouveau_display_staging_release(&s)&&!s.instance.generation&&!s.push.generation);
	assert(nouveau_display_staging_release(&s));
	for(unsigned i=1;i<=2;i++){
		reset();fail_alloc=i;
		assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_ALLOCATION_FAILED);
		reset();fail_query=i;
		assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_ALLOCATION_FAILED);
	}
	for(unsigned i=1;i<=5;i++){
		reset();corrupt_query=i;
		assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_ALLOCATION_FAILED);
	}
	reset();available=false;
	assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_UNAVAILABLE&&!allocations);
	reset();fail_query=2;fail_free=1;
	assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_CLEANUP_FAILED);
	assert(s.instance.generation&&!s.push.generation);fail_free=0;
	assert(nouveau_display_staging_release(&s));reset();
	assert(nouveau_display_staging_create(vram,&plan,&s)==NOUVEAU_STAGING_OK);
	fail_free=2;assert(!nouveau_display_staging_release(&s)&&s.push.generation&&!s.instance.generation);
	fail_free=0;assert(nouveau_display_staging_release(&s));reset();
	puts("NOUVEAU_MEMORY_PASS: GP102 capacity, GF119 DMA golden words, RAMHT collisions/wrap/full, VRAM relocation guards, DMA lifecycle/rollback");
}
