/* GF119 firmware handoff, instance binding and rollback ownership. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_engine.h"
static uint32_t memory[16384],backup[16384],image[16384];
struct model {
	uint32_t instance,firmware,supervisor,masks[3],controls[17],caps[20];
	unsigned reads,writes,fail_read,fail_write,wrong_read,delays;
	uint64_t now;
	bool stuck,stalled,backward,owner,fail_memory;
	struct nouveau_engine engine;
	struct nouveau_engine_io io;
	struct nouveau_vram_pool pool;
	struct nouveau_vram_allocation allocation;
	struct nouveau_upload upload;
	struct nouveau_upload_io upload_io;
};
static int cap_index(uint32_t reg,bool source)
{
	for(unsigned i=0;i<4;i++)for(unsigned j=0;j<3;j++)
		if(reg==(source?0x616104U:0x6101b4U)+i*0x800+j*4)return i*3+j;
	for(unsigned i=0;i<8;i++)if(reg==(source?0x61c000U:0x6301c4U)+i*0x800)return 12+i;
	return -1;
}
static bool read32(void *ctx,uint32_t reg,uint32_t *value)
{
	struct model *m=ctx;m->reads++;
	if(m->reads==m->fail_read)return false;
	if(reg==0x22448)*value=4;
	else if(reg==0x612004)*value=0x30f;
	else if(cap_index(reg,false)>=0)*value=m->caps[cap_index(reg,false)];
	else if(cap_index(reg,true)>=0)*value=0x1000+(unsigned)cap_index(reg,true);
	else if(reg==0x610010)*value=m->instance;
	else if(reg==0x6194e8)*value=m->firmware;
	else if(reg==0x6100ac)*value=m->supervisor;
	else if(reg==0x610090)*value=m->masks[0];
	else if(reg==0x6100a0)*value=m->masks[1];
	else if(reg==0x6100b0)*value=m->masks[2];
	else {assert(reg>=0x610490&&reg<=0x610590&&!((reg-0x610490)&15));*value=m->controls[(reg-0x610490)/16];}
	if(m->reads==m->wrong_read)*value^=1;
	return true;
}
static bool write32(void *ctx,uint32_t reg,uint32_t value)
{
	struct model *m=ctx;m->writes++;
	if(cap_index(reg,false)>=0)m->caps[cap_index(reg,false)]=value;
	else if(reg==0x610010) {
		assert(value==1||m->upload.state==NOUVEAU_UPLOAD_PINNED);m->instance=value;
	}else if(reg==0x6194e8) {
		/* Firmware ack bit is read-only, do not emulate it as writable. */
		m->firmware=(m->firmware&2)|(value&~2U);
		if(!(value&1)&&!m->stuck)m->firmware&=~2U;
		if(value&1) {
			/* Driver must restore bytes before returning control to firmware. */
			if(m->upload.state!=NOUVEAU_UPLOAD_EMPTY)assert(m->upload.state==NOUVEAU_UPLOAD_RESTORED);
			m->owner=false;
		}
	}else if(reg==0x6100ac){assert(value==0x100);m->supervisor&=~value;}
	else if(reg==0x610090)m->masks[0]=value;
	else if(reg==0x6100a0)m->masks[1]=value;
	else {assert(reg==0x6100b0);m->masks[2]=value;}
	return m->writes!=m->fail_write;
}
static bool time_us(void *ctx,uint64_t *value)
{struct model *m=ctx;*value=m->backward&&m->delays?0:m->now;return true;}
static bool delay_us(void *ctx,unsigned value)
{struct model *m=ctx;assert(value==100);m->delays++;if(!m->stalled)m->now+=value;return true;}
static bool owns(void *ctx,uint64_t address,uint64_t size)
{struct model *m=ctx;return m->owner&&address==0x40000&&size==65536;}
static bool memread(void *ctx,uint64_t address,uint32_t *value)
{struct model *m=ctx;assert(m->owner&&address>=0x40000&&address<0x50000);*value=memory[(address-0x40000)/4];return !m->fail_memory;}
static bool memwrite(void *ctx,uint64_t address,uint32_t value)
{struct model *m=ctx;assert(m->owner&&m->instance==1&&address>=0x40000&&address<0x50000);memory[(address-0x40000)/4]=value;return !m->fail_memory;}
static void setup(struct model *m)
{
	*m=(struct model){.instance=1,.firmware=3,.supervisor=0x100,.masks={0x12,0x34,0x56},.now=100};
	m->io=(struct nouveau_engine_io){m,read32,write32,time_us,delay_us};
	m->upload_io=(struct nouveau_upload_io){m,memread,memwrite,owns};
	for(unsigned i=0;i<20;i++)m->caps[i]=0x2000+i;
	for(unsigned i=0;i<16384;i++){memory[i]=i^0xa5a5;image[i]=i^0x5a5a;}
}
static void upload(struct model *m)
{
	assert(nouveau_engine_capabilities(&m->engine)==NOUVEAU_ENGINE_OK);
	m->owner=true;
	assert(nouveau_vram_pool_init(&m->pool,0x1000000,NULL,0));
	assert(nouveau_vram_pool_alloc(&m->pool,65536,65536,&m->allocation));
	assert(nouveau_upload_save(&m->upload,&m->upload_io,m->allocation,backup,65536)==NOUVEAU_UPLOAD_OK);
	assert(nouveau_upload_image(&m->upload,image)==NOUVEAU_UPLOAD_OK);
}
static void recovered(struct model *m)
{
	m->fail_read=m->fail_write=m->wrong_read=0;m->stuck=m->backward=false;
	m->firmware&=~2U;
	assert(nouveau_engine_restore(&m->engine)==NOUVEAU_ENGINE_OK);
	assert(m->instance==1&&(m->firmware&1)==1);
	assert(m->masks[0]==0x12&&m->masks[1]==0x34&&m->masks[2]==0x56);
	assert(m->engine.state==NOUVEAU_ENGINE_EMPTY);
	for(unsigned i=0;i<20;i++)assert(m->caps[i]==0x2000+i);
}
int main(void)
{
	struct model m;setup(&m);
	assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);
	unsigned reads=m.reads,writes=m.writes;
	assert(m.engine.state==NOUVEAU_ENGINE_QUIESCENT&&!m.supervisor);
	assert(!m.masks[0]&&!m.masks[1]&&!m.masks[2]&&!(m.firmware&3));
	upload(&m);assert(nouveau_engine_bind(&m.engine,&m.upload)==NOUVEAU_ENGINE_OK);
	assert(m.instance==0x409&&m.upload.state==NOUVEAU_UPLOAD_PINNED);
	assert(!nouveau_upload_finish(&m.upload));
	m.controls[8]=3;assert(nouveau_engine_restore(&m.engine)==NOUVEAU_ENGINE_BUSY);
	assert(m.upload.state==NOUVEAU_UPLOAD_PINNED);m.controls[8]=0;
	m.fail_memory=true;
	assert(nouveau_engine_restore(&m.engine)==NOUVEAU_ENGINE_IO_FAILED);
	assert(m.instance==1&&m.upload.state==NOUVEAU_UPLOAD_QUARANTINED&&!(m.firmware&1));
	m.fail_memory=false;recovered(&m);
	for(unsigned i=0;i<16384;i++)assert(memory[i]==(i^0xa5a5));
	assert(nouveau_upload_finish(&m.upload));
	for(unsigned i=1;i<=reads;i++) {
		setup(&m);m.fail_read=i;assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_IO_FAILED);
		if(m.engine.snapshot_valid)recovered(&m);else assert(!m.writes);
	}
	for(unsigned i=1;i<=writes;i++) {
		setup(&m);m.fail_write=i;assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_IO_FAILED);recovered(&m);
	}
	for(unsigned id=0;id<17;id++) {
		setup(&m);m.controls[id]=1;
		assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_BUSY&&!m.writes);
	}
	for(unsigned mode=0;mode<3;mode++) {
		setup(&m);m.stuck=true;m.stalled=mode==1;m.backward=mode==2;
		assert(nouveau_engine_take(&m.engine,&m.io)==(mode==2?NOUVEAU_ENGINE_IO_FAILED:NOUVEAU_ENGINE_TIMEOUT));
		assert(m.delays<=20000);recovered(&m);
	}
	setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);upload(&m);
	m.fail_write=m.writes+1;
	assert(nouveau_engine_bind(&m.engine,&m.upload)==NOUVEAU_ENGINE_IO_FAILED);
	assert(m.upload.state==NOUVEAU_UPLOAD_PINNED);recovered(&m);assert(nouveau_upload_finish(&m.upload));
	setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);
	unsigned cap_reads=m.reads,cap_writes=m.writes;
	assert(nouveau_engine_capabilities(&m.engine)==NOUVEAU_ENGINE_OK);
	cap_reads=m.reads-cap_reads;cap_writes=m.writes-cap_writes;
	for(unsigned i=0;i<20;i++)assert(m.caps[i]==0x1000+i);
	recovered(&m);
	for(unsigned i=1;i<=cap_reads;i++) {
		setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);
		m.fail_read=m.reads+i;
		assert(nouveau_engine_capabilities(&m.engine)==NOUVEAU_ENGINE_IO_FAILED);recovered(&m);
	}
	for(unsigned i=1;i<=cap_writes;i++) {
		setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);
		m.fail_write=m.writes+i;
		assert(nouveau_engine_capabilities(&m.engine)==NOUVEAU_ENGINE_IO_FAILED);recovered(&m);
	}
	/* Every restore register IO must remain retryable, including failures
	 * after unbind/VRAM rollback and a write that already resumed firmware. */
	setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);upload(&m);
	assert(nouveau_engine_bind(&m.engine,&m.upload)==NOUVEAU_ENGINE_OK);
	unsigned restore_reads=m.reads,restore_writes=m.writes;
	assert(nouveau_engine_restore(&m.engine)==NOUVEAU_ENGINE_OK);
	restore_reads=m.reads-restore_reads;restore_writes=m.writes-restore_writes;
	assert(nouveau_upload_finish(&m.upload));
	for(unsigned type=0;type<2;type++) {
		unsigned count=type?restore_writes:restore_reads;
		for(unsigned i=1;i<=count;i++) {
			setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);upload(&m);
			assert(nouveau_engine_bind(&m.engine,&m.upload)==NOUVEAU_ENGINE_OK);
			if(type)m.fail_write=m.writes+i;else m.fail_read=m.reads+i;
			assert(nouveau_engine_restore(&m.engine)==NOUVEAU_ENGINE_IO_FAILED);
			assert(m.engine.state==NOUVEAU_ENGINE_QUARANTINED);
			recovered(&m);assert(nouveau_upload_finish(&m.upload));
		}
	}
	setup(&m);assert(nouveau_engine_take(&m.engine,&m.io)==NOUVEAU_ENGINE_OK);upload(&m);
	m.wrong_read=m.reads+18;
	assert(nouveau_engine_bind(&m.engine,&m.upload)==NOUVEAU_ENGINE_IO_FAILED);
	assert(m.upload.state==NOUVEAU_UPLOAD_PINNED);recovered(&m);assert(nouveau_upload_finish(&m.upload));
	puts("NOUVEAU_ENGINE_PASS: GF119 handoff, 17-channel idle gate, instance pin/readback, VRAM rollback before firmware return, all take/capability/restore register IO failures, bind readback mismatch, timeout and retry");
}
