/* Protocol model: EEPROM slave on GPIO I2C, and GM200 AUX controller replies.
 * No host hardware is accessed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau_edid.h"

static uint8_t sample[512];
static void checksum(uint8_t *p)
{
	p[127]=0; uint8_t sum=0;
	for (unsigned i=0;i<128;i++) sum+=p[i];
	p[127]=(uint8_t)-sum;
}
static void fixture_edid(void)
{
	memset(sample,0,sizeof(sample));
	memset(sample+1,255,6);
	unsigned m=(19<<10)|(1<<5)|13; /* SAM */
	sample[8]=m>>8;sample[9]=m;sample[10]=0x34;sample[11]=0x12;
	sample[12]=0x78;sample[13]=0x56;sample[18]=1;sample[19]=4;sample[24]=2;
	for(unsigned i=38;i<54;i++)sample[i]=1;
	/* 1920x1080p60: 148.5 MHz, totals 2200x1125. */
	uint8_t *d=sample+54;
	d[0]=0x02;d[1]=0x3a;d[2]=0x80;d[3]=0x18;d[4]=0x71;
	d[5]=0x38;d[6]=0x2d;d[7]=0x40;d[17]=0x1e;
	d[8]=88;d[9]=44;d[10]=0x45;
	d=sample+72;d[3]=0xfc;memcpy(d+5,"Test Monitor\n",13);
	sample[35]=1<<5; /* established 640x480p60 */
	sample[38]=129;sample[39]=0x80; /* standard 1280x1024p60 */
	sample[126]=1;
	sample[128]=2;sample[129]=3;sample[130]=7;
	sample[132]=(2<<5)|2;sample[133]=16;sample[134]=97; /* 1080p and 2160p */
	checksum(sample);checksum(sample+128);
}

