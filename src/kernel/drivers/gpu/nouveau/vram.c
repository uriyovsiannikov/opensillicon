/*
 * Copyright 2012 Red Hat Inc.
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
/* Native bounded read transport adapted from Linux v6.12
 * nvkm/subdev/instmem/nv50.c nv50_instobj_rd32_slow. */
#include "kernel/nouveau_vram.h"

enum nouveau_vram_status nouveau_vram_read(const struct nouveau_pramin_io *io,
	uint64_t size,uint64_t address,uint32_t *destination,size_t length)
{
	if(!io || !io->read32 || !io->write32 || !destination || !length ||
	    length>NOUVEAU_INSTANCE_SIZE || ((uintptr_t)destination&3) || (length&3) || (address&3) ||
	    size>(UINT64_C(1)<<40) || address>=size || length>size-address)
		return NOUVEAU_VRAM_INVALID;
	uint32_t saved,current,check;
	if(!io->read32(io->context,0x1700,&saved) || saved==UINT32_MAX)
		return NOUVEAU_VRAM_IO_FAILED;
	current=saved;
	bool changed=false;
	enum nouveau_vram_status status=NOUVEAU_VRAM_IO_FAILED;
	for(size_t i=0;i<length/4;i++) {
		uint64_t physical=address+i*4;
		uint32_t window=(physical&~UINT64_C(0xfffff))>>16;
		if(current!=window) {
			/* Treat a failed write callback as potentially having written. */
			changed=true;
			if(!io->write32(io->context,0x1700,window) ||
			    !io->read32(io->context,0x1700,&check) || check!=window) goto out;
			current=window;
		}
		if(!io->read32(io->context,0x700000+(physical&0xfffff),&destination[i])) goto out;
	}
	status=NOUVEAU_VRAM_OK;
out:
	if(changed) {
		/* Attempt read-back even if the write callback reports failure. */
		bool written=io->write32(io->context,0x1700,saved);
		bool read=io->read32(io->context,0x1700,&check);
		if(!written || !read || check!=saved) return NOUVEAU_VRAM_RESTORE_FAILED;
	}
	return status;
}


enum nouveau_scanout_match nouveau_scanout_compare(const struct nouveau_scanout_reader *io,
	uint64_t vram,uint64_t candidate,uint64_t span)
{
	if(!io || !io->cpu || !io->gpu || span<8192 || span>64U*1024U*1024U ||
	    (span&3) || (candidate&3) || vram>(UINT64_C(1)<<40) ||
	    candidate>=vram || span>vram-candidate) return NOUVEAU_SCANOUT_INVALID;
	uint32_t before[16],after[16],gpu[16],first=0;
	bool diverse=false;
	for(unsigned i=0;i<128;i++) {
		uint64_t offset=(((span-64)*i)/127)&~UINT64_C(3);
		if(!io->cpu(io->context,offset,before,16) ||
		    !io->gpu(io->context,candidate+offset,gpu,16) ||
		    !io->cpu(io->context,offset,after,16)) return NOUVEAU_SCANOUT_IO_FAILED;
		for(unsigned j=0;j<16;j++) if(before[j]!=after[j]) return NOUVEAU_SCANOUT_UNSTABLE;
		for(unsigned j=0;j<16;j++) if(before[j]!=gpu[j]) return NOUVEAU_SCANOUT_MISMATCH;
		if(!i) first=before[0];
		for(unsigned j=0;j<16;j++) if(before[j]!=first) diverse=true;
	}
	return diverse?NOUVEAU_SCANOUT_SAMPLE_MATCH:NOUVEAU_SCANOUT_FLAT;
}
