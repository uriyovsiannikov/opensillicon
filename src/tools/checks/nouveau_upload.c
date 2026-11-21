/* Upload/readback/rollback with side-effecting failed writes and ownership loss. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_upload.h"
static uint32_t memory[NOUVEAU_UPLOAD_LIMIT/4],backup[NOUVEAU_UPLOAD_LIMIT/4],image[NOUVEAU_UPLOAD_LIMIT/4];
struct model { uint64_t base;unsigned words,reads,writes,fail_read,fail_write,corrupt_read,drop_write;bool owns; };
static bool owns(void *ctx,uint64_t base,uint64_t size)
{struct model *m=ctx;assert(base==m->base&&size==m->words*4);return m->owns;}
static bool read32(void *ctx,uint64_t address,uint32_t *value)
{
	struct model *m=ctx;assert(m->owns&&address>=m->base&&!(address&3));
	unsigned i=(address-m->base)/4;assert(i<m->words);m->reads++;
	if(m->reads==m->fail_read)return false;
	*value=memory[i];if(m->reads==m->corrupt_read)*value^=1;return true;
}
static bool write32(void *ctx,uint64_t address,uint32_t value)
{
	struct model *m=ctx;assert(m->owns&&address>=m->base&&!(address&3));
	unsigned i=(address-m->base)/4;assert(i<m->words);m->writes++;
	if(m->writes!=m->drop_write)memory[i]=value;
	return m->writes!=m->fail_write;
}
static struct nouveau_vram_pool pool;
static struct nouveau_upload_io io;
static struct model m;
static struct nouveau_vram_allocation allocation;
static struct nouveau_upload u;
static void setup(unsigned words)
{
	pool=(struct nouveau_vram_pool){0};allocation=(struct nouveau_vram_allocation){0};u=(struct nouveau_upload){0};
	assert(nouveau_vram_pool_init(&pool,0x1000000,NULL,0));
	assert(nouveau_vram_pool_alloc(&pool,words*4,65536,&allocation));
	struct nouveau_vram_range range;assert(nouveau_vram_pool_query(allocation,&range));
	m=(struct model){.base=range.start,.words=words,.owns=true};
	io=(struct nouveau_upload_io){&m,read32,write32,owns};
	for(unsigned i=0;i<words;i++){memory[i]=i^0x12345678;image[i]=i^0x87654321;backup[i]=0;}
}
static void original(void)
{for(unsigned i=0;i<m.words;i++)assert(memory[i]==(i^0x12345678));}
static void saved(void)
{assert(nouveau_upload_save(&u,&io,allocation,backup,m.words*4)==NOUVEAU_UPLOAD_OK);}
static void restore_finish(void)
{
	m.owns=true;m.fail_read=m.fail_write=m.drop_write=m.corrupt_read=0;
	if(u.state!=NOUVEAU_UPLOAD_RESTORED)assert(nouveau_upload_restore(&u)==NOUVEAU_UPLOAD_OK);
	original();assert(nouveau_upload_finish(&u));
	struct nouveau_vram_range range;assert(!nouveau_vram_pool_query(allocation,&range));
}
int main(void)
{
	setup(16384);saved();assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_OK);
	assert(!memcmp(memory,image,sizeof(image))&&u.state==NOUVEAU_UPLOAD_VERIFIED);
	assert(!nouveau_upload_finish(&u));assert(nouveau_upload_pin(&u));
	assert(!nouveau_upload_pin(&u));
	assert(nouveau_upload_restore(&u)==NOUVEAU_UPLOAD_INVALID&&!nouveau_upload_finish(&u));
	m.owns=false;assert(!nouveau_upload_unpin(&u));m.owns=true;
	assert(nouveau_upload_unpin(&u));restore_finish();
	/* Exhaust all read/write fault positions on a small transaction. */
	for(unsigned i=1;i<=16;i++) {
		setup(16);m.fail_read=i;
		assert(nouveau_upload_save(&u,&io,allocation,backup,64)==NOUVEAU_UPLOAD_READ_FAILED);
		assert(u.state==NOUVEAU_UPLOAD_EMPTY&&!m.writes);original();
		assert(nouveau_vram_pool_free(&allocation));
		setup(16);saved();m.fail_write=i;
		assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_FAILED_RESTORED);restore_finish();
		setup(16);saved();m.fail_read=m.reads+i;
		assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_FAILED_RESTORED);restore_finish();
		setup(16);saved();m.corrupt_read=m.reads+i;
		assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_FAILED_RESTORED);restore_finish();
		setup(16);saved();m.drop_write=i;
		assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_FAILED_RESTORED);restore_finish();
		/* Persistent restore failures quarantine, preserve backup/allocation,
		 * and allow a later explicit restore after transport recovery. */
		setup(16);saved();assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_OK);
		m.fail_write=m.writes+i;
		assert(nouveau_upload_restore(&u)==NOUVEAU_UPLOAD_RESTORE_FAILED);
		assert(u.state==NOUVEAU_UPLOAD_QUARANTINED&&!nouveau_upload_finish(&u));restore_finish();
		setup(16);saved();assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_OK);
		m.fail_read=m.reads+i;
		assert(nouveau_upload_restore(&u)==NOUVEAU_UPLOAD_RESTORE_FAILED);restore_finish();
	}
	setup(16);saved();m.fail_write=1;m.fail_read=m.reads+1;
	assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_RESTORE_FAILED);
	assert(u.state==NOUVEAU_UPLOAD_QUARANTINED&&!nouveau_upload_finish(&u));restore_finish();
	setup(16);saved();assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_OK);m.owns=false;
	assert(nouveau_upload_restore(&u)==NOUVEAU_UPLOAD_NOT_OWNED&&u.state==NOUVEAU_UPLOAD_QUARANTINED);
	assert(!nouveau_upload_finish(&u));restore_finish();
	setup(16);m.owns=false;
	assert(nouveau_upload_save(&u,&io,allocation,backup,64)==NOUVEAU_UPLOAD_NOT_OWNED&&!m.reads&&!m.writes);
	m.owns=true;saved();m.owns=false;
	assert(nouveau_upload_image(&u,image)==NOUVEAU_UPLOAD_NOT_OWNED&&!m.writes);
	m.owns=true;assert(nouveau_upload_finish(&u));
	setup(16);saved();assert(nouveau_upload_image(&u,backup)==NOUVEAU_UPLOAD_INVALID&&!m.writes);
	assert(nouveau_upload_image(&u,backup+1)==NOUVEAU_UPLOAD_INVALID&&!m.writes);
	assert(nouveau_upload_finish(&u));
	setup(16);assert(nouveau_upload_save(&u,&io,allocation,backup,60)==NOUVEAU_UPLOAD_INVALID);
	assert(nouveau_upload_save(&u,&io,allocation,backup,65540)==NOUVEAU_UPLOAD_INVALID);
	assert(nouveau_upload_save(&u,&io,allocation,(uint32_t *)((char *)backup+1),64)==NOUVEAU_UPLOAD_INVALID);
	assert(nouveau_vram_pool_free(&allocation));
	puts("NOUVEAU_UPLOAD_PASS: 64KiB backup/upload/readback/restore, all word failures, corrupted/lost writes, pinning, ownership checks and retryable quarantine");
}
