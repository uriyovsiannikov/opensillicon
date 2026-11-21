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
/* GF119 firmware handoff and instance binding from NVKM v6.12, adapted
 * with snapshots, explicit rollback and pinned upload lifetime. No native
 * boot caller until supervisor/capability/PLL integration is complete. */
#include "kernel/nouveau_engine.h"
static const uint32_t masks[]={0x610090,0x6100a0,0x6100b0};
static bool read32(const struct nouveau_engine_io *io,uint32_t reg,uint32_t *value)
{
	return io->read32(io->context,reg,value) && *value!=UINT32_MAX;
}
static enum nouveau_engine_status idle(const struct nouveau_engine_io *io)
{
	for(unsigned i=0;i<17;i++) {
		uint32_t value;
		if(!read32(io,0x610490+i*16,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(value&0x801e0003) return NOUVEAU_ENGINE_BUSY;
	}
	return NOUVEAU_ENGINE_OK;
}
static enum nouveau_engine_status wait_firmware(const struct nouveau_engine_io *io)
{
	uint64_t start,previous,now;
	if(!io->time_us(io->context,&start)) return NOUVEAU_ENGINE_IO_FAILED;
	previous=start;
	for(unsigned i=0;i<20001;i++) {
		uint32_t value;
		if(!read32(io,0x6194e8,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(!(value&2)) return NOUVEAU_ENGINE_OK;
		if(!io->time_us(io->context,&now) || now<previous) return NOUVEAU_ENGINE_IO_FAILED;
		if(now-start>=2000000 || i==20000) return NOUVEAU_ENGINE_TIMEOUT;
		previous=now;
		if(!io->delay_us(io->context,100)) return NOUVEAU_ENGINE_IO_FAILED;
	}
	return NOUVEAU_ENGINE_TIMEOUT;
}
enum nouveau_engine_status nouveau_engine_take(struct nouveau_engine *e,const struct nouveau_engine_io *io)
{
	if(!e || e->state!=NOUVEAU_ENGINE_EMPTY || e->snapshot_valid || !io ||
	    !io->read32 || !io->write32 || !io->time_us || !io->delay_us) return NOUVEAU_ENGINE_INVALID;
	uint32_t instance,firmware,interrupts[3],supervisor;uint64_t time;
	if(!io->time_us(io->context,&time) || !read32(io,0x610010,&instance) ||
	    !read32(io,0x6194e8,&firmware) || !read32(io,0x6100ac,&supervisor)) return NOUVEAU_ENGINE_IO_FAILED;
	for(unsigned i=0;i<3;i++) if(!read32(io,masks[i],&interrupts[i])) return NOUVEAU_ENGINE_IO_FAILED;
	enum nouveau_engine_status status=idle(io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	*e=(struct nouveau_engine){.state=NOUVEAU_ENGINE_QUARANTINED,.io=io,
	    .saved_instance=instance,.saved_firmware=firmware,.snapshot_valid=true};
	for(unsigned i=0;i<3;i++) e->saved_interrupts[i]=interrupts[i];
	if(supervisor&0x100) {
		/* Write-one-to-clear is an acknowledgement, not restorable state. */
		if(!io->write32(io->context,0x6100ac,0x100)) return NOUVEAU_ENGINE_IO_FAILED;
		e->firmware_touched=true;
		if(!io->write32(io->context,0x6194e8,firmware&~1U)) return NOUVEAU_ENGINE_IO_FAILED;
		status=wait_firmware(io);
		if(status!=NOUVEAU_ENGINE_OK) return status;
	} else if(firmware&2) {
		/* Avoid assuming ownership from inconsistent request/ack state. */
		return NOUVEAU_ENGINE_BUSY;
	}
	status=idle(io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	e->interrupts_touched=true;
	for(unsigned i=0;i<3;i++) {
		uint32_t value;
		if(!io->write32(io->context,masks[i],0) || !read32(io,masks[i],&value) || value)
			return NOUVEAU_ENGINE_IO_FAILED;
	}
	e->state=NOUVEAU_ENGINE_QUIESCENT;return NOUVEAU_ENGINE_OK;
}
enum nouveau_engine_status nouveau_engine_bind(struct nouveau_engine *e,struct nouveau_upload *u)
{
	if(!e || e->state!=NOUVEAU_ENGINE_QUIESCENT || !e->snapshot_valid || e->instance ||
	    !u || u->state!=NOUVEAU_UPLOAD_VERIFIED || u->length!=65536 ||
	    (u->address&65535) || u->address>=(UINT64_C(1)<<40)) return NOUVEAU_ENGINE_INVALID;
	enum nouveau_engine_status status=idle(e->io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	if(!nouveau_upload_pin(u)) return NOUVEAU_ENGINE_INVALID;
	e->instance=u;e->instance_touched=true;e->state=NOUVEAU_ENGINE_QUARANTINED;
	uint32_t value,reg=(uint32_t)(u->address>>8)|9;
	if(!e->io->write32(e->io->context,0x610010,reg) ||
	    !read32(e->io,0x610010,&value) || value!=reg) return NOUVEAU_ENGINE_IO_FAILED;
	e->state=NOUVEAU_ENGINE_BOUND;return NOUVEAU_ENGINE_OK;
}
enum nouveau_engine_status nouveau_engine_restore(struct nouveau_engine *e)
{
	if(!e || e->state==NOUVEAU_ENGINE_EMPTY || !e->snapshot_valid || !e->io)
		return NOUVEAU_ENGINE_INVALID;
	e->state=NOUVEAU_ENGINE_QUARANTINED;
	enum nouveau_engine_status status=idle(e->io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	uint32_t value;
	if(e->instance_touched) {
		if(!e->io->write32(e->io->context,0x610010,e->saved_instance) ||
		    !read32(e->io,0x610010,&value) || value!=e->saved_instance) return NOUVEAU_ENGINE_IO_FAILED;
		if(!e->instance || !nouveau_upload_unpin(e->instance)) return NOUVEAU_ENGINE_INVALID;
		e->instance_touched=false;
	}
	/* Restore VRAM while driver ownership still holds, BEFORE handing the
	 * firmware request bit back. A rollback failure remains retryable. */
	if(e->instance) {
		if(nouveau_upload_restore(e->instance)!=NOUVEAU_UPLOAD_OK) return NOUVEAU_ENGINE_IO_FAILED;
		e->instance=NULL;
	}
	if(e->interrupts_touched) {
		for(unsigned i=0;i<3;i++) {
			if(!e->io->write32(e->io->context,masks[i],e->saved_interrupts[i]) ||
			    !read32(e->io,masks[i],&value) || value!=e->saved_interrupts[i]) return NOUVEAU_ENGINE_IO_FAILED;
		}
	}
	if(e->firmware_touched) {
		if(!read32(e->io,0x6194e8,&value) ||
		    !e->io->write32(e->io->context,0x6194e8,(value&~1U)|(e->saved_firmware&1)) ||
		    !read32(e->io,0x6194e8,&value) || (value&1)!=(e->saved_firmware&1)) return NOUVEAU_ENGINE_IO_FAILED;
	}
	*e=(struct nouveau_engine){0};return NOUVEAU_ENGINE_OK;
}
