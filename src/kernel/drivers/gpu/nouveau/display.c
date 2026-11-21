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
/* NVKM GF119/GM200 display state and NV907D head methods used by GP102.
 * Copyright 2012, 2014, 2016, 2018 Red Hat Inc. SPDX-License-Identifier: MIT
 * Adapted from Linux v6.12; see upstream/display/ and README.md.
 * This module reads state and builds CPU-side timing packets only. */
#include "kernel/nouveau_display.h"

#define READ(reg, target) do { if (!read(context,(reg),&(target))) return false; } while (0)
static bool bank(nouveau_display_reader read, void *context, uint32_t base,
	struct nouveau_head_bank *b)
{
	READ(base+0x404,b->output); READ(base+0x408,b->control);
	READ(base+0x414,b->raster); READ(base+0x418,b->sync_end);
	READ(base+0x41c,b->blank_end); READ(base+0x420,b->blank_start);
	READ(base+0x424,b->blank2); READ(base+0x450,b->clock);
	READ(base+0x454,b->clock_config); READ(base+0x460,b->image_offset);
	READ(base+0x468,b->image_size); READ(base+0x46c,b->storage);
	READ(base+0x470,b->params); READ(base+0x474,b->dma);
	READ(base+0x4b8,b->viewport_in); READ(base+0x4c0,b->viewport_out);
	b->htotal=b->raster&0x7fff; b->vtotal=(b->raster>>16)&0x7fff;
	unsigned hb=b->blank_end&0x7fff, vb=(b->blank_end>>16)&0x7fff;
	unsigned hs=b->blank_start&0x7fff, vs=(b->blank_start>>16)&0x7fff;
	if (b->raster!=UINT32_MAX && b->clock!=UINT32_MAX && b->htotal && b->vtotal &&
	    hs>hb && vs>vb && hs<b->htotal && vs<b->vtotal && (b->clock&0x7fffffff)) {
		b->width=hs-hb; b->height=vs-vb;
		uint64_t hz=(uint64_t)(b->clock&0x7fffffff)*1000;
		if(b->clock&0x80000000U) hz=hz*1000/1001;
		if(b->control&1) hz*=2;
		b->refresh_millihz=(uint32_t)(hz/((uint32_t)b->htotal*b->vtotal));
		b->timing_valid=b->refresh_millihz>=1000 && b->refresh_millihz<=1000000;
	}
	return true;
}

bool nouveau_display_snapshot(nouveau_display_reader read, void *context,
	struct nouveau_display_snapshot *s)
{
	if(s==NULL) return false;
	*s=(struct nouveau_display_snapshot){0};
	if(read==NULL) return false;
	READ(0x22448,s->head_count); READ(0x612004,s->capabilities);
	READ(0x100ce0,s->vidmem);
	READ(0x1704,s->bar1_instance); READ(0x1714,s->bar3_instance);
	READ(0x1710,s->bar_status);
	if(!s->head_count || s->head_count>NOUVEAU_DISPLAY_HEADS || s->capabilities==UINT32_MAX) return false;
	s->head_mask=s->capabilities&15; s->sor_mask=(s->capabilities>>8)&255;
	if(!s->head_mask || (s->head_mask&~((1U<<s->head_count)-1))) return false;
	READ(0x610010,s->instance); READ(0x610490,s->core_control);
	/* Pascal moved these descriptors from 0x61049x to 0x61149x. */
	READ(0x611494,s->core_push); READ(0x611498,s->core_limit); READ(0x61149c,s->core_target);
	READ(0x640000,s->user_put); READ(0x610088,s->interrupt);
	READ(0x6100ac,s->supervisor); READ(0x6100b0,s->supervisor_enable);
	READ(0x6101d0,s->supervisor_control); READ(0x6194e8,s->bios_handoff);
	for(unsigned i=0;i<4;i++) {
		READ(0x612308+i*0x100,s->route[i][0]);
		READ(0x612388+i*0x100,s->route[i][1]);
	}
	for(unsigned i=0;i<NOUVEAU_DISPLAY_SORS;i++) if(s->sor_mask&(1U<<i)) {
		READ(0x640200+i*0x20,s->sors[i].arm);
		READ(0x660200+i*0x20,s->sors[i].asy);
	}
	for(unsigned i=0;i<s->head_count;i++) if(s->head_mask&(1U<<i)) {
		if(!bank(read,context,0x640000+i*0x300,&s->heads[i].arm) ||
		    !bank(read,context,0x660000+i*0x300,&s->heads[i].asy)) return false;
		uint32_t x,y;
		/* VLINE read latches HLINE, as nv50_head_rgpos requires. */
		READ(0x616340+i*0x800,y); READ(0x616344+i*0x800,x);
		s->heads[i].scan_x=x&65535; s->heads[i].scan_y=y&65535;
		READ(0x6101d4+i*0x800,s->heads[i].supervisor_mask);
		READ(0x61410c+i*0x800,s->pll_control[i]);
		struct nouveau_base_snapshot *b=&s->base[i];
		READ(0x6104a0+i*0x10,b->control); READ(0x6114a4+i*0x10,b->push);
		READ(0x641084+i*0x1000,b->present);
		for(unsigned j=0;j<2;j++) {
			READ(0x6410c0+i*0x1000+j*4,b->dma[j]);
			for(unsigned k=0;k<2;k++) {
				struct nouveau_base_surface *surf=k?&b->asy[j]:&b->arm[j];
				uint32_t addr=(k?0x661400:0x641400)+i*0x1000+j*0x20;
				READ(addr,surf->offset_lo); READ(addr+4,surf->offset_hi);
				READ(addr+8,surf->size); READ(addr+12,surf->storage); READ(addr+16,surf->params);
			}
		}
	}
	return true;
}
#undef READ

