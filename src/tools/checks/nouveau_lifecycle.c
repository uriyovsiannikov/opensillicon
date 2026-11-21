/* Integrate range ownership, reversible upload and quarantined channel lifetime.
 * All hardware is modeled; this does not grant ownership on a real GPU. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_upload.h"
#include "kernel/nouveau_channel.h"
struct fixture {
	struct nouveau_vram_pool pool;
	struct nouveau_vram_allocation allocation;
	struct nouveau_upload upload;
	struct nouveau_upload_io memory_io;
	struct nouveau_channel channel;
	struct nouveau_channel_io channel_io;
	uint32_t memory[16],backup[16],image[16],control,put;
	uint64_t address,time;
	unsigned writes,fail_write;
	bool owned,busy;
};
static bool owns(void *ctx,uint64_t address,uint64_t size)
{struct fixture *f=ctx;return f->owned&&address==f->address&&size==64;}
static bool read_memory(void *ctx,uint64_t address,uint32_t *value)
{
	struct fixture *f=ctx;assert(f->owned&&address>=f->address&&address<f->address+64&&!(address&3));
	*value=f->memory[(address-f->address)/4];return true;
}
static bool write_memory(void *ctx,uint64_t address,uint32_t value)
{
	struct fixture *f=ctx;assert(f->owned&&address>=f->address&&address<f->address+64&&!(address&3));
	/* A live/quarantined consumer must never see its instance overwritten. */
	assert(f->upload.state!=NOUVEAU_UPLOAD_PINNED);
	f->memory[(address-f->address)/4]=value;return true;
}
static bool read_register(void *ctx,uint32_t reg,uint32_t *value)
{
	struct fixture *f=ctx;
	if(reg==0x610490)*value=f->control|(f->busy?0x20000:0);
	else {assert(reg==0x640000);*value=f->put;}
	return true;
}
static bool write_register(void *ctx,uint32_t reg,uint32_t value)
{
	struct fixture *f=ctx;assert(f->upload.state==NOUVEAU_UPLOAD_PINNED);
	if(reg==0x610490)f->control=value&~0x801e0000U; /* activity bits are read-only */
	else if(reg==0x640000)f->put=value;
	else assert(reg==0x611494||reg==0x611498||reg==0x61149c);
	f->writes++;return f->writes!=f->fail_write;
}
static bool time_us(void *ctx,uint64_t *value){*value=((struct fixture *)ctx)->time;return true;}
static bool delay_us(void *ctx,unsigned amount){((struct fixture *)ctx)->time+=amount;return true;}
static bool acquire(void *ctx){return nouveau_upload_pin(&((struct fixture *)ctx)->upload);}
static bool release(void *ctx)
{
	struct fixture *f=ctx;assert(!(f->control&3)&&!f->busy);
	return nouveau_upload_unpin(&f->upload);
}
static void setup(struct fixture *f)
{
	memset(f,0,sizeof(*f));f->owned=true;
	assert(nouveau_vram_pool_init(&f->pool,0x1000000,NULL,0));
	assert(nouveau_vram_pool_alloc(&f->pool,64,65536,&f->allocation));
	struct nouveau_vram_range range;assert(nouveau_vram_pool_query(f->allocation,&range));f->address=range.start;
	f->memory_io=(struct nouveau_upload_io){f,read_memory,write_memory,owns};
	f->channel_io=(struct nouveau_channel_io){f,read_register,write_register,time_us,delay_us,acquire,release};
	for(unsigned i=0;i<16;i++){f->memory[i]=i^0x13579bdf;f->image[i]=i^0x2468ace0;}
	assert(nouveau_upload_save(&f->upload,&f->memory_io,f->allocation,f->backup,64)==NOUVEAU_UPLOAD_OK);
	assert(nouveau_upload_image(&f->upload,f->image)==NOUVEAU_UPLOAD_OK);
	assert(nouveau_channel_prepare(&f->channel,0,0x3403,132));
}
static void restore_finish(struct fixture *f)
{
	assert(nouveau_upload_restore(&f->upload)==NOUVEAU_UPLOAD_OK);
	for(unsigned i=0;i<16;i++)assert(f->memory[i]==(i^0x13579bdf));
	assert(nouveau_upload_finish(&f->upload));
	struct nouveau_vram_range range;assert(!nouveau_vram_pool_query(f->allocation,&range));
}
int main(void)
{
	struct fixture f;
	for(unsigned fail=0;fail<=6;fail++) {
		setup(&f);f.fail_write=fail;
		enum nouveau_channel_status status=nouveau_channel_start(&f.channel,&f.channel_io);
		assert(status==(fail?NOUVEAU_CHANNEL_IO_FAILED:NOUVEAU_CHANNEL_OK));
		assert(f.upload.state==NOUVEAU_UPLOAD_PINNED);
		assert(nouveau_upload_restore(&f.upload)==NOUVEAU_UPLOAD_INVALID);
		assert(!nouveau_upload_finish(&f.upload));
		assert(nouveau_channel_release(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_INVALID);
		if(!fail)assert(nouveau_channel_kick(&f.channel,&f.channel_io,132)==NOUVEAU_CHANNEL_OK);
		f.fail_write=0;f.busy=true;
		assert(nouveau_channel_stop(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_TIMEOUT);
		assert(f.upload.state==NOUVEAU_UPLOAD_PINNED&&!nouveau_upload_finish(&f.upload));
		f.busy=false;
		assert(nouveau_channel_stop(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_OK);
		assert(nouveau_channel_release(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_OK);
		assert(f.upload.state==NOUVEAU_UPLOAD_VERIFIED);restore_finish(&f);
	}
	setup(&f);assert(nouveau_channel_start(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_OK);
	f.fail_write=f.writes+1;
	assert(nouveau_channel_kick(&f.channel,&f.channel_io,132)==NOUVEAU_CHANNEL_IO_FAILED);
	assert(f.upload.state==NOUVEAU_UPLOAD_PINNED);
	f.fail_write=0;
	assert(nouveau_channel_stop(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_OK);
	/* Loss of ownership cannot silently complete unpin/free. */
	f.owned=false;
	assert(nouveau_channel_release(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_RESOURCE_FAILED);
	assert(f.upload.state==NOUVEAU_UPLOAD_PINNED&&!nouveau_upload_finish(&f.upload));
	f.owned=true;
	assert(nouveau_channel_release(&f.channel,&f.channel_io)==NOUVEAU_CHANNEL_OK);restore_finish(&f);
	puts("NOUVEAU_LIFECYCLE_PASS: upload pin -> channel start/kick/quarantine -> verified stop/release -> VRAM restore/free, with all start-write failures and ownership loss");
}
