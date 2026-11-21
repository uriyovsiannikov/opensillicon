/* Native handoff transport: real engine code over modeled PCI mappings. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau_engine.h"
#include "kernel/nouveau_memory.h"
#include "arch/x86_64/pci_resource.h"
static uint32_t regs[0x800000/4],saved[0x800000/4];
static unsigned maps,calls,addresses,fail_map,fail_address;
static uint64_t clock_ns;
static bool stalled,taken,caps,restored,busy;
static char log_text[4096];
static unsigned allocations,alloc_calls,fail_alloc;
static bool uploaded,vr_restored,bound,unbound;
static const struct pci_device_lease claim={.generation=1};
static const struct pci_bar_resource bar={.type=PCI_BAR_MEMORY32,.aperture_size=0x1000000};
static void fixture(void)
{
	assert(!maps);calls=addresses=fail_map=fail_address=0;
	assert(!allocations);alloc_calls=fail_alloc=0;uploaded=vr_restored=bound=unbound=false;
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
		assert(size==4||size==12||(off==0x700000&&size==0x100000));
	}
	if(calls==fail_map)return false;
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
	if(strstr(s,"channels/firmware busy"))busy=true;
}
_Noreturn void kernel_halt(void){abort();}
int main(void)
{
	fixture();nouveau_engine_probe_log(&claim,&bar);
	assert(taken&&caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
unsigned total=calls;assert(total==26);
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
	struct nouveau_upload_probe_layout layout={&snapshot,UINT64_C(0x100000000),0,8294400,0};
	for(unsigned test=0;test<10;test++) {
		fixture();regs[0x1714/4]=0x40000000;regs[0x1700/4]=0x200;
		for(unsigned i=0;i<65536/4;i++)regs[0x700000/4+i]=0xa55a0000^i;
		if(test==9)regs[0x1704/4]=0x80000000; /* BAR VM changed since snapshot */
		memcpy(saved,regs,sizeof(regs));
		if(test==1)fail_alloc=1;
		if(test==2)fail_alloc=2;
		if(test==3)fail_map=27;
		if(test==4)fail_map=28;
		if(test==5)fail_address=27;
		if(test==6)fail_address=28;
		if(test==7)snapshot.heads[1].arm.timing_valid=true;
		if(test==8)layout.scanout_sample=0x1000000;
		nouveau_engine_probe_upload_log(&claim,&bar,&layout);
		assert(taken&&caps&&restored&&!maps&&!allocations&&!memcmp(saved,regs,sizeof(regs)));
		assert(uploaded==(test==0)&&vr_restored==(test==0));
		assert(bound==(test==0)&&unbound==(test==0));
		snapshot.heads[1].arm.timing_valid=false;layout.scanout_sample=0;
	}
	puts("NOUVEAU_ENGINE_PCI_PASS: native take/capabilities, 64KiB upload, pinned instance bind/readback/unbind, VRAM/PRAMIN rollback, heap/map failures, unsupported scanout; no channel start");
}