static bool one_bit(uint32_t value) { return value && !(value&(value-1)); }
static unsigned first_bit(uint32_t value)
{
	unsigned bit=0; while(!(value&1)) { value>>=1; bit++; } return bit;
}
static uint32_t xy(unsigned x,unsigned y) { return x|(y<<16); }
static void header(struct nouveau_display_plan *p,unsigned method,unsigned count)
{
	/* NV507C packet: METHOD_COUNT[27:18], byte METHOD_OFFSET[11:2]. */
	p->words[p->count++]=(count<<18)|method;
}
static void data(struct nouveau_display_plan *p,uint32_t value) { p->words[p->count++]=value; }

enum nouveau_display_plan_status nouveau_display_plan(
	const struct nouveau_display_snapshot *s,const struct nouveau_dcb_output *o,
	const struct nouveau_edid_mode *m,const struct os_framebuffer_info *fb,
	struct nouveau_display_plan *p)
{
	if(p==NULL) return NOUVEAU_PLAN_BAD_TIMING;
	*p=(struct nouveau_display_plan){0};
	if(s==NULL || o==NULL || m==NULL || fb==NULL) return NOUVEAU_PLAN_BAD_TIMING;
	if(!s->head_count || s->head_count>NOUVEAU_DISPLAY_HEADS ||
	    (s->head_mask&~((1U<<s->head_count)-1))) return NOUVEAU_PLAN_NO_HEAD;
	if(!m->detailed || !m->clock_khz) return NOUVEAU_PLAN_NO_TIMING;
	if(m->interlaced || !m->width || !m->height ||
	    m->width>m->hsync_start || m->hsync_start>=m->hsync_end || m->hsync_end>m->htotal ||
	    m->height>m->vsync_start || m->vsync_start>=m->vsync_end || m->vsync_end>m->vtotal ||
	    m->htotal>0x7fff || m->vtotal>0x7fff || m->clock_khz>0x7fffffffU/1000)
		return NOUVEAU_PLAN_BAD_TIMING;
	if(o->type!=2 || o->location || o->link<1 || o->link>3)
		return NOUVEAU_PLAN_UNSUPPORTED_OUTPUT;
	if(!one_bit(o->or_mask) || (o->or_mask&~15)) return NOUVEAU_PLAN_NO_ROUTE;
	unsigned physical=first_bit(o->or_mask), sor=0, links=0;
	for(unsigned i=0;i<2;i++) if(o->link&(1U<<i)) {
		uint32_t route=s->route[physical][i];
		unsigned id=route&15, link=(route>>4)&1;
		if(!id || id>NOUVEAU_DISPLAY_SORS || !(s->sor_mask&(1U<<(id-1)))) return NOUVEAU_PLAN_NO_ROUTE;
		if(sor && sor!=id) return NOUVEAU_PLAN_AMBIGUOUS_ROUTE;
		if(o->link==3 && link!=i) return NOUVEAU_PLAN_AMBIGUOUS_ROUTE;
		sor=id; links|=1U<<link;
	}
	sor--;
	uint32_t control=s->sors[sor].arm;
	unsigned protocol=(control>>8)&15;
	if((links==1 && protocol!=1) || (links==2 && protocol!=2) || (links==3 && protocol!=5))
		return NOUVEAU_PLAN_NO_ROUTE;
	unsigned heads=control&15;
	if(!one_bit(heads)) return NOUVEAU_PLAN_AMBIGUOUS_ROUTE;
	if(!(heads&s->head_mask&o->heads)) return NOUVEAU_PLAN_NO_HEAD;
	unsigned head=first_bit(heads);
	if(head>=NOUVEAU_DISPLAY_HEADS || !s->heads[head].arm.timing_valid) return NOUVEAU_PLAN_NO_HEAD;
	if(fb->width!=m->width || fb->height!=m->height || fb->bits_per_pixel!=32 ||
	    fb->red_mask_size!=8 || fb->red_mask_shift!=16 ||
	    fb->green_mask_size!=8 || fb->green_mask_shift!=8 ||
	    fb->blue_mask_size!=8 || fb->blue_mask_shift!=0 ||
	    fb->pitch<fb->width*4 || fb->height>UINT64_MAX/fb->pitch || fb->size<fb->pitch*fb->height ||
	    s->heads[head].arm.width!=fb->width || s->heads[head].arm.height!=fb->height)
		return NOUVEAU_PLAN_FRAMEBUFFER_MISMATCH;
	p->core_class=NOUVEAU_GP102_CORE_CLASS; p->head=head; p->sor=sor;
	p->link=links; p->connector=o->connector; p->mode=*m;
	unsigned off=head*0x300;
	unsigned he=m->hsync_end-m->hsync_start-1, ve=m->vsync_end-m->vsync_start-1;
	unsigned hb=m->htotal-m->hsync_start-1, vb=m->vtotal-m->vsync_start-1;
	header(p,0x410+off,6); data(p,0);
	data(p,xy(m->htotal,m->vtotal)); data(p,xy(he,ve));
	data(p,xy(hb,vb)); data(p,xy(hb+m->width,vb+m->height)); data(p,1);
	header(p,0x42c+off,1); data(p,0);
	header(p,0x450+off,3); data(p,m->clock_khz*1000); data(p,0x200000); data(p,m->clock_khz*1000);
	header(p,0x404+off,2);
	data(p,(5U<<6)|(m->hsync_positive?0:8)|(m->vsync_positive?0:16));
	data(p,0x31ec6000U|(head<<25));
	header(p,0x494+off,1); data(p,0);
	header(p,0x4b8+off,1); data(p,xy(m->width,m->height));
	header(p,0x4c0+off,3);
	data(p,xy(m->width,m->height)); data(p,xy(m->width,m->height)); data(p,xy(m->width,m->height));
	return NOUVEAU_PLAN_OK;
}


