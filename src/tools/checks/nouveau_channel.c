/* GP102/GF119 protocol and retained ownership under every injected failure. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_channel.h"
struct write {uint32_t reg,value;};
struct model {
	unsigned id,reads,writes,times,delays,fail_read,fail_write,fail_time,fail_delay;
	unsigned acquired,released;
	uint32_t ctrl,put;
	uint64_t now;
	bool held,fail_acquire,fail_release,init_busy,stop_busy,stalled,backward,disconnected,bad_put;
	struct write log[32];
};
static bool read32(void *context,uint32_t reg,uint32_t *value)
{
	struct model *m=context;m->reads++;
	if(m->reads==m->fail_read)return false;
	if(reg==0x610490+m->id*16) {
		*value=m->ctrl;
		if((m->ctrl&3)&&m->init_busy)*value|=0x80000000;
		if(!(m->ctrl&3)&&m->stop_busy)*value|=0x20000;
		if(m->disconnected)*value=UINT32_MAX;
	}else {assert(reg==0x640000+m->id*0x1000);*value=m->bad_put?m->put^4:m->put;}
	return true;
}
static bool write32(void *context,uint32_t reg,uint32_t value)
{
	struct model *m=context;assert(m->held);assert(m->writes<32);
	m->log[m->writes++]=(struct write){reg,value};
	if(reg==0x610490+m->id*16)m->ctrl=value;
	else if(reg==0x640000+m->id*0x1000)m->put=value;
	else assert(reg==0x611494+m->id*16||reg==0x611498+m->id*16||reg==0x61149c+m->id*16);
	/* A write can reach hardware even if the transport reports failure. */
	return m->writes!=m->fail_write;
}
static bool time_us(void *context,uint64_t *value)
{
	struct model *m=context;m->times++;
	if(m->times==m->fail_time)return false;
	*value=m->backward&&m->times>2?0:m->now;return true;
}
static bool delay_us(void *context,unsigned delay)
{
	struct model *m=context;assert(delay==100);m->delays++;
	if(m->delays==m->fail_delay)return false;
	if(!m->stalled)m->now+=delay;
	return true;
}
static bool acquire(void *context)
{
	struct model *m=context;assert(!m->held);m->acquired++;
	if(m->fail_acquire)return false;
	m->held=true;return true;
}
static bool release(void *context)
{
	struct model *m=context;assert(m->held&&!(m->ctrl&3)&&!m->stop_busy);m->released++;
	if(m->fail_release)return false;
	m->held=false;return true;
}
static struct nouveau_channel_io io_for(struct model *m)
{return (struct nouveau_channel_io){m,read32,write32,time_us,delay_us,acquire,release};}
static void cleanup(struct nouveau_channel *c,struct model *m)
{
	m->fail_read=m->fail_write=m->fail_time=m->fail_delay=0;
	m->init_busy=m->stop_busy=m->backward=m->disconnected=m->fail_release=false;
	struct nouveau_channel_io io=io_for(m);
	if(c->resources_held) {
		assert(nouveau_channel_release(c,&io)==NOUVEAU_CHANNEL_INVALID);
		assert(nouveau_channel_stop(c,&io)==NOUVEAU_CHANNEL_OK);
		assert(nouveau_channel_release(c,&io)==NOUVEAU_CHANNEL_OK);
	}
	assert(!m->held);
}
int main(void)
{
	for(unsigned id=0;id<=8;id++) {
		struct model m={.id=id,.now=100};struct nouveau_channel_io io=io_for(&m);
		struct nouveau_channel c={0};
		assert(nouveau_channel_prepare(&c,id,0x123003,132));
		assert(!nouveau_channel_prepare(&c,id,0x123003,132));
		assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_OK);
		assert(c.state==NOUVEAU_CHANNEL_RUNNING&&c.resources_held&&m.held);
		const struct write expected[]={
			{0x611494+id*16,0x123003},{0x611498+id*16,0x10000},
			{0x61149c+id*16,1},{0x610490+id*16,0x10},
			{0x640000+id*0x1000,0},{0x610490+id*16,id?0x13:0x01000013}
		};
		assert(m.writes==6&&!memcmp(m.log,expected,sizeof(expected)));
		assert(nouveau_channel_release(&c,&io)==NOUVEAU_CHANNEL_INVALID);
		assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_INVALID);
		m.put=132;
		assert(nouveau_channel_stop(&c,&io)==NOUVEAU_CHANNEL_OK&&c.saved_put==132);
		assert(m.log[6].value==(id?0x1003:0x01000003));
		assert(m.log[7].value==(id?0x1000:0x01000000));
		m.fail_release=true;
		assert(nouveau_channel_release(&c,&io)==NOUVEAU_CHANNEL_RESOURCE_FAILED&&c.resources_held);
		m.fail_release=false;
		assert(nouveau_channel_release(&c,&io)==NOUVEAU_CHANNEL_OK&&!m.held&&c.state==NOUVEAU_CHANNEL_EMPTY);
	}
	/* Fail every start read/write/time operation. Once acquire succeeded,
	 * no release is permitted until a subsequent verified stop. */
	for(unsigned type=0;type<3;type++)for(unsigned nth=1;nth<=(type==0?3:type==1?6:2);nth++) {
		struct model m={0};struct nouveau_channel_io io=io_for(&m);struct nouveau_channel c={0};
		assert(nouveau_channel_prepare(&c,0,0x3403,132));
		if(type==0)m.fail_read=nth;
		if(type==1)m.fail_write=nth;
		if(type==2)m.fail_time=nth;
		assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_IO_FAILED);
		if(m.held)assert(c.state==NOUVEAU_CHANNEL_QUARANTINED);
		cleanup(&c,&m);
	}
	for(unsigned type=0;type<3;type++)for(unsigned nth=1;nth<=(type==0?4:type==1?2:1);nth++) {
		struct model m={0};struct nouveau_channel_io io=io_for(&m);struct nouveau_channel c={0};
		assert(nouveau_channel_prepare(&c,0,0x3403,132));assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_OK);
		if(type==0)m.fail_read=m.reads+nth;
		if(type==1)m.fail_write=m.writes+nth;
		if(type==2)m.fail_time=m.times+nth;
		assert(nouveau_channel_stop(&c,&io)==NOUVEAU_CHANNEL_IO_FAILED&&c.resources_held);
		cleanup(&c,&m);
	}
	for(unsigned mode=0;mode<5;mode++) {
		struct model m={.now=100,.init_busy=true};struct nouveau_channel_io io=io_for(&m);struct nouveau_channel c={0};
		assert(nouveau_channel_prepare(&c,0,0x3403,132));
		if(mode==1)m.stalled=true;
		if(mode==2)m.backward=true;
		if(mode==3)m.fail_delay=1;
		if(mode==4)m.fail_time=3;
		enum nouveau_channel_status status=nouveau_channel_start(&c,&io);
		assert(status==(mode<2?NOUVEAU_CHANNEL_TIMEOUT:NOUVEAU_CHANNEL_IO_FAILED));
		assert(m.delays<=20000&&c.state==NOUVEAU_CHANNEL_QUARANTINED);
		cleanup(&c,&m);
	}
	for(unsigned mode=0;mode<4;mode++) {
		struct model m={0};struct nouveau_channel_io io=io_for(&m);struct nouveau_channel c={0};
		assert(nouveau_channel_prepare(&c,0,0x3403,132));
		assert(nouveau_channel_kick(&c,&io,132)==NOUVEAU_CHANNEL_INVALID&&!m.writes);
		assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_OK);
		unsigned initial_writes=m.writes;
		assert(nouveau_channel_kick(&c,&io,0)==NOUVEAU_CHANNEL_INVALID);
		assert(nouveau_channel_kick(&c,&io,133)==NOUVEAU_CHANNEL_INVALID);
		assert(nouveau_channel_kick(&c,&io,4096)==NOUVEAU_CHANNEL_INVALID);
		assert(m.writes==initial_writes);
		if(mode==1)m.fail_write=m.writes+1;
		if(mode==2)m.fail_read=m.reads+1;
		if(mode==3)m.bad_put=true;
		assert(nouveau_channel_kick(&c,&io,132)==(mode?NOUVEAU_CHANNEL_IO_FAILED:NOUVEAU_CHANNEL_OK));
		assert(m.log[6].reg==0x640000&&m.log[6].value==132);
		assert(c.resources_held&&m.held);
		if(mode) {
			assert(c.state==NOUVEAU_CHANNEL_QUARANTINED);
			assert(nouveau_channel_kick(&c,&io,136)==NOUVEAU_CHANNEL_INVALID);
		}else {
			assert(c.saved_put==132);
			assert(nouveau_channel_kick(&c,&io,132)==NOUVEAU_CHANNEL_INVALID);
			assert(nouveau_channel_kick(&c,&io,128)==NOUVEAU_CHANNEL_INVALID);
			assert(nouveau_channel_kick(&c,&io,136)==NOUVEAU_CHANNEL_INVALID);
		}
		m.bad_put=false;cleanup(&c,&m);
	}
	struct model m={0};struct nouveau_channel_io io=io_for(&m);struct nouveau_channel c={0};
	assert(!nouveau_channel_prepare(&c,9,0x3403,132));
	assert(!nouveau_channel_prepare(&c,0,0x3401,132));
	assert(!nouveau_channel_prepare(&c,0,0x340b,132));
	assert(nouveau_channel_prepare(&c,0,0x3403,132));
	m.ctrl=0x01000013;
	assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_BUSY&&!m.writes&&!m.acquired);
	m.ctrl=0;m.fail_acquire=true;
	assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_RESOURCE_FAILED&&!m.writes);
	m.fail_acquire=false;
	assert(nouveau_channel_start(&c,&io)==NOUVEAU_CHANNEL_OK);
	m.stop_busy=true;
	assert(nouveau_channel_stop(&c,&io)==NOUVEAU_CHANNEL_TIMEOUT&&c.resources_held);
	cleanup(&c,&m);
	struct nouveau_channel invalid={0};
	assert(!nouveau_channel_prepare(&invalid,0,0x3403,0));
	assert(!nouveau_channel_prepare(&invalid,0,0x3403,133));
	assert(!nouveau_channel_prepare(&invalid,0,0x3403,4096));
	assert(nouveau_channel_prepare(&invalid,0,0x3403,4092));
	puts("NOUVEAU_CHANNEL_PASS: GP102 golden init, GF119 core/base/overlay stop, all IO failures, quarantine/retry, timeout/stalled/backward clock, resource retention, PUT ready-range bounds");
}
