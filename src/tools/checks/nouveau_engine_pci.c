/* Native handoff transport: real engine code over modeled PCI mappings. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "kernel/nouveau_engine.h"
#include "kernel/nouveau_channel.h"
#include "kernel/nouveau_memory.h"
#include "kernel/dma.h"
#include "arch/x86_64/pci_resource.h"
static uint32_t regs[0x800000/4],saved[0x800000/4];
static unsigned maps,calls,addresses,fail_map,fail_address;
static unsigned external_maps;
#define ENGINE_MAPS 15U
static uint64_t clock_ns;
static bool stalled,taken,caps,restored,busy;
static char log_text[4096];
static unsigned allocations,alloc_calls,fail_alloc;
static bool uploaded,vr_restored,bound,unbound;
static bool dma_live,core_running,core_stopped,fail_dma_alloc,dma_available=true,fail_dma_acquire;
static enum dma_owner dma_owner;
static uint32_t push_words[1024];
static const struct dma_buffer dma_handle={.generation=100,.slot=0};
static unsigned dma_alloc_calls;
static bool expect_stop_failure;
static uint32_t bound_fail_reg;
static bool bound_read(void *ctx,uint32_t reg,uint32_t *value)
{(void)ctx;if(reg==bound_fail_reg)return false;*value=regs[reg/4];return true;}
static bool bound_write(void *ctx,uint32_t reg,uint32_t value)
{(void)ctx;(void)reg;(void)value;assert(0);return false;}
static bool bound_time(void *ctx,uint64_t *value){(void)ctx;*value=0;return true;}
static bool bound_delay(void *ctx,unsigned value){(void)ctx;(void)value;return true;}
static const struct pci_device_lease claim={.generation=1};
static const struct pci_bar_resource bar={.type=PCI_BAR_MEMORY32,.aperture_size=0x1000000};
static void fixture(void)
{
	assert(!maps);calls=addresses=fail_map=fail_address=0;
	external_maps=0;
	assert(!allocations);alloc_calls=fail_alloc=0;uploaded=vr_restored=bound=unbound=false;
	assert(!dma_live);core_running=core_stopped=fail_dma_alloc=fail_dma_acquire=false;dma_available=true;
	dma_alloc_calls=0;
	stalled=taken=caps=restored=busy=false;clock_ns=0;
	log_text[0]=0;
	memset(regs,0,sizeof(regs));
	regs[0x22448/4]=4;regs[0x612004/4]=0x30f;regs[0x610010/4]=1;
	regs[0x610090/4]=0x12;regs[0x6100a0/4]=0x34;regs[0x6100b0/4]=0x56;
	for(unsigned head=0;head<4;head++)for(unsigned j=0;j<3;j++) {
		regs[(0x616104+head*0x800+j*4)/4]=0x1000+head*3+j;
		regs[(0x6101b4+head*0x800+j*4)/4]=0x2000+head*3+j;
	}
	for(unsigned sor=0;sor<8;sor++) {
		regs[(0x61c000+sor*0x800)/4]=0x3000+sor;
		regs[(0x6301c4+sor*0x800)/4]=0x4000+sor;
	}
	memcpy(saved,regs,sizeof(regs));
}
static bool map(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t off,size_t size,struct pci_mmio_lease *lease,bool write)
{
	assert(c==&claim&&b==&bar&&off+size<=sizeof(regs));calls++;
	/* Instance lease must be exactly one DWORD. Channel descriptors/PUT
	 * remain inaccessible; only the disabled core control may be written. */
	if(write) {
		assert(off!=0x610010||size==4);
		assert(!(off<=0x610490&&off+size>0x610490) || (off==0x610490&&size==4));
		assert(size==4||size==12||(off==0x700000&&size==0x100000)||
		    (off==0x610090&&size==0x24)||(off==0x6301c4&&size==0x3804));
	}
	if(calls==fail_map || maps+external_maps>=PCI_MMIO_LEASE_SLOTS)return false;
	lease->generation=calls;lease->address=(uintptr_t)((uint8_t *)regs+off);maps++;
	return true;
}
bool pci_mmio_map(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t off,size_t size,struct pci_mmio_lease *l){return map(c,b,off,size,l,true);}
bool pci_mmio_map_readonly(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t off,size_t size,struct pci_mmio_lease *l){return map(c,b,off,size,l,false);}
bool pci_mmio_lease_address(const struct pci_mmio_lease *l,volatile void **address)
{
	assert(l->generation);addresses++;
	if(addresses==fail_address)return false;
	*address=(volatile void *)l->address;return true;
}
bool pci_mmio_release(struct pci_mmio_lease *l)
{assert(l->generation&&maps);maps--;l->generation=0;return true;}
uint64_t timer_monotonic_ns(void){if(!stalled)clock_ns+=1000;return clock_ns;}
void *kheap_alloc(size_t size)
{
	assert(size==65536);alloc_calls++;
	if(alloc_calls==fail_alloc)return NULL;
	void *p=malloc(size);assert(p);allocations++;return p;
}
bool kheap_free(void *p){assert(p&&allocations);allocations--;free(p);return true;}
bool dma_identity_addressing_available(void){return dma_available;}
bool dma_alloc(size_t size,size_t align,unsigned bits,struct dma_buffer *b)
{
	assert(!dma_live&&size==4096&&align==4096&&bits==40);
	dma_alloc_calls++;
	if(fail_dma_alloc)return false;
	dma_live=true;dma_owner=DMA_OWNER_CPU;*b=dma_handle;
	memset(push_words,0xa5,sizeof(push_words));return true;
}
bool dma_buffer_query(struct dma_buffer b,struct dma_buffer_info *info)
{
	assert(dma_live&&b.generation==100);
	*info=(struct dma_buffer_info){.owner=dma_owner,.cpu_virtual_address=push_words,
	    .requested_size=4096,.allocated_size=4096,.device_address=0x340000};return true;
}
bool dma_device_acquire(struct dma_buffer b,uint64_t *address)
{
	assert(dma_live&&b.generation==100&&dma_owner==DMA_OWNER_CPU&&regs[0x610010/4]==0x40009);
	for(unsigned i=0;i<1024;i++)assert(!push_words[i]);
	if(fail_dma_acquire)return false;
	dma_owner=DMA_OWNER_DEVICE;*address=0x340000;return true;
}
bool dma_device_complete(struct dma_buffer b)
{
	assert(dma_live&&b.generation==100&&dma_owner==DMA_OWNER_DEVICE);
	assert(!(regs[0x610490/4]&0x801e0003)&&regs[0x610010/4]==0x40009);
	assert(regs[0x640000/4]==saved[0x640000/4]);
	for(unsigned i=0;i<3;i++)assert(regs[0x611494/4+i]==saved[0x611494/4+i]);
	dma_owner=DMA_OWNER_CPU;return true;
}
bool dma_free(struct dma_buffer b)
{
	assert(dma_live&&b.generation==100&&dma_owner==DMA_OWNER_CPU);
	assert(!(regs[0x610490/4]&0x801e0003));dma_live=false;return true;
}
void klog(const char *s)
{
	assert(strlen(log_text)+strlen(s)<sizeof(log_text));strcat(log_text,s);
	if(!strcmp(s,"ok")) {
		if(!taken) {taken=true;assert(!regs[0x610090/4]&&!regs[0x6100a0/4]&&!regs[0x6100b0/4]);}
		else {
			caps=true;
			for(unsigned h=0;h<4;h++)for(unsigned j=0;j<3;j++)
				assert(regs[(0x6101b4+h*0x800+j*4)/4]==regs[(0x616104+h*0x800+j*4)/4]);
			for(unsigned sor=0;sor<8;sor++)
				assert(regs[(0x6301c4+sor*0x800)/4]==regs[(0x61c000+sor*0x800)/4]);
		}
	}
	if(strstr(s,"HANDOFF restored"))restored=true;
	if(strstr(s,"UPLOAD instance/RAMHT verified")) {
		uploaded=true;
		assert(taken&&caps&&regs[0x1700/4]==0x400&&regs[0x610010/4]==1);
		uint32_t offset;const uint32_t *image=regs+0x700000/4;
		assert(nouveau_ramht_lookup(image,0,NOUVEAU_NOTIFIER_HANDLE,&offset));
		assert(offset==NOUVEAU_NOTIFIER_OBJECT&&image[(offset+4)/4]==0x400f0);
	}
	if(strstr(s,"UPLOAD original VRAM restored")) {
		vr_restored=true;
		assert(!memcmp(regs+0x700000/4,saved+0x700000/4,65536));
	}
	if(strstr(s,"BIND instance readback verified")) {
		bound=true;
		assert(uploaded&&regs[0x610010/4]==0x40009&&allocations==2);
		for(unsigned i=0;i<17;i++)assert(!(regs[(0x610490+i*16)/4]&0x801e0003));
	}
	if(strstr(s,"BIND original instance restored")) {
		unbound=true;
		assert(bound&&regs[0x610010/4]==1);
		assert(!memcmp(regs+0x700000/4,saved+0x700000/4,65536));
	}
	if(strstr(s,"CORECHANNEL running empty")) {
		core_running=true;
		assert(bound&&dma_live&&dma_owner==DMA_OWNER_DEVICE&&regs[0x640000/4]==0);
		assert(regs[0x611494/4]==0x3403&&regs[0x611498/4]==0x10000&&regs[0x61149c/4]==1);
		assert((regs[0x610490/4]&0x01000013)==0x01000013);
		if(expect_stop_failure)regs[0x610490/4]|=0x20000;
	}
	if(strstr(s,"CORECHANNEL stopped")) {
		core_stopped=true;assert(dma_owner==DMA_OWNER_CPU&&!unbound);
	}
	if(strstr(s,"channels/firmware busy"))busy=true;
}
_Noreturn void kernel_halt(void)
{
	if(expect_stop_failure) {
		assert(strstr(log_text,"CORECHANNEL stop/restore failed"));
		assert(dma_live&&dma_owner==DMA_OWNER_DEVICE&&allocations==2&&maps==ENGINE_MAPS+4);
		assert(bound&&!unbound&&!vr_restored&&regs[0x610010/4]==0x40009);
		_Exit(73);
	}
	abort();
}
int main(void)
{
	fixture();nouveau_engine_probe_log(&claim,&bar);
	assert(taken&&caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
unsigned total=calls;assert(total==ENGINE_MAPS);
	for(unsigned kind=0;kind<2;kind++)for(unsigned i=1;i<=total;i++) {
		fixture();if(kind)fail_address=i;else fail_map=i;
		nouveau_engine_probe_log(&claim,&bar);
		assert(!taken&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	}
	for(unsigned i=0;i<17;i++) {
		fixture();regs[(0x610490+i*16)/4]=3;memcpy(saved,regs,sizeof(regs));
		nouveau_engine_probe_log(&claim,&bar);
		assert(busy&&!taken&&!maps&&!memcmp(saved,regs,sizeof(regs)));
		char expected[160];
		snprintf(expected,sizeof(expected),"HANDOFF blocked reg=0x%08x value=0x00000003 mask=0x801e0003 pending=0x00000003",0x610490+i*16);
		assert(strstr(log_text,expected));
		snprintf(expected,sizeof(expected),"HANDOFF blocked channel=0x%08x",i);
		assert(strstr(log_text,expected));
	}
	fixture();stalled=true;nouveau_engine_probe_log(&claim,&bar);
	assert(!calls&&!taken&&!maps);
	/* Without VBIOS handoff indication, disabled activity still rejects
	 * before writes; do not retry the pre-handoff drain that timed out. */
	fixture();regs[0x610490/4]=0x48070088;memcpy(saved,regs,sizeof(regs));
	nouveau_engine_probe_log(&claim,&bar);
	assert(!taken&&!caps&&!restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	assert(strstr(log_text,"HANDOFF take=channels/firmware busy"));
	assert(strstr(log_text,"HANDOFF blocked firmware=0x00000000 supervisor=0x00000000"));
	/* After supported request/ack, RAM leaves activity stuck. Roll back only
	 * firmware state without requiring that unowned firmware channel idle. */
	fixture();regs[0x610490/4]=0x48070088;regs[0x6194e8/4]=1;regs[0x6100ac/4]=0x100;
	memcpy(saved,regs,sizeof(regs));nouveau_engine_probe_log(&claim,&bar);
	assert(!taken&&!caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	assert(strstr(log_text,"HANDOFF core drain after firmware=timeout"));
	fixture();regs[0x6194e8/4]=1;memcpy(saved,regs,sizeof(regs));
	nouveau_engine_probe_log(&claim,&bar);
	assert(busy&&!taken&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	assert(strstr(log_text,"HANDOFF blocked reg=0x006194e8 value=0x00000001 mask=0x00000003 pending=0x00000001"));
	fixture();regs[0x616104/4]=UINT32_MAX;memcpy(saved,regs,sizeof(regs));
	nouveau_engine_probe_log(&claim,&bar);
	assert(taken&&!caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	struct nouveau_display_snapshot snapshot={.bar3_instance=0x40000000};
	snapshot.heads[0].arm.timing_valid=true;
	struct nouveau_upload_probe_layout layout={.snapshot=&snapshot,.vram=UINT64_C(0x100000000),
	    .scanout_size=8294400,.test_core_channel=true};
	for(unsigned test=0;test<20;test++) {
		fixture();regs[0x1714/4]=0x40000000;regs[0x1700/4]=0x200;
		for(unsigned i=0;i<65536/4;i++)regs[0x700000/4+i]=0xa55a0000^i;
		if(test==9)regs[0x1704/4]=0x80000000; /* BAR VM changed since snapshot */
		regs[0x640000/4]=0x88;
		regs[0x611494/4]=0x1003;regs[0x611498/4]=0x8000;regs[0x61149c/4]=2;
		memcpy(saved,regs,sizeof(regs));
		if(test==1)fail_alloc=1;
		if(test==2)fail_alloc=2;
		if(test==3)fail_map=ENGINE_MAPS+1;
		if(test==4)fail_map=ENGINE_MAPS+2;
		if(test==5)fail_address=ENGINE_MAPS+1;
		if(test==6)fail_address=ENGINE_MAPS+2;
		if(test==7)snapshot.heads[1].arm.timing_valid=true;
		if(test==8)layout.scanout_sample=0x1000000;
		if(test==10)fail_dma_alloc=true;
		if(test==11)dma_available=false;
		if(test==12)fail_map=ENGINE_MAPS+3;
		if(test==13)fail_map=ENGINE_MAPS+4;
		if(test==14)fail_address=ENGINE_MAPS+3;
		if(test==15)fail_address=ENGINE_MAPS+4;
		if(test==16)fail_dma_acquire=true;
		if(test==17)external_maps=12; /* old 30-lease GPU path would exhaust 32 */
		if(test==18)external_maps=14; /* force late PUT map failure even now */
		if(test==19)layout.test_core_channel=false;
		nouveau_engine_probe_upload_log(&claim,&bar,&layout);
		assert(taken&&caps&&restored&&!maps&&!allocations&&!memcmp(saved,regs,sizeof(regs)));
		assert(uploaded==(test==0||test>=10)&&vr_restored==(test==0||test>=10));
		assert(bound==(test==0||test>=10)&&unbound==(test==0||test>=10));
		assert(core_running==(test==0||test==17)&&core_stopped==(test==0||test==17)&&!dma_live);
		if(test==17)assert(calls==ENGINE_MAPS+4&&external_maps==12);
		if(test==18)assert(strstr(log_text,"CORECHANNEL PUT MMIO unavailable")&&external_maps==14);
		if(test==19)assert(strstr(log_text,"CORECHANNEL disabled (experimental start/stop)")&&
		    !dma_alloc_calls&&calls==ENGINE_MAPS+2);
		snapshot.heads[1].arm.timing_valid=false;layout.scanout_sample=0;
		layout.test_core_channel=true;
	}
	pid_t child=fork();assert(child>=0);
	if(!child) {
		fixture();expect_stop_failure=true;regs[0x1714/4]=0x40000000;
		memcpy(saved,regs,sizeof(regs));
		nouveau_engine_probe_upload_log(&claim,&bar,&layout);
		_Exit(1);
	}
	int child_status;assert(waitpid(child,&child_status,0)==child);
	assert(WIFEXITED(child_status)&&WEXITSTATUS(child_status)==73);
	/* Distinguish initial software/transport/MMIO/DMA rejection without any
	 * allocation, mapping or device writes. Observed data is the rejecting read. */
	for(unsigned test=0;test<11;test++) {
		fixture();dma_available=false;bound_fail_reg=0;regs[0x610010/4]=0x40009;
		struct nouveau_upload u={.state=NOUVEAU_UPLOAD_PINNED,.address=0x4000000};
		struct nouveau_engine_io io={NULL,bound_read,bound_write,bound_time,bound_delay};
		struct nouveau_engine e={.state=NOUVEAU_ENGINE_BOUND,.snapshot_valid=true,.caps_ready=true,
		    .instance_touched=true,.instance=&u,.io=&io};
		if(test==0)e.snapshot_valid=false;
		if(test==1)e.caps_ready=false;
		if(test==2)e.io=NULL;
		if(test==3)e.state=NOUVEAU_ENGINE_QUIESCENT;
		if(test==4)e.instance_touched=false;
		if(test==5)u.state=NOUVEAU_UPLOAD_VERIFIED;
		if(test==6)bound_fail_reg=0x6194e8;
		if(test==7)regs[0x6194e8/4]=3;
		if(test==8)bound_fail_reg=0x610010;
		if(test==9)regs[0x610010/4]=1;
		nouveau_core_channel_probe_log(&claim,&bar,&e);
		const char *reasons[]={"engine snapshot/capabilities unavailable","engine snapshot/capabilities unavailable",
		    "engine transport unavailable","engine not bound","bound instance tracking unavailable",
		    "instance not pinned","firmware register read failed","firmware request/ack active",
		    "instance register read failed","instance register mismatch","HOST DMA identity unavailable"};
		assert(strstr(log_text,reasons[test])&&!calls&&!alloc_calls&&!dma_live);
		if(test==7)assert(strstr(log_text,"reg=0x006194e8 observed=0x00000003 expected=0x00000000"));
		if(test==9)assert(strstr(log_text,"reg=0x00610010 observed=0x00000001 expected=0x00040009"));
	}
	puts("NOUVEAU_ENGINE_PCI_PASS: native upload/bind, empty core start/stop, PUT=0, DMA ownership, descriptor restoration, VRAM/PRAMIN rollback, map/heap/DMA failures; no method submission");
}
