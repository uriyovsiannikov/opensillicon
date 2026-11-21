/* SPDX-License-Identifier: BSD-2-Clause
 * Native bounded range allocator. No MMIO or GPU memory access. */
#include "kernel/nouveau_vram_pool.h"
#define VRAM_MAX (UINT64_C(1)<<40)
static bool range_valid(uint64_t capacity,uint64_t start,uint64_t size)
{
	return size && start<capacity && size<=capacity-start;
}
static bool overlap(uint64_t start,uint64_t size,const struct nouveau_vram_range *r)
{
	return start<r->start+r->size && r->start<start+size;
}
static int empty_slot(const struct nouveau_vram_pool *p)
{
	for(unsigned i=0;i<NOUVEAU_VRAM_RANGES;i++)
		if(!p->slots[i].reserved && !p->slots[i].generation) return (int)i;
	return -1;
}
bool nouveau_vram_pool_reserve(struct nouveau_vram_pool *p,uint64_t start,uint64_t size)
{
	if(!p || !p->initialized || !range_valid(p->size,start,size)) return false;
	/* Reservations can overlap, but may never cover a live allocation. */
	for(unsigned i=0;i<NOUVEAU_VRAM_RANGES;i++)
		if(p->slots[i].generation && overlap(start,size,&p->slots[i].range)) return false;
	int slot=empty_slot(p);
	if(slot<0) return false;
	p->slots[slot]=(struct nouveau_vram_slot){.range={start,size},.reserved=true};
	return true;
}
bool nouveau_vram_pool_init(struct nouveau_vram_pool *p,uint64_t size,
	const struct nouveau_vram_range *excluded,size_t count)
{
	if(!p || p->initialized || size<0x150000 || size>VRAM_MAX ||
	    count>NOUVEAU_VRAM_RANGES-2 || (count && !excluded)) return false;
	for(size_t i=0;i<count;i++)
		if(!range_valid(size,excluded[i].start,excluded[i].size)) return false;
	struct nouveau_vram_pool fresh={.size=size,.next_generation=1,.initialized=true};
	/* Init is atomic on failure: do not expose a partially reserved pool. */
	if(!nouveau_vram_pool_reserve(&fresh,0,0x40000) ||
	    !nouveau_vram_pool_reserve(&fresh,size-0x100000,0x100000)) return false;
	for(size_t i=0;i<count;i++)
		if(!nouveau_vram_pool_reserve(&fresh,excluded[i].start,excluded[i].size)) return false;
	*p=fresh;return true;
}
bool nouveau_vram_pool_alloc(struct nouveau_vram_pool *p,uint64_t size,
	uint64_t alignment,struct nouveau_vram_allocation *allocation)
{
	if(!p || !p->initialized || !allocation || allocation->generation ||
	    !size || size>p->size || !alignment || alignment>VRAM_MAX ||
	    (alignment&(alignment-1)) || !p->next_generation) return false;
	int slot=empty_slot(p);
	if(slot<0) return false;
	uint64_t start=0;
	/* At most one jump past each occupied range, regardless of VRAM size. */
	for(unsigned pass=0;pass<=NOUVEAU_VRAM_RANGES;pass++) {
		if(start>UINT64_MAX-(alignment-1)) return false;
		start=(start+alignment-1)&~(alignment-1);
		if(!range_valid(p->size,start,size)) return false;
		uint64_t next=start;
		for(unsigned i=0;i<NOUVEAU_VRAM_RANGES;i++) {
			const struct nouveau_vram_slot *s=&p->slots[i];
			if((s->reserved || s->generation) && overlap(start,size,&s->range) &&
			    s->range.start+s->range.size>next) next=s->range.start+s->range.size;
		}
		if(next!=start) {start=next;continue;}
		uint64_t generation=p->next_generation++;
		p->slots[slot]=(struct nouveau_vram_slot){.range={start,size},.generation=generation};
		*allocation=(struct nouveau_vram_allocation){.pool=p,.generation=generation,.slot=(uint8_t)slot};
		return true;
	}
	return false;
}
bool nouveau_vram_pool_query(struct nouveau_vram_allocation a,struct nouveau_vram_range *range)
{
	if(!range || !a.pool || !a.pool->initialized || a.slot>=NOUVEAU_VRAM_RANGES ||
	    !a.generation || a.pool->slots[a.slot].generation!=a.generation ||
	    a.pool->slots[a.slot].reserved) return false;
	*range=a.pool->slots[a.slot].range;return true;
}
bool nouveau_vram_pool_free(struct nouveau_vram_allocation *a)
{
	struct nouveau_vram_range range;
	if(!a || !nouveau_vram_pool_query(*a,&range)) return false;
	a->pool->slots[a->slot]=(struct nouveau_vram_slot){0};
	*a=(struct nouveau_vram_allocation){0};return true;
}