enum phase { IDLE, RX, RX_ACK, TX, MASTER_ACK };
struct model {
	uint32_t regs[1024];
	uint8_t memory[512];
	uint32_t bus, aux;
	unsigned segment, offset, bytes, starts, stops, packets, delays, elapsed;
	unsigned bits, byte, device, payloads;
	bool addressed, slave_low, ack_clocked, read_direction, master_nack;
	enum phase phase;
	bool nack, sink, stuck_clock, timeout_aux, claim_timeout, fail_restore;
	unsigned stretch_after, rising_edges, fail_delay, defers, short_read;
	bool zero_reply;
};
static void tx_level(struct model *m)
{
	unsigned pos=m->segment*256+m->offset;
	assert(pos<sizeof(m->memory));
	m->slave_low=(m->memory[pos]&(0x80U>>m->bits))==0;
}
static void gpio_write(struct model *m,uint32_t value)
{
	uint32_t old=m->regs[(m->bus-0xd000)/4];
	bool old_scl=(old&1)!=0, new_scl=(value&1)!=0;
	bool old_sda=(old&2)!=0, new_sda=(value&2)!=0;
	m->regs[(m->bus-0xd000)/4]=value&~0x30U;
	if(old_scl&&new_scl&&old_sda&&!new_sda&&!m->slave_low){
		m->starts++;m->phase=RX;m->bits=0;m->byte=0;m->addressed=false;
		m->slave_low=false;m->ack_clocked=false;return;
	}
	if(old_scl&&new_scl&&!old_sda&&new_sda&&!m->slave_low){
		m->stops++;m->phase=IDLE;m->slave_low=false;return;
	}
	if(!old_scl&&new_scl){
		m->rising_edges++;
		switch(m->phase){
		case RX:
			m->byte=(m->byte<<1)|new_sda;
			if(++m->bits==8){m->phase=RX_ACK;m->ack_clocked=false;}
			break;
		case RX_ACK:m->ack_clocked=true;break;
		case TX:m->bits++;break;
		case MASTER_ACK:m->master_nack=new_sda;m->ack_clocked=true;break;
		default:break;
		}
	}
	if(old_scl&&!new_scl){
		switch(m->phase){
		case RX_ACK:
			if(!m->ack_clocked){m->slave_low=!m->nack;break;}
			m->slave_low=false;
			if(!m->addressed){
				m->device=m->byte>>1;m->read_direction=m->byte&1;
				assert(m->device==0x50||m->device==0x30);
				m->addressed=true;m->payloads=0;
			}else{
				assert(!m->read_direction&&m->payloads++==0);
				if(m->device==0x50)m->offset=m->byte;
				else m->segment=m->byte;
			}
			m->bits=0;m->byte=0;
			if(m->read_direction&&!m->nack){m->phase=TX;tx_level(m);}
			else m->phase=RX;
			break;
		case TX:
			if(m->bits==8){m->phase=MASTER_ACK;m->slave_low=false;m->ack_clocked=false;}
			else tx_level(m);
			break;
		case MASTER_ACK:
			if(m->ack_clocked){
				m->bytes++;m->offset=(m->offset+1)&255;m->bits=0;
				if(m->master_nack){m->phase=IDLE;m->slave_low=false;}
				else{m->phase=TX;tx_level(m);}
			}
			break;
		default:break;
		}
	}
}
static uint32_t read_reg(void *ctx,uint32_t reg)
{
	struct model *m=ctx;assert(reg>=0xd000&&reg<0xe000&&!(reg&3));
	uint32_t value=m->regs[(reg-0xd000)/4];
	if(reg==m->aux+0x28)value=(value&~0x10000000U)|(m->sink?0x10000000:0);
	if(reg==m->bus){
		value&=~0x30U;
		if((value&1)&&!m->stuck_clock&&(!m->stretch_after||m->rising_edges<m->stretch_after))value|=0x10;
		if((value&2)&&!m->slave_low)value|=0x20;
	}
	return value;
}
static void aux_request(struct model *m,uint32_t value)
{
	m->packets++;
	if(m->timeout_aux)return;
	uint32_t *ctrl=&m->regs[(m->aux+0x24-0xd000)/4];
	*ctrl&=~0x10000U;
	uint32_t stat=m->sink?0x10000000:0;
	if(m->defers){m->defers--;stat|=2<<16;}
	else if(m->nack)stat|=4<<16;
	else{
		unsigned command=(value>>12)&15;
		unsigned len=value&0x100?0:(value&15)+1;
		unsigned address=m->regs[(m->aux+0x20-0xd000)/4];
		assert(command==0||command==1||command==4||command==5);
		assert(address==0x50||address==0x30);
		if(command&1){
			if(m->zero_reply)len=0;
			else if(m->short_read&&len>m->short_read)len=m->short_read;
			uint8_t bytes[16]={0};
			for(unsigned i=0;i<len;i++){
				unsigned pos=m->segment*256+m->offset;assert(pos<sizeof(m->memory));
				bytes[i]=m->memory[pos];m->offset=(m->offset+1)&255;
			}
			for(unsigned i=0;i<4;i++){
				uint32_t word=0;for(unsigned j=0;j<4;j++)word|=(uint32_t)bytes[i*4+j]<<(j*8);
				m->regs[(m->aux+0x10+i*4-0xd000)/4]=word;
			}
			m->bytes+=len;stat|=len;
		}else if(len){
			assert(len==1);
			unsigned byte=m->regs[(m->aux-0xd000)/4]&255;
			if(address==0x50)m->offset=byte;else m->segment=byte;
		}
	}
	m->regs[(m->aux+0x28-0xd000)/4]=stat;
}
static void write_reg(void *ctx,uint32_t reg,uint32_t value)
{
	struct model *m=ctx;assert(reg>=0xd000&&reg<0xe000&&!(reg&3));
	if(m->fail_restore&&reg==0xd970+0x50&&(value&0xc003)==0x8000)return;
	if(reg==m->bus){gpio_write(m,value);return;}
	if(reg==m->aux+0x28){m->regs[(reg-0xd000)/4]&=~(value&~0x10000000U);return;}
	if(reg==m->aux+0x24){
		value&=~0x07000000U;
		if((value&0x00700000)&&!m->claim_timeout)value|=0x01000000;
		if(value&0x80000000U)value&=~0x80010000U;
		m->regs[(reg-0xd000)/4]=value;
		if(value&0x10000)aux_request(m,value);
		return;
	}
	m->regs[(reg-0xd000)/4]=value;
}
static bool delay_us(void *ctx,unsigned us)
{
	struct model *m=ctx;m->elapsed+=us;m->delays++;
	return m->delays!=m->fail_delay;
}
static void init(struct model *m)
{
	memset(m,0,sizeof(*m));memcpy(m->memory,sample,sizeof(sample));
	m->bus=0xd014+2*0x20;m->aux=0xd930+1*0x50;m->sink=true;
	m->regs[(m->bus-0xd000)/4]=0x40000007;
	m->regs[(0xd970+0x50-0xd000)/4]=0x12348000;
	m->regs[(0xd97c+0x50-0xd000)/4]=0xabc001;
	m->regs[(m->aux+0x24-0xd000)/4]=0x33;
	m->regs[(m->aux+0x20-0xd000)/4]=0x123;
	m->regs[(m->aux+0x28-0xd000)/4]=0x10000000;
	m->regs[(m->aux+0x38-0xd000)/4]=0x10000;
	for(unsigned i=0;i<4;i++)m->regs[(m->aux+i*4-0xd000)/4]=0xaa55bb00+i;
}
static struct nouveau_ddc_result run(struct model *m,bool aux)
{
	struct nouveau_ddc_io io={m,read_reg,write_reg,delay_us};
	struct nouveau_i2c_entry entry={.type=0x80,.drive=2,.aux=1,.share=1};
	struct nouveau_ddc_result r;
	nouveau_ddc_read(&io,&entry,aux,&r);
	assert((m->regs[(m->bus-0xd000)/4]&7)==7);
	assert(m->regs[(0xd970+0x50-0xd000)/4]==0x12348000);
	assert(m->regs[(0xd97c+0x50-0xd000)/4]==0xabc001);
	if(aux){
		assert(m->regs[(m->aux+0x24-0xd000)/4]==0x33);
		assert(m->regs[(m->aux+0x20-0xd000)/4]==0x123);
		assert(m->regs[(m->aux+0x38-0xd000)/4]==0x10000);
		for(unsigned i=0;i<4;i++)assert(m->regs[(m->aux+i*4-0xd000)/4]==0xaa55bb00+i);
	}
	assert(m->elapsed<215000);
	return r;
}

