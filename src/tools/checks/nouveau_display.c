/* Read-only display capture, dynamic SOR routing and exact EVO timing words. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "kernel/nouveau_display.h"
#include "kernel/dma.h"

static alignas(4) uint32_t regs[0x800000/4];
static unsigned reads,fail_read,maps,map_calls,address_calls,fail_map,fail_address;
static bool framebuffer_available=true;
static uint32_t xy(unsigned x,unsigned y){return x|(y<<16);}
static uint32_t packet(unsigned method,unsigned count){return (count<<18)|method;}
static struct nouveau_edid_mode mode;
static struct nouveau_dcb_output output;
static struct os_framebuffer_info fb;
static const struct pci_device_lease claim={.generation=1};
static const struct pci_bar_resource bar={.type=PCI_BAR_MEMORY32,.base=0xa3000000,.aperture_size=0x1000000};

static void fixture(void)
{
	assert(maps==0);
	memset(regs,0,sizeof(regs));reads=map_calls=address_calls=0;
	fail_read=fail_map=fail_address=0;framebuffer_available=true;
	mode=(struct nouveau_edid_mode){.width=1920,.height=1080,.clock_khz=148500,
	    .refresh_millihz=60000,.preferred=true,.detailed=true,
	    .hsync_start=2008,.hsync_end=2052,.htotal=2200,
	    .vsync_start=1084,.vsync_end=1089,.vtotal=1125,
	    .hsync_positive=true,.vsync_positive=true};
	output=(struct nouveau_dcb_output){.type=2,.connector=0,.or_mask=2,.heads=15,.link=1};
	fb=(struct os_framebuffer_info){.width=1920,.height=1080,.pitch=7680,.size=7680*1080,
	    .bits_per_pixel=32,.red_mask_size=8,.red_mask_shift=16,.green_mask_size=8,
	    .green_mask_shift=8,.blue_mask_size=8,.blue_mask_shift=0};
	regs[0x100ce0/4]=0x108;regs[0x22448/4]=4;regs[0x612004/4]=0x30f;
	regs[0x610010/4]=0x10009;regs[0x610490/4]=0x01000013;
	regs[0x611494/4]=0x123003;regs[0x611498/4]=0x10000;regs[0x61149c/4]=1;
	regs[0x610494/4]=0xdeadbeef; /* old pre-Pascal descriptor must not be used */
	regs[0x640000/4]=0x80;
	/* Physical OR1 A goes to SOR0 link B, not SOR1/link A. */
	regs[0x612408/4]=0x11;
	regs[0x640200/4]=0x201;regs[0x660200/4]=0x201;
	for(unsigned bank=0;bank<2;bank++){
		unsigned base=bank?0x660000:0x640000;
		regs[(base+0x404)/4]=5<<6;regs[(base+0x408)/4]=0x31ec6000;
		regs[(base+0x414)/4]=xy(2200,1125);regs[(base+0x418)/4]=xy(43,4);
		regs[(base+0x41c)/4]=xy(191,40);regs[(base+0x420)/4]=xy(2111,1120);
		regs[(base+0x424)/4]=1;regs[(base+0x450)/4]=148500000;
		regs[(base+0x454)/4]=0x200000;regs[(base+0x460)/4]=0x2000;
		regs[(base+0x468)/4]=xy(1920,1080);regs[(base+0x46c)/4]=0x1001e00;
		regs[(base+0x470)/4]=0xcf00;regs[(base+0x474)/4]=0xf0000001;
		regs[(base+0x4b8)/4]=xy(1920,1080);regs[(base+0x4c0)/4]=xy(1920,1080);
	}
	regs[0x641400/4]=0x123456;regs[0x661400/4]=0x654321;
	regs[0x644428/4]=xy(1280,720);regs[0x664430/4]=0xcf00;
	regs[0x6440c4/4]=0xf0000001;
	regs[0x616340/4]=100;regs[0x616344/4]=200;
}
static bool read_reg(void *context,uint32_t reg,uint32_t *value)
{
	(void)context;assert(reg<sizeof(regs)&&!(reg&3));
	assert(reg!=0x610494);reads++;
	if(reads==fail_read)return false;
	*value=regs[reg/4];return true;
}
bool pci_mmio_map_readonly(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t offset,size_t size,struct pci_mmio_lease *l)
{
	assert(c==&claim&&b==&bar&&offset+size<=sizeof(regs));map_calls++;
	if(map_calls==fail_map)return false;
	l->generation=1;l->address=(uintptr_t)((uint8_t *)regs+offset);maps++;return true;
}
bool pci_mmio_lease_address(const struct pci_mmio_lease *l,volatile void **address)
{
	assert(l->generation==1);address_calls++;
	if(address_calls==fail_address)return false;
	*address=(volatile void *)(uintptr_t)l->address;return true;
}
bool pci_mmio_release(struct pci_mmio_lease *l)
{
	assert(l->generation==1&&maps>0);maps--;l->generation=0;return true;
}
void nouveau_firmware_instance_log(const struct pci_device_lease *c,
	const struct pci_bar_resource *b,const struct nouveau_display_snapshot *s,uint64_t size)
{assert(c==&claim&&b==&bar&&s->instance==0x10009&&size==UINT64_C(0x100000000));}
bool framebuffer_device_boot_physical(uint64_t *base,uint64_t *size)
{*base=0xb0100000;*size=fb.size;return framebuffer_available;}
bool pci_bar_probe_size(const struct pci_device_lease *c,unsigned index,struct pci_bar_resource *b)
{assert(c==&claim&&index==1);*b=(struct pci_bar_resource){.type=PCI_BAR_MEMORY64,.base=0xb0000000,.aperture_size=0x10000000};return true;}
bool framebuffer_device_info(struct os_framebuffer_info *out){*out=fb;return framebuffer_available;}
void klog(const char *s){(void)s;}
void klog_u64(uint64_t v){(void)v;}
_Noreturn void kernel_halt(void){abort();}

