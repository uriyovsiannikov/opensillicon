/* Range/handle accounting independent of GPU MMIO. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_vram_pool.h"
static uint32_t seed=0x137;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static bool overlaps(struct nouveau_vram_range a,struct nouveau_vram_range b)
{return a.start<b.start+b.size&&b.start<a.start+a.size;}
int main(void)
{
	struct nouveau_vram_pool pool={0};
	struct nouveau_vram_range excluded[]={{0,8294400},{0x20e0000,0x100000}};
	assert(nouveau_vram_pool_init(&pool,UINT64_C(0x100000000),excluded,2));
	assert(!nouveau_vram_pool_init(&pool,UINT64_C(0x100000000),NULL,0));
	struct nouveau_vram_allocation a={0},b={0};struct nouveau_vram_range r;
	assert(nouveau_vram_pool_alloc(&pool,65536,65536,&a));
	assert(nouveau_vram_pool_query(a,&r)&&r.start==0x7f0000&&r.size==65536);
	assert(!nouveau_vram_pool_alloc(&pool,65536,65536,&a));
	assert(!nouveau_vram_pool_reserve(&pool,r.start+1,1));
	assert(nouveau_vram_pool_alloc(&pool,65536,65536,&b));
	assert(nouveau_vram_pool_query(b,&r)&&r.start==0x800000);
	struct nouveau_vram_allocation stale=a;
	assert(nouveau_vram_pool_free(&a)&&!a.generation);
	assert(!nouveau_vram_pool_free(&a));
	assert(nouveau_vram_pool_alloc(&pool,65536,65536,&a));
	assert(a.slot==stale.slot&&a.generation!=stale.generation);
	assert(!nouveau_vram_pool_query(stale,&r)&&!nouveau_vram_pool_free(&stale));
	assert(nouveau_vram_pool_free(&a)&&nouveau_vram_pool_free(&b));
	for(unsigned i=0;i<20000;i++) {
		struct nouveau_vram_allocation handles[12]={{0}};
		for(unsigned j=0;j<12;j++) {
			uint64_t alignment=UINT64_C(1)<<(random32()%20);
			uint64_t size=1+(random32()%0x100000);
			assert(nouveau_vram_pool_alloc(&pool,size,alignment,&handles[j]));
			assert(nouveau_vram_pool_query(handles[j],&r)&&!(r.start&(alignment-1)));
			for(unsigned k=0;k<NOUVEAU_VRAM_RANGES;k++) {
				if(k==handles[j].slot)continue;
				struct nouveau_vram_slot *s=&pool.slots[k];
				if(s->reserved||s->generation)assert(!overlaps(r,s->range));
			}
		}
		for(unsigned j=0;j<12;j++)assert(nouveau_vram_pool_free(&handles[j]));
	}
	assert(!nouveau_vram_pool_alloc(&pool,1,3,&a));
	assert(!nouveau_vram_pool_alloc(&pool,0,1,&a));
	assert(!nouveau_vram_pool_alloc(&pool,UINT64_MAX,1,&a));
	assert(!nouveau_vram_pool_reserve(&pool,UINT64_MAX,1));
	struct nouveau_vram_pool bad={0},copy=bad;
	struct nouveau_vram_range invalid={UINT64_MAX,10};
	assert(!nouveau_vram_pool_init(&bad,UINT64_C(0x100000000),&invalid,1));
	assert(!memcmp(&bad,&copy,sizeof(bad)));
	struct nouveau_vram_pool tiny={0};
	assert(nouveau_vram_pool_init(&tiny,0x150000,NULL,0));
	assert(nouveau_vram_pool_alloc(&tiny,65536,65536,&a));
	assert(!nouveau_vram_pool_alloc(&tiny,1,1,&b));
	assert(nouveau_vram_pool_free(&a));
	pool.next_generation=UINT64_MAX;
	assert(nouveau_vram_pool_alloc(&pool,1,1,&a));
	assert(!nouveau_vram_pool_alloc(&pool,1,1,&b));
	assert(nouveau_vram_pool_free(&a));
	struct nouveau_vram_pool full={0};
	assert(nouveau_vram_pool_init(&full,UINT64_C(0x100000000),NULL,0));
	for(unsigned i=2;i<32;i++)assert(nouveau_vram_pool_reserve(&full,0x1000000+i*65536,65536));
	assert(!nouveau_vram_pool_alloc(&full,1,1,&a));
	puts("NOUVEAU_VRAM_POOL_PASS: reservations, scanout/VGA/VBIOS exclusion, alignment, stale handles, exhaustion, generation wrap, 240000 randomized allocations");
}