static uint32_t rng=0x137;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void)
{
	fixture_edid();struct nouveau_edid e;
	assert(nouveau_edid_parse(sample,256,&e));
	assert(!strcmp(e.manufacturer,"SAM")&&!strcmp(e.name,"Test Monitor"));
	assert(e.product==0x1234&&e.serial==0x5678&&e.mode_count==4);
	assert(e.modes[0].width==1920&&e.modes[0].height==1080&&e.modes[0].preferred);
	assert(e.modes[0].refresh_millihz==60000&&e.modes[0].clock_khz==148500);
	assert(e.modes[0].detailed&&e.modes[0].hsync_start==2008&&e.modes[0].hsync_end==2052);
	assert(e.modes[0].htotal==2200&&e.modes[0].vsync_start==1084&&e.modes[0].vsync_end==1089&&e.modes[0].vtotal==1125);
	/* Invalid duplicate DTD must not mix a new clock with old exact timing. */
	sample[24]=0;memcpy(sample+90,sample+54,18);sample[90]++;
	sample[98]=sample[99]=sample[100]=0;checksum(sample);
	assert(nouveau_edid_parse(sample,256,&e)&&e.modes[0].detailed&&e.modes[0].clock_khz==148500);
	fixture_edid();assert(nouveau_edid_parse(sample,256,&e));
	assert(e.modes[3].width==3840&&e.modes[3].height==2160);
	assert(nouveau_edid_parse(sample,128,&e)&&e.truncated);
	sample[255]^=1;assert(nouveau_edid_parse(sample,256,&e)&&e.bad_extensions==1);sample[255]^=1;
	sample[132]=(2<<5)|31;checksum(sample+128);
	assert(nouveau_edid_parse(sample,256,&e)&&e.bad_extensions==1);
	fixture_edid();sample[0]=1;assert(!nouveau_edid_parse(sample,128,&e));
	fixture_edid();sample[18]=2;checksum(sample);assert(!nouveau_edid_parse(sample,128,&e));
	fixture_edid();sample[8]=sample[9]=0;checksum(sample);assert(!nouveau_edid_parse(sample,128,&e));
	fixture_edid();sample[19]=2;sample[38]=129;sample[39]=0;checksum(sample);
	assert(nouveau_edid_parse(sample,128,&e)&&e.modes[2].height==1280);
	fixture_edid();sample[54]=1;sample[55]=0x1d;sample[59]=0x1c;sample[60]=0x16;
	sample[61]=0x20;sample[71]|=0x80;checksum(sample);
	assert(nouveau_edid_parse(sample,128,&e)&&e.modes[0].height==1080);
	assert(e.modes[0].interlaced&&e.modes[0].refresh_millihz==60000);
	fixture_edid();
	struct model m;struct nouveau_ddc_result r;
	init(&m);r=run(&m,false);assert(r.status==NOUVEAU_DDC_OK&&r.bytes==256);
	assert(!memcmp(r.data,sample,256)&&m.bytes==256&&m.stops>=2);
	init(&m);r=run(&m,true);assert(r.status==NOUVEAU_DDC_OK&&r.bytes==256);
	assert(!memcmp(r.data,sample,256)&&m.bytes==256);
	init(&m);m.nack=true;r=run(&m,false);assert(r.status==NOUVEAU_DDC_NACK&&r.bytes==0);
	init(&m);m.nack=true;r=run(&m,true);assert(r.status==NOUVEAU_DDC_NACK&&r.bytes==0);
	init(&m);m.stuck_clock=true;r=run(&m,false);assert(r.status==NOUVEAU_DDC_BUSY);
	init(&m);m.stretch_after=12;r=run(&m,false);assert(r.status==NOUVEAU_DDC_TIMEOUT);
	init(&m);m.fail_delay=1;r=run(&m,false);assert(r.status==NOUVEAU_DDC_TIMEOUT);
	init(&m);m.sink=false;r=run(&m,true);assert(r.status==NOUVEAU_DDC_NO_SINK&&m.packets==0);
	init(&m);m.claim_timeout=true;r=run(&m,true);assert(r.status==NOUVEAU_DDC_TIMEOUT&&m.packets==0);
	init(&m);m.timeout_aux=true;r=run(&m,true);assert(r.status==NOUVEAU_DDC_TIMEOUT);
	init(&m);m.defers=3;r=run(&m,true);assert(r.status==NOUVEAU_DDC_OK&&r.bytes==256);
	init(&m);m.defers=100;r=run(&m,true);assert(r.status==NOUVEAU_DDC_TIMEOUT&&m.packets<=16);
	init(&m);m.zero_reply=true;r=run(&m,true);assert(r.status==NOUVEAU_DDC_TIMEOUT);
	init(&m);m.short_read=7;r=run(&m,true);assert(r.status==NOUVEAU_DDC_OK&&r.bytes==256&&!memcmp(r.data,sample,256));
	for(unsigned aux=0;aux<2;aux++){
		init(&m);m.memory[127]^=1;r=run(&m,aux);assert(r.status==NOUVEAU_DDC_EDID_INVALID&&r.bytes==128);
		init(&m);m.memory[126]=3;checksum(m.memory);
		memcpy(m.memory+256,m.memory+128,128);memcpy(m.memory+384,m.memory+128,128);
		r=run(&m,aux);assert(r.status==NOUVEAU_DDC_OK&&r.bytes==512&&m.segment==0);
		assert(!memcmp(r.data,m.memory,512));
	}
	struct nouveau_ddc_io io={&m,read_reg,write_reg,delay_us};
	struct nouveau_i2c_entry bad={.type=0x80,.drive=31,.aux=8,.share=8};
	init(&m);nouveau_ddc_read(&io,&bad,true,&r);assert(r.status==NOUVEAU_DDC_UNSUPPORTED&&m.packets==0);
	struct nouveau_i2c_entry entry={.type=0x80,.drive=2,.aux=1,.share=1};
	init(&m);m.fail_restore=true;nouveau_ddc_read(&io,&entry,false,&r);
	assert(r.status==NOUVEAU_DDC_RESTORE);
	init(&m);m.regs[(m.aux+0x24-0xd000)/4]|=0x10000;
	nouveau_ddc_read(&io,&entry,true,&r);
	assert(r.status==NOUVEAU_DDC_BUSY&&m.packets==0);
	assert(m.regs[(m.aux+0x24-0xd000)/4]==0x10033);
	assert(m.regs[(0xd970+0x50-0xd000)/4]==0x12348000);
	for(unsigned i=0;i<10000;i++){
		size_t size=random32()%513;uint8_t *p=malloc(size?size:1);assert(p);
		fixture_edid();memcpy(p,sample,size);
		for(unsigned j=0;j<8&&size;j++)p[random32()%size]=(uint8_t)random32();
		if(i&1)for(size_t off=0;off+128<=size;off+=128)checksum(p+off);
		(void)nouveau_edid_parse(p,size,&e);free(p);
	}
	puts("NOUVEAU_EDID_PASS: GPIO I2C EEPROM, GM200 AUX, DEFER/NACK/timeouts/short replies, E-DDC segments, register restore, EDID/CTA, 10000 mutations");
	return 0;
}