bool dma_identity_addressing_available(void){return false;}
bool dma_alloc(size_t a,size_t b,unsigned c,struct dma_buffer *d)
{(void)a;(void)b;(void)c;(void)d;abort();}
bool dma_buffer_query(struct dma_buffer b,struct dma_buffer_info *i)
{(void)b;(void)i;abort();}
bool dma_free(struct dma_buffer b){(void)b;abort();}

static uint32_t rng=0x987d;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void)
{
	struct nouveau_display_snapshot s;
	struct nouveau_display_plan p;
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	assert(s.vidmem==0x108 && s.base[0].arm[0].offset_lo==0x123456 &&
	    s.base[0].asy[0].offset_lo==0x654321 && s.base[3].arm[1].size==xy(1280,720) &&
	    s.base[3].asy[1].params==0xcf00 && s.base[3].dma[1]==0xf0000001);
	assert(s.head_mask==15&&s.sor_mask==3&&s.head_count==4);
	assert(s.core_push==0x123003&&s.core_limit==0x10000&&s.user_put==0x80);
	assert(s.heads[0].arm.width==1920&&s.heads[0].arm.height==1080);
	assert(s.heads[0].arm.timing_valid&&s.heads[0].arm.refresh_millihz==60000);
	assert(!s.heads[1].arm.timing_valid&&s.heads[0].scan_x==200&&s.heads[0].scan_y==100);
	assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_OK);
	assert(p.head==0&&p.sor==0&&p.link==2&&p.core_class==0x987d&&p.count==24);
	static const uint32_t expected[]={
		(6U<<18)|0x410,0,(1125U<<16)|2200,(4U<<16)|43,(40U<<16)|191,(1120U<<16)|2111,1,
		(1U<<18)|0x42c,0,(3U<<18)|0x450,148500000,0x200000,148500000,
		(2U<<18)|0x404,0x140,0x31ec6000,(1U<<18)|0x494,0,
		(1U<<18)|0x4b8,(1080U<<16)|1920,(3U<<18)|0x4c0,
		(1080U<<16)|1920,(1080U<<16)|1920,(1080U<<16)|1920
	};
	assert(sizeof(expected)==sizeof(p.words)&&!memcmp(p.words,expected,sizeof(expected)));
	unsigned total_reads=reads;
	for(unsigned i=1;i<=total_reads;i++){
		reads=0;fail_read=i;assert(!nouveau_display_snapshot(read_reg,NULL,&s));
	}
	fixture();regs[0x22448/4]=5;assert(!nouveau_display_snapshot(read_reg,NULL,&s));
	fixture();regs[0x612004/4]=UINT32_MAX;assert(!nouveau_display_snapshot(read_reg,NULL,&s));
	fixture();regs[0x22448/4]=1;assert(!nouveau_display_snapshot(read_reg,NULL,&s));
	fixture();regs[0x640450/4]|=0x80000000;
	assert(nouveau_display_snapshot(read_reg,NULL,&s)&&s.heads[0].arm.refresh_millihz==59940);
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	output.link=3;s.route[1][0]=1;s.route[1][1]=0x11;s.sors[0].arm=0x501;
	assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_OK&&p.link==3);
	s.route[1][1]=0x12;
	assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_AMBIGUOUS_ROUTE);
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	mode.detailed=false;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_NO_TIMING);
	mode.detailed=true;mode.interlaced=true;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_BAD_TIMING);
	mode.interlaced=false;mode.hsync_end=mode.hsync_start;
	assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_BAD_TIMING);
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	fb.pitch=0;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_FRAMEBUFFER_MISMATCH);
	fb.pitch=UINT64_MAX;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_FRAMEBUFFER_MISMATCH);
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	output.type=6;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_UNSUPPORTED_OUTPUT);
	output.type=2;output.or_mask=0;assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_NO_ROUTE);
	output.or_mask=2;s.sors[0].arm=0x203;
	assert(nouveau_display_plan(&s,&output,&mode,&fb,&p)==NOUVEAU_PLAN_AMBIGUOUS_ROUTE);
	fixture();assert(nouveau_display_snapshot(read_reg,NULL,&s));
	/* Native capture must use only read-only leases, release partial mappings,
	 * and leave the entire emulated BAR unchanged. */
	struct nouveau_edid edid={.mode_count=1};edid.modes[0]=mode;
	uint32_t *copy=malloc(sizeof(regs));assert(copy);memcpy(copy,regs,sizeof(regs));
	nouveau_display_probe_log(&claim,&bar,&output,&edid);
	assert(!maps&&map_calls==8&&!memcmp(copy,regs,sizeof(regs)));
	for(unsigned i=1;i<=8;i++){
		fixture();fail_map=i;nouveau_display_probe_log(&claim,&bar,&output,&edid);assert(!maps);
		fixture();fail_address=i;nouveau_display_probe_log(&claim,&bar,&output,&edid);assert(!maps);
	}
	free(copy);
	fixture();framebuffer_available=false;nouveau_display_probe_log(&claim,&bar,&output,&edid);assert(!maps);
	/* Fuzz public planner inputs; no queue submission or writes exist here. */
	for(unsigned i=0;i<12000;i++){
		assert(nouveau_display_snapshot(read_reg,NULL,&s));
		struct nouveau_edid_mode m=mode;struct nouveau_dcb_output o=output;
		unsigned f=random32()%8;
		if(f==0)m.htotal=(uint16_t)random32();
		if(f==1)m.clock_khz=random32();
		if(f==2)o.or_mask=(uint8_t)random32();
		if(f==3)o.link=(uint8_t)random32();
		if(f==4)s.head_mask=random32();
		if(f==5)s.route[1][0]=random32();
		if(f==6)s.sors[0].arm=random32();
		if(f==7)s.head_count=random32();
		enum nouveau_display_plan_status status=nouveau_display_plan(&s,&o,&m,&fb,&p);
		if(status==NOUVEAU_PLAN_OK){
			assert(p.count==24&&p.head<4&&p.sor<8);
			assert(p.words[0]==packet(0x410+p.head*0x300,6));
		}
	}
	puts("NOUVEAU_DISPLAY_PASS: armed/async state, dynamic TMDS routing, 24 golden EVO words, read-only leases/cleanup, 12000 planner mutations");
	return 0;
}
