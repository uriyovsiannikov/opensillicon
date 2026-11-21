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
/* Native GF119 supervisor dispatcher adapted from Linux v6.12 NVKM.
 * Stage callbacks are required; unsupported BIOS/PLL operations must fail. */
#include "kernel/nouveau_supervisor.h"
static bool run(const struct nouveau_supervisor_io *io,enum nouveau_supervisor_action action,
	const struct nouveau_supervisor *s,uint32_t mask)
{
	for(unsigned i=0;i<4;i++) if((s->head_mask&(1U<<i)) && (s->head_masks[i]&mask))
		if(!io->action(io->context,action,i)) return false;
	return true;
}
enum nouveau_supervisor_status nouveau_supervisor_step(struct nouveau_supervisor *s,
	const struct nouveau_supervisor_io *io)
{
	if(!s || s->state!=NOUVEAU_SUPERVISOR_IDLE || !s->head_mask || (s->head_mask&~15) ||
	    !io || !io->read32 || !io->write32 || !io->action) return NOUVEAU_SUPERVISOR_INVALID;
	s->state=NOUVEAU_SUPERVISOR_PROCESSING;s->acknowledged=false;s->pending=0;
	for(unsigned i=0;i<4;i++) s->head_masks[i]=0;
	uint32_t status;
	if(!io->read32(io->context,0x6100ac,&status) || status==UINT32_MAX) goto io_failed;
	s->pending=status&7;
	if(!s->pending) {s->state=NOUVEAU_SUPERVISOR_IDLE;return NOUVEAU_SUPERVISOR_NO_EVENT;}
	if(s->pending&(s->pending-1)) {
		s->state=NOUVEAU_SUPERVISOR_FAILED;return NOUVEAU_SUPERVISOR_AMBIGUOUS;
	}
	for(unsigned i=0;i<4;i++) if(s->head_mask&(1U<<i))
		if(!io->read32(io->context,0x6101d4+i*0x800,&s->head_masks[i]) ||
		    s->head_masks[i]==UINT32_MAX) goto io_failed;
	/* Mark acknowledgement ambiguous BEFORE a possibly side-effecting
	 * failed write; no automatic retry can safely repeat BIOS scripts. */
	if(!io->write32(io->context,0x6100ac,s->pending)) goto io_failed;
	s->acknowledged=true;
	bool ok;
	if(s->pending==1) {
		ok=io->action(io->context,NOUVEAU_SUPERVISOR_SNAPSHOT,255) &&
		    run(io,NOUVEAU_SUPERVISOR_DETACH1,s,0x1000);
	} else if(s->pending==2) {
		ok=run(io,NOUVEAU_SUPERVISOR_DETACH2,s,0x1000) &&
		    run(io,NOUVEAU_SUPERVISOR_PLL,s,0x10000) &&
		    run(io,NOUVEAU_SUPERVISOR_ATTACH2,s,0x1000);
	} else ok=run(io,NOUVEAU_SUPERVISOR_ATTACH3,s,0x1000);
	if(!ok) {s->state=NOUVEAU_SUPERVISOR_FAILED;return NOUVEAU_SUPERVISOR_ACTION_FAILED;}
	for(unsigned i=0;i<4;i++) if(s->head_mask&(1U<<i))
		if(!io->write32(io->context,0x6101d4+i*0x800,0)) goto io_failed;
	if(!io->write32(io->context,0x6101d0,0x80000000)) goto io_failed;
	s->state=NOUVEAU_SUPERVISOR_IDLE;return NOUVEAU_SUPERVISOR_OK;
io_failed:
	s->state=NOUVEAU_SUPERVISOR_FAILED;return NOUVEAU_SUPERVISOR_IO_FAILED;
}
