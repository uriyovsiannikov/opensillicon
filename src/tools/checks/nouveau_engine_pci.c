/* Native handoff transport: real engine code over modeled PCI mappings. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau_engine.h"
#include "arch/x86_64/pci_resource.h"
static uint32_t regs[0x800000/4],saved[0x800000/4];
static unsigned maps,calls,addresses,fail_map,fail_address;
static uint64_t clock_ns;
static bool stalled,taken,caps,restored,busy;
static const struct pci_device_lease claim={.generation=1};
static const struct pci_bar_resource bar={.type=PCI_BAR_MEMORY32,.aperture_size=0x1000000};
static void fixture(void)
{
	assert(!maps);calls=addresses=fail_map=fail_address=0;
	stalled=taken=caps=restored=busy=false;clock_ns=0;
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
	/* No lease can expose VRAM/PRAMIN, a channel control, or instance as
	 * writable. Mirrors use exact DWORD or 12-byte leases. */
	if(write) {
		assert(off!=0x610010&&off<0x700000);
		assert(!(off<=0x610490&&off+size>0x610490));
		assert(size==4||size==12);
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
void klog(const char *s)
{
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
	if(strstr(s,"channels/firmware busy"))busy=true;
}
_Noreturn void kernel_halt(void){abort();}
int main(void)
{
	fixture();nouveau_engine_probe_log(&claim,&bar);
	assert(taken&&caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	unsigned total=calls;assert(total==23);
	for(unsigned kind=0;kind<2;kind++)for(unsigned i=1;i<=total;i++) {
		fixture();if(kind)fail_address=i;else fail_map=i;
		nouveau_engine_probe_log(&claim,&bar);
		assert(!taken&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	}
	for(unsigned i=0;i<17;i++) {
		fixture();regs[(0x610490+i*16)/4]=3;memcpy(saved,regs,sizeof(regs));
		nouveau_engine_probe_log(&claim,&bar);
		assert(busy&&!taken&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	}
	fixture();stalled=true;nouveau_engine_probe_log(&claim,&bar);
	assert(!calls&&!taken&&!maps);
	fixture();regs[0x6194e8/4]=1;memcpy(saved,regs,sizeof(regs));
	nouveau_engine_probe_log(&claim,&bar);
	assert(busy&&!taken&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	fixture();regs[0x616104/4]=UINT32_MAX;memcpy(saved,regs,sizeof(regs));
	nouveau_engine_probe_log(&claim,&bar);
	assert(taken&&!caps&&restored&&!maps&&!memcmp(saved,regs,sizeof(regs)));
	puts("NOUVEAU_ENGINE_PCI_PASS: native take/capability copy/restore, exact leases, all mapping failures, 17 busy channels, stalled clock, invalid capability rollback; no VRAM/instance/channel writes");
}
