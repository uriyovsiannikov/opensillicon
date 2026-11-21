/*
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
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
/* GP102 channel init and GF119 fini, adapted from Linux v6.12 NVKM.
 * Injectable transport; native boot probe deliberately does not call this
 * until engine ownership and instance upload have been implemented. */
#include "kernel/nouveau_channel.h"
#define TIMEOUT_US UINT64_C(2000000)
#define MAX_POLLS 20001U
static bool valid_io(const struct nouveau_channel_io *io)
{
	return io && io->read32 && io->write32 && io->time_us && io->delay_us &&
	    io->acquire && io->release;
}
static bool mask(const struct nouveau_channel_io *io,uint32_t reg,uint32_t bits,uint32_t value)
{
	uint32_t old;
	return io->read32(io->context,reg,&old) && old!=UINT32_MAX &&
	    io->write32(io->context,reg,(old&~bits)|value);
}
static enum nouveau_channel_status wait_clear(const struct nouveau_channel_io *io,
	uint32_t reg,uint32_t bits)
{
	uint64_t start,now,previous;
	if(!io->time_us(io->context,&start)) return NOUVEAU_CHANNEL_IO_FAILED;
	previous=start;
	for(unsigned i=0;i<MAX_POLLS;i++) {
		uint32_t value;
		if(!io->read32(io->context,reg,&value) || value==UINT32_MAX)
			return NOUVEAU_CHANNEL_IO_FAILED;
		if(!(value&bits)) return NOUVEAU_CHANNEL_OK;
		if(!io->time_us(io->context,&now) || now<previous)
			return NOUVEAU_CHANNEL_IO_FAILED;
		if(now-start>=TIMEOUT_US || i+1==MAX_POLLS) return NOUVEAU_CHANNEL_TIMEOUT;
		previous=now;
		if(!io->delay_us(io->context,100)) return NOUVEAU_CHANNEL_IO_FAILED;
	}
	/* Covers a stalled clock or a callback that does not actually delay. */
	return NOUVEAU_CHANNEL_TIMEOUT;
}
bool nouveau_channel_prepare(struct nouveau_channel *c,unsigned id,uint32_t push,uint32_t prepared_bytes)
{
	if(!c || c->state!=NOUVEAU_CHANNEL_EMPTY || c->resources_held || id>8 ||
	    (push&15)!=3 || (prepared_bytes&3) || prepared_bytes>4092) return false;
	*c=(struct nouveau_channel){.state=NOUVEAU_CHANNEL_PREPARED,.id=id,.push=push,.prepared_bytes=prepared_bytes};
	return true;
}
enum nouveau_channel_status nouveau_channel_start(struct nouveau_channel *c,
	const struct nouveau_channel_io *io)
{
	if(!c || !valid_io(io) || c->id>8 || c->state!=NOUVEAU_CHANNEL_PREPARED ||
	    c->resources_held || (c->push&15)!=3 ||
	    (c->prepared_bytes&3) || c->prepared_bytes>4092) return NOUVEAU_CHANNEL_INVALID;
	uint32_t ctrl=0x610490+c->id*16,old;uint64_t time;
	if(!io->time_us(io->context,&time) || !io->read32(io->context,ctrl,&old) || old==UINT32_MAX)
		return NOUVEAU_CHANNEL_IO_FAILED;
	/* Never steal a firmware/other driver's live or pending channel. */
	if(old&0x801e1013) return NOUVEAU_CHANNEL_BUSY;
	if(!io->acquire(io->context)) return NOUVEAU_CHANNEL_RESOURCE_FAILED;
	c->resources_held=true;c->state=NOUVEAU_CHANNEL_QUARANTINED;
	/* Mark dirty BEFORE the first write: even false callbacks may have
	 * delivered the write, so release requires a verified stop afterwards. */
	if(!io->write32(io->context,0x611494+c->id*16,c->push) ||
	    !io->write32(io->context,0x611498+c->id*16,0x10000) ||
	    !io->write32(io->context,0x61149c+c->id*16,1) ||
	    !mask(io,ctrl,0x10,0x10) ||
	    !io->write32(io->context,0x640000+c->id*0x1000,0) ||
	    !io->write32(io->context,ctrl,c->id?0x13:0x01000013))
		return NOUVEAU_CHANNEL_IO_FAILED;
	enum nouveau_channel_status status=wait_clear(io,ctrl,0x80000000);
	if(status==NOUVEAU_CHANNEL_OK) { c->state=NOUVEAU_CHANNEL_RUNNING;c->saved_put=0; }
	return status;
}
enum nouveau_channel_status nouveau_channel_kick(struct nouveau_channel *c,
	const struct nouveau_channel_io *io,uint32_t put)
{
	if(!c || !valid_io(io) || c->id>8 || c->state!=NOUVEAU_CHANNEL_RUNNING ||
	    !c->resources_held || (put&3) || put>4092 || put>c->prepared_bytes || put<=c->saved_put)
		return NOUVEAU_CHANNEL_INVALID;
	uint32_t reg=0x640000+c->id*0x1000,value;
	/* All methods were immutable and DMA-published by acquire before init.
	 * Mark ambiguous execution before writing the doorbell. */
	c->state=NOUVEAU_CHANNEL_QUARANTINED;
	if(!io->write32(io->context,reg,put) || !io->read32(io->context,reg,&value) ||
	    value!=put) return NOUVEAU_CHANNEL_IO_FAILED;
	c->saved_put=put;c->state=NOUVEAU_CHANNEL_RUNNING;
	return NOUVEAU_CHANNEL_OK;
}

enum nouveau_channel_status nouveau_channel_stop(struct nouveau_channel *c,
	const struct nouveau_channel_io *io)
{
	if(!c || !valid_io(io) || c->id>8 || !c->resources_held ||
	    (c->state!=NOUVEAU_CHANNEL_RUNNING && c->state!=NOUVEAU_CHANNEL_QUARANTINED))
		return NOUVEAU_CHANNEL_INVALID;
	c->state=NOUVEAU_CHANNEL_QUARANTINED;
	uint32_t ctrl=0x610490+c->id*16;
	if(!mask(io,ctrl,c->id?0x1010:0x10,c->id?0x1000:0) ||
	    !mask(io,ctrl,3,0)) return NOUVEAU_CHANNEL_IO_FAILED;
	/* GF119 waits for activity[20:17]. Also require the GP102 init busy
	 * bit to clear before allowing device-visible resources to be freed. */
	enum nouveau_channel_status status=wait_clear(io,ctrl,0x801e0000);
	if(status!=NOUVEAU_CHANNEL_OK) return status;
	if(!io->read32(io->context,0x640000+c->id*0x1000,&c->saved_put) ||
	    c->saved_put==UINT32_MAX) return NOUVEAU_CHANNEL_IO_FAILED;
	c->state=NOUVEAU_CHANNEL_STOPPED;return NOUVEAU_CHANNEL_OK;
}
enum nouveau_channel_status nouveau_channel_release(struct nouveau_channel *c,
	const struct nouveau_channel_io *io)
{
	if(!c || !valid_io(io) || c->state!=NOUVEAU_CHANNEL_STOPPED || !c->resources_held)
		return NOUVEAU_CHANNEL_INVALID;
	if(!io->release(io->context)) return NOUVEAU_CHANNEL_RESOURCE_FAILED;
	*c=(struct nouveau_channel){0};return NOUVEAU_CHANNEL_OK;
}