bool nouveau_display_surface_plan(unsigned head,uint64_t address,uint64_t vram,
	uint32_t handle,const struct os_framebuffer_info *fb,struct nouveau_surface_plan *plan)
{
	if(!plan) return false;
	*plan=(struct nouveau_surface_plan){0};
	if(!fb || head>=4 || !handle || !fb->width || fb->width>65535 ||
	    !fb->height || fb->height>65535 || fb->bits_per_pixel!=32 ||
	    fb->red_mask_size!=8 || fb->green_mask_size!=8 || fb->blue_mask_size!=8 ||
	    fb->green_mask_shift!=8 ||
	    !((fb->red_mask_shift==16 && fb->blue_mask_shift==0) ||
	      (fb->red_mask_shift==0 && fb->blue_mask_shift==16)) ||
	    (address&255) || !fb->pitch || (fb->pitch&255) || fb->pitch>0x1fff00 ||
	    fb->pitch<fb->width*4 || vram>(UINT64_C(1)<<40) || address>=vram ||
	    fb->height>(vram-address)/fb->pitch || fb->size<fb->pitch*fb->height) return false;
	unsigned base=head*0x300;
	uint32_t format=fb->red_mask_shift==16?0xcf:0xd5;
	/* head907d_core_set: offset, four consecutive surface methods, viewport. */
	uint32_t words[]={ (1U<<18)|(0x460+base),(uint32_t)(address>>8),
	    (4U<<18)|(0x468+base),(uint32_t)(fb->width|(fb->height<<16)),
	    0x1000000|(uint32_t)fb->pitch,format<<8,handle,
	    (1U<<18)|(0x4b0+base),0 };
	for(unsigned i=0;i<NOUVEAU_SURFACE_PLAN_WORDS;i++) plan->words[i]=words[i];
	return true;
}
