#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_supervisor.h"
struct event { unsigned action,head; };
struct model {
	uint32_t pending,masks[4];unsigned reads,writes,actions,completed;
	unsigned fail_read,fail_write,fail_action;
	struct event events[16];
	struct nouveau_supervisor *s;struct nouveau_supervisor_io *io;
};
static bool read32(void *ctx,uint32_t reg,uint32_t *value)
{
	struct model *m=ctx;m->reads++;
	if(m->reads==m->fail_read)return false;
	if(reg==0x6100ac)*value=m->pending;
	else {assert(reg>=0x6101d4&&reg<=0x6119d4&&!((reg-0x6101d4)&0x7ff));*value=m->masks[(reg-0x6101d4)/0x800];}
	return true;
}
static bool write32(void *ctx,uint32_t reg,uint32_t value)
{
	struct model *m=ctx;m->writes++;
	if(reg==0x6100ac){assert(m->reads==5&&m->actions==0&&value==(m->pending&7));m->pending&=~value;}
	else if(reg==0x6101d0){assert(value==0x80000000);m->completed++;}
	else {assert(reg>=0x6101d4&&reg<=0x6119d4&&value==0);m->masks[(reg-0x6101d4)/0x800]=0;}
	return m->writes!=m->fail_write;
}
static bool action(void *ctx,enum nouveau_supervisor_action action,unsigned head)
{
	struct model *m=ctx;assert(m->s->acknowledged&&!m->pending);
	assert(m->actions<16);m->events[m->actions++]=(struct event){action,head};
	/* Reentrant callbacks must not acknowledge or execute a stage twice. */
	assert(nouveau_supervisor_step(m->s,m->io)==NOUVEAU_SUPERVISOR_INVALID);
	return m->actions!=m->fail_action;
}
static void setup(struct model *m,struct nouveau_supervisor *s,struct nouveau_supervisor_io *io,unsigned stage)
{
	*s=(struct nouveau_supervisor){.head_mask=15};
	*m=(struct model){.pending=stage,.masks={0x11000,0x10000,0x1000,0},.s=s,.io=io};
	*io=(struct nouveau_supervisor_io){m,read32,write32,action};
}
int main(void)
{
	struct model m;struct nouveau_supervisor s;struct nouveau_supervisor_io io;
	for(unsigned stage=1;stage<=4;stage*=2) {
		setup(&m,&s,&io,stage);
		assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_OK&&m.completed==1&&s.acknowledged);
		const struct event phase1[]={{NOUVEAU_SUPERVISOR_SNAPSHOT,255},{NOUVEAU_SUPERVISOR_DETACH1,0},{NOUVEAU_SUPERVISOR_DETACH1,2}};
		const struct event phase2[]={{NOUVEAU_SUPERVISOR_DETACH2,0},{NOUVEAU_SUPERVISOR_DETACH2,2},
		    {NOUVEAU_SUPERVISOR_PLL,0},{NOUVEAU_SUPERVISOR_PLL,1},
		    {NOUVEAU_SUPERVISOR_ATTACH2,0},{NOUVEAU_SUPERVISOR_ATTACH2,2}};
		const struct event phase3[]={{NOUVEAU_SUPERVISOR_ATTACH3,0},{NOUVEAU_SUPERVISOR_ATTACH3,2}};
		const void *expected=stage==1?(const void *)phase1:stage==2?(const void *)phase2:phase3;
		unsigned count=stage==1?3:stage==2?6:2;
		assert(m.actions==count&&!memcmp(m.events,expected,count*sizeof(struct event)));
		for(unsigned i=1;i<=5;i++) {
			setup(&m,&s,&io,stage);m.fail_read=i;
			assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_IO_FAILED&&!m.writes);
		}
		for(unsigned i=1;i<=6;i++) {
			setup(&m,&s,&io,stage);m.fail_write=i;
			assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_IO_FAILED);
			assert(s.state==NOUVEAU_SUPERVISOR_FAILED);
			unsigned writes=m.writes,actions=m.actions;
			assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_INVALID&&m.writes==writes&&m.actions==actions);
		}
		for(unsigned i=1;i<=count;i++) {
			setup(&m,&s,&io,stage);m.fail_action=i;
			assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_ACTION_FAILED&&!m.completed&&m.writes==1);
		}
	}
	setup(&m,&s,&io,0);assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_NO_EVENT&&!m.writes);
	setup(&m,&s,&io,3);assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_AMBIGUOUS&&!m.writes&&!m.actions);
	setup(&m,&s,&io,0x100);assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_NO_EVENT&&m.pending==0x100);
	setup(&m,&s,&io,1);s.head_mask=16;assert(nouveau_supervisor_step(&s,&io)==NOUVEAU_SUPERVISOR_INVALID&&!m.reads);
	puts("NOUVEAU_SUPERVISOR_PASS: GF119 stage ordering, mask snapshot before W1C, all read/write/action failures, reentry/replay rejection, no completion after failure");
}
