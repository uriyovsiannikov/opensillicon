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
 * with snapshots, explicit rollback and pinned upload lifetime. Native
 * probe exercises only take/capabilities/restore, never instance binding. */
#include "kernel/nouveau_engine.h"
static const uint32_t masks[]={0x610090,0x6100a0,0x6100b0};
static bool read32(const struct nouveau_engine_io *io,uint32_t reg,uint32_t *value)
{
	return io->read32(io->context,reg,value) && *value!=UINT32_MAX;
}
static enum nouveau_engine_status idle(struct nouveau_engine *e,const struct nouveau_engine_io *io)
{
	for(unsigned i=0;i<17;i++) {
		uint32_t value;
		if(!read32(io,0x610490+i*16,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(value&0x801e0003) {
			e->blocked_reg=0x610490+i*16;e->blocked_value=value;e->blocked_mask=0x801e0003;
			return NOUVEAU_ENGINE_BUSY;
		}
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
enum nouveau_engine_status nouveau_engine_drain_disabled_core(const struct nouveau_engine_io *io)
{
	if(!io || !io->read32 || !io->write32 || !io->time_us || !io->delay_us)
		return NOUVEAU_ENGINE_INVALID;
	uint32_t core,value;uint64_t start,previous,now;
	if(!io->time_us(io->context,&start) || !read32(io,0x610490,&core))
		return NOUVEAU_ENGINE_IO_FAILED;
	if(core&0x80000013) return NOUVEAU_ENGINE_BUSY;
	for(unsigned i=1;i<17;i++) {
		if(!read32(io,0x610490+i*16,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(value&0x801e0003) return NOUVEAU_ENGINE_BUSY;
	}
	if(!(core&0x001e0000)) return NOUVEAU_ENGINE_OK;
	/* Recheck immediately before the GF119 core fini requests. Only write
	 * fields that were already disabled: no active firmware queue is stolen. */
	for(unsigned step=0;step<2;step++) {
		if(!read32(io,0x610490,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(value&0x80000013) return NOUVEAU_ENGINE_BUSY;
		if(!io->write32(io->context,0x610490,value&~(step?3U:0x10U)))
			return NOUVEAU_ENGINE_IO_FAILED;
	}
	previous=start;
	for(unsigned i=0;i<20001;i++) {
		if(!read32(io,0x610490,&value)) return NOUVEAU_ENGINE_IO_FAILED;
		if(value&0x80000013) return NOUVEAU_ENGINE_BUSY;
		if(!(value&0x001e0000)) return NOUVEAU_ENGINE_OK;
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
	e->blocked_reg=e->blocked_value=e->blocked_mask=0;
	uint32_t instance,firmware,interrupts[3],supervisor;uint64_t time;
	if(!io->time_us(io->context,&time) || !read32(io,0x610010,&instance) ||
	    !read32(io,0x6194e8,&firmware) || !read32(io,0x6100ac,&supervisor)) return NOUVEAU_ENGINE_IO_FAILED;
	for(unsigned i=0;i<3;i++) if(!read32(io,masks[i],&interrupts[i])) return NOUVEAU_ENGINE_IO_FAILED;
	enum nouveau_engine_status status=idle(e,io);
	bool deferred_core=status==NOUVEAU_ENGINE_BUSY && (supervisor&0x100) &&
	    e->blocked_reg==0x610490 && !(e->blocked_value&0x80000013);
	if(status!=NOUVEAU_ENGINE_OK && !deferred_core) return status;
	if(deferred_core) {
		/* Firmware may hold activity until request/ack completes. Still reject
		 * every other live channel before touching handoff, and never steal
		 * an allocated/connected/PUT-enabled core. */
		for(unsigned i=1;i<17;i++) {
			uint32_t value;
			if(!read32(io,0x610490+i*16,&value)) return NOUVEAU_ENGINE_IO_FAILED;
			if(value&0x801e0003) {
				e->blocked_reg=0x610490+i*16;e->blocked_value=value;e->blocked_mask=0x801e0003;
				return NOUVEAU_ENGINE_BUSY;
			}
		}
	}
	*e=(struct nouveau_engine){.state=NOUVEAU_ENGINE_QUARANTINED,.io=io,
	    .saved_instance=instance,.saved_firmware=firmware,.saved_supervisor=supervisor,.snapshot_valid=true};
	for(unsigned i=0;i<3;i++) e->saved_interrupts[i]=interrupts[i];
	if(supervisor&0x100) {
		/* Write-one-to-clear is an acknowledgement, not restorable state. */
		if(!io->write32(io->context,0x6100ac,0x100)) return NOUVEAU_ENGINE_IO_FAILED;
		e->firmware_touched=true;
		if(!io->write32(io->context,0x6194e8,firmware&~1U)) return NOUVEAU_ENGINE_IO_FAILED;
		status=wait_firmware(io);
		if(status!=NOUVEAU_ENGINE_OK) return status;
	} else if(firmware&3) {
		/* Avoid assuming ownership from inconsistent request/ack state. */
		e->blocked_reg=0x6194e8;e->blocked_value=firmware;e->blocked_mask=3;
		return NOUVEAU_ENGINE_BUSY;
	}
	/* Ack alone is insufficient if firmware ignored the request-bit write. */
	uint32_t released;
	if(!read32(io,0x6194e8,&released)) return NOUVEAU_ENGINE_IO_FAILED;
	if(released&3) {
		e->blocked_reg=0x6194e8;e->blocked_value=released;e->blocked_mask=3;
		return NOUVEAU_ENGINE_BUSY;
	}
	if(deferred_core) {
		e->drain_attempted=true;
		e->drain_status=nouveau_engine_drain_disabled_core(io);
		if(e->drain_status!=NOUVEAU_ENGINE_OK) return e->drain_status;
	}
	status=idle(e,io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	e->interrupts_touched=true;
	for(unsigned i=0;i<3;i++) {
		uint32_t value;
		if(!io->write32(io->context,masks[i],0) || !read32(io,masks[i],&value) || value)
			return NOUVEAU_ENGINE_IO_FAILED;
	}
	e->state=NOUVEAU_ENGINE_QUIESCENT;return NOUVEAU_ENGINE_OK;
}
enum nouveau_engine_status nouveau_engine_capabilities(struct nouveau_engine *e)
{
	if(!e || e->state!=NOUVEAU_ENGINE_QUIESCENT || !e->snapshot_valid ||
	    e->caps_ready || e->caps_touched) return NOUVEAU_ENGINE_INVALID;
	enum nouveau_engine_status status=idle(e,e->io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	uint32_t count,mask;
	if(!read32(e->io,0x22448,&count) || !read32(e->io,0x612004,&mask)) return NOUVEAU_ENGINE_IO_FAILED;
	if(!count || count>4 || !(mask&15) || ((mask&15)&~((1U<<count)-1))) return NOUVEAU_ENGINE_INVALID;
	struct nouveau_engine_cap caps[20];unsigned used=0;
	for(unsigned i=0;i<count;i++) if(mask&(1U<<i)) {
		for(unsigned j=0;j<3;j++) {
			struct nouveau_engine_cap *c=&caps[used++];
			c->reg=0x6101b4+i*0x800+j*4;
			if(!read32(e->io,c->reg,&c->saved) ||
			    !read32(e->io,0x616104+i*0x800+j*4,&c->value)) return NOUVEAU_ENGINE_IO_FAILED;
		}
	}
	/* gf119_disp_init copies the fixed SOR register bank (nr=8). */
	for(unsigned i=0;i<8;i++) {
		struct nouveau_engine_cap *c=&caps[used++];c->reg=0x6301c4+i*0x800;
		if(!read32(e->io,c->reg,&c->saved) ||
		    !read32(e->io,0x61c000+i*0x800,&c->value)) return NOUVEAU_ENGINE_IO_FAILED;
	}
	for(unsigned i=0;i<used;i++) e->caps[i]=caps[i];
	e->cap_count=used;e->caps_touched=true;e->state=NOUVEAU_ENGINE_QUARANTINED;
	for(unsigned i=0;i<used;i++) {
		uint32_t value;
		if(!e->io->write32(e->io->context,caps[i].reg,caps[i].value) ||
		    !read32(e->io,caps[i].reg,&value) || value!=caps[i].value) return NOUVEAU_ENGINE_IO_FAILED;
	}
	e->caps_ready=true;e->state=NOUVEAU_ENGINE_QUIESCENT;return NOUVEAU_ENGINE_OK;
}

enum nouveau_engine_status nouveau_engine_bind(struct nouveau_engine *e,struct nouveau_upload *u)
{
	if(!e || e->state!=NOUVEAU_ENGINE_QUIESCENT || !e->snapshot_valid || !e->caps_ready || e->instance ||
	    !u || u->state!=NOUVEAU_UPLOAD_VERIFIED || u->length!=65536 ||
	    (u->address&65535) || u->address>=(UINT64_C(1)<<40)) return NOUVEAU_ENGINE_INVALID;
	enum nouveau_engine_status status=idle(e,e->io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	if(!nouveau_upload_pin(u)) return NOUVEAU_ENGINE_INVALID;
	e->instance=u;e->instance_touched=true;e->state=NOUVEAU_ENGINE_QUARANTINED;
	uint32_t value,reg=(uint32_t)(u->address>>8)|9;
	if(!e->io->write32(e->io->context,0x610010,reg) ||
	    !read32(e->io,0x610010,&value) || value!=reg) return NOUVEAU_ENGINE_IO_FAILED;
	e->state=NOUVEAU_ENGINE_BOUND;return NOUVEAU_ENGINE_OK;
}
enum nouveau_engine_status nouveau_engine_unbind(struct nouveau_engine *e)
{
	if(!e || !e->snapshot_valid || !e->io || !e->instance ||
	    (e->state!=NOUVEAU_ENGINE_BOUND && e->state!=NOUVEAU_ENGINE_QUARANTINED))
		return NOUVEAU_ENGINE_INVALID;
	e->state=NOUVEAU_ENGINE_QUARANTINED;
	enum nouveau_engine_status status=idle(e,e->io);
	if(status!=NOUVEAU_ENGINE_OK) return status;
	uint32_t value;
	if(e->instance_touched) {
		if(!e->io->write32(e->io->context,0x610010,e->saved_instance) ||
		    !read32(e->io,0x610010,&value) || value!=e->saved_instance) return NOUVEAU_ENGINE_IO_FAILED;
		if(!nouveau_upload_unpin(e->instance)) return NOUVEAU_ENGINE_INVALID;
		e->instance_touched=false;
	}
	if(nouveau_upload_restore(e->instance)!=NOUVEAU_UPLOAD_OK) return NOUVEAU_ENGINE_IO_FAILED;
	e->instance=NULL;e->state=NOUVEAU_ENGINE_QUIESCENT;
	return NOUVEAU_ENGINE_OK;
}
enum nouveau_engine_status nouveau_engine_restore(struct nouveau_engine *e)
{
	if(!e || e->state==NOUVEAU_ENGINE_EMPTY || !e->snapshot_valid || !e->io)
		return NOUVEAU_ENGINE_INVALID;
	e->state=NOUVEAU_ENGINE_QUARANTINED;
	/* Failed handoff/drain never bound any driver resources or changed
	 * capability/IRQ state. Return only the firmware request bit even when
	 * firmware activity could not drain. Full teardown still requires idle. */
	if(e->instance || e->instance_touched || e->caps_touched || e->interrupts_touched) {
		enum nouveau_engine_status status=idle(e,e->io);
		if(status!=NOUVEAU_ENGINE_OK) return status;
	}
	/* Restore VRAM while driver ownership still holds, BEFORE handing the
	 * firmware request bit back. A rollback failure remains retryable. */
	if(e->instance) {
		enum nouveau_engine_status status=nouveau_engine_unbind(e);
		if(status!=NOUVEAU_ENGINE_OK) return status;
	}
	/* Unbind alone returns QUIESCENT; the larger restore remains ambiguous
	 * until every capability/IRQ/firmware write has been verified. */
	e->state=NOUVEAU_ENGINE_QUARANTINED;
	uint32_t value;
	if(e->caps_touched) {
		for(unsigned i=0;i<e->cap_count;i++) {
			if(!e->io->write32(e->io->context,e->caps[i].reg,e->caps[i].saved) ||
			    !read32(e->io,e->caps[i].reg,&value) || value!=e->caps[i].saved) return NOUVEAU_ENGINE_IO_FAILED;
		}
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
