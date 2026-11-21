/*
 * Copyright 2012 Red Hat Inc.
 * Copyright 2016 Red Hat Inc.
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
 */
/* Adapted from Linux v6.12 NVKM core/ramht.c, engine/dma/usergf119.c,
 * subdev/fb/gp102.c. Native CPU staging, no Linux runtime dependencies. */
#include "kernel/nouveau_memory.h"

bool nouveau_gp102_vram_size(uint32_t reg,uint64_t *bytes)
{
	if(bytes==NULL) return false;
	*bytes=0;
	if(reg==UINT32_MAX) return false;
	uint64_t size=(uint64_t)((reg>>4)&63) << ((reg&15)+20);
	if(reg&0x40000000) size=size/16*15;
	if(!size || size>(UINT64_C(1)<<40)) return false;
	*bytes=size;return true;
}

bool nouveau_vram_dma_object(uint32_t w[6],uint64_t start,uint64_t limit,bool sp)
{
	if(w==NULL || start>limit || (start&255) || (limit&255)!=255 ||
	    limit>=(UINT64_C(1)<<40)) return false;
	w[0]=9|(sp?0x40:0);w[1]=start>>8;w[2]=limit>>8;
	w[3]=w[4]=w[5]=0;return true;
}

static unsigned hash(unsigned channel,uint32_t handle)
{
	unsigned value=channel<<5;
	while(handle) { value^=handle&511;handle>>=9; }
	return value;
}

bool nouveau_ramht_lookup(const uint32_t *image,unsigned channel,uint32_t handle,
	uint32_t *offset)
{
	if(!image || !offset || channel>8 || !handle) return false;
	unsigned slot=hash(channel,handle);
	for(unsigned i=0;i<NOUVEAU_RAMHT_SLOTS;i++,slot=(slot+1)&511) {
		uint32_t context=image[slot*2+1];
		if((context&1) && (context>>27)==channel && image[slot*2]==handle) {
			uint32_t addr=(context&0x07fffffe)>>9;
			if((addr&15) || addr<0x1000 || addr>NOUVEAU_NOTIFIER_OFFSET-24) return false;
			*offset=addr;return true;
		}
	}
	return false;
}

bool nouveau_ramht_insert(uint32_t *image,unsigned channel,uint32_t handle,uint32_t offset)
{
	uint32_t existing;
	if(!image || channel>8 || !handle || (offset&15) || offset<0x1000 ||
	    offset>NOUVEAU_NOTIFIER_OFFSET-24 ||
	    nouveau_ramht_lookup(image,channel,handle,&existing)) return false;
	unsigned slot=hash(channel,handle);
	for(unsigned i=0;i<NOUVEAU_RAMHT_SLOTS;i++,slot=(slot+1)&511) {
		if(!image[slot*2+1]) {
			image[slot*2]=handle;
			image[slot*2+1]=(channel<<27)|(offset<<9)|1;
			return true;
		}
	}
	return false;
}

bool nouveau_instance_prepare(uint32_t *image,size_t size,uint64_t vram,unsigned head)
{
	if(!image || size!=NOUVEAU_INSTANCE_SIZE || head>=4 ||
	    vram<0x150000 || vram>(UINT64_C(1)<<40) || (vram&255)) return false;
	for(unsigned i=0;i<size/4;i++) image[i]=0;
	if(!nouveau_vram_dma_object(image+NOUVEAU_SURFACE_OBJECT/4,0,vram-1,false) ||
	    !nouveau_vram_dma_object(image+NOUVEAU_NOTIFIER_OBJECT/4,
	    NOUVEAU_NOTIFIER_OFFSET,NOUVEAU_INSTANCE_SIZE-1,true)) return false;
	for(unsigned i=0;i<2;i++) {
		unsigned channel=i?head+1:0;
		if(!nouveau_ramht_insert(image,channel,NOUVEAU_SURFACE_HANDLE,NOUVEAU_SURFACE_OBJECT) ||
		    !nouveau_ramht_insert(image,channel,NOUVEAU_NOTIFIER_HANDLE,NOUVEAU_NOTIFIER_OBJECT)) return false;
	}
	return true;
}

bool nouveau_instance_relocate(uint32_t *image,size_t size,uint64_t vram,
	uint64_t base,size_t owned_size,uint32_t *reg)
{
	/* GP102 RAM allocator reserves VGA head 256KiB and VBIOS tail 1MiB. */
	if(!image || !reg || size!=NOUVEAU_INSTANCE_SIZE || owned_size<size ||
	    vram<0x150000 || vram>(UINT64_C(1)<<40) || (base&0xffff) ||
	    base<0x40000 || base>vram-0x100000 || owned_size>vram-0x100000-base) return false;
	uint32_t *obj=image+NOUVEAU_NOTIFIER_OBJECT/4;
	if(!nouveau_vram_dma_object(obj,base+NOUVEAU_NOTIFIER_OFFSET,base+size-1,true)) return false;
	*reg=(uint32_t)(base>>8)|9;return true;
}
