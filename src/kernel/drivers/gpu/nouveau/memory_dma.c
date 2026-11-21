/* SPDX-License-Identifier: BSD-2-Clause
 * Native DMA allocation lifecycle for CPU-owned display staging buffers. */
#include "kernel/nouveau_memory.h"

bool nouveau_display_staging_release(struct nouveau_display_staging *s)
{
	if(!s) return false;
	bool ok=true;
	if(s->push.generation) {
		if(dma_free(s->push)) s->push=(struct dma_buffer){0};else ok=false;
	}
	if(s->instance.generation) {
		if(dma_free(s->instance)) s->instance=(struct dma_buffer){0};else ok=false;
	}
	if(ok) s->push_descriptor=0;
	return ok;
}

static bool valid(struct dma_buffer buffer,size_t size,size_t align,struct dma_buffer_info *info)
{
	return dma_buffer_query(buffer,info) && info->owner==DMA_OWNER_CPU &&
	    info->cpu_virtual_address && !((uintptr_t)info->cpu_virtual_address&3) &&
	    info->requested_size==size && info->allocated_size>=size &&
	    !(info->device_address&(align-1)) &&
	    info->device_address<(UINT64_C(1)<<40) &&
	    size<=(UINT64_C(1)<<40)-info->device_address;
}

enum nouveau_staging_status nouveau_display_staging_create(uint64_t vram,
	const struct nouveau_display_plan *p,struct nouveau_display_staging *s)
{
	/* Refuse to overwrite live handles. Callers must initialize the struct. */
	if(!s || s->instance.generation || s->push.generation || !p ||
	    p->core_class!=NOUVEAU_GP102_CORE_CLASS || p->count!=NOUVEAU_DISPLAY_PLAN_WORDS ||
	    p->head>=4 || vram<0x150000 || vram>(UINT64_C(1)<<40) || (vram&255))
		return NOUVEAU_STAGING_INVALID;
	s->push_descriptor=0;
	if(!dma_identity_addressing_available()) return NOUVEAU_STAGING_UNAVAILABLE;
	struct dma_buffer_info instance,push;
	if(!dma_alloc(NOUVEAU_INSTANCE_SIZE,NOUVEAU_INSTANCE_SIZE,40,&s->instance) ||
	    !valid(s->instance,NOUVEAU_INSTANCE_SIZE,NOUVEAU_INSTANCE_SIZE,&instance) ||
	    !dma_alloc(4096,4096,40,&s->push) || !valid(s->push,4096,4096,&push)) goto fail;
	if(!nouveau_instance_prepare(instance.cpu_virtual_address,NOUVEAU_INSTANCE_SIZE,vram,p->head)) goto fail;
	uint32_t *words=push.cpu_virtual_address;
	for(unsigned i=0;i<4096/4;i++) words[i]=0;
	for(unsigned i=0;i<p->count;i++) words[i]=p->words[i];
	/* NV50 display push target HOST=3; this is not the instance target VRAM=9. */
	s->push_descriptor=(uint32_t)(push.device_address>>8)|3;
	return NOUVEAU_STAGING_OK;
fail:
	return nouveau_display_staging_release(s)?NOUVEAU_STAGING_ALLOCATION_FAILED:
	    NOUVEAU_STAGING_CLEANUP_FAILED;
}
