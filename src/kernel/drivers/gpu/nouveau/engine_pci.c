/* SPDX-License-Identifier: BSD-2-Clause
 * Native reversible GF119 handoff, before enabling any VRAM upload. */
#include "kernel/nouveau_engine.h"
#include "arch/x86_64/pci_resource.h"
#include "kernel/log.h"
#include "kernel/timer.h"

struct region { uint32_t base,size; bool writable; };
/* Only core control permits reasserting an already disabled stop request;
 * no instance/descriptors/PUT/PLL/PRAMIN mappings are writable here. */
static const struct region regions[]={
	{0x22448,4,false},{0x612004,4,false},{0x610010,4,false},
	{0x610490,4,true},{0x6104a0,0xf4,false},{0x616000,0x2000,false},{0x61c000,0x4000,false},
	{0x610090,4,true},{0x6100a0,4,true},{0x6100ac,4,true},{0x6100b0,4,true},
	{0x6194e8,4,true},
	{0x6101b4,12,true},{0x6109b4,12,true},{0x6111b4,12,true},{0x6119b4,12,true},
	{0x6301c4,4,true},{0x6309c4,4,true},{0x6311c4,4,true},{0x6319c4,4,true},
	{0x6321c4,4,true},{0x6329c4,4,true},{0x6331c4,4,true},{0x6339c4,4,true}
};
#define REGIONS (sizeof(regions)/sizeof(regions[0]))
struct native_engine {
	struct pci_mmio_lease leases[REGIONS];
	volatile uint32_t *addresses[REGIONS];
};
static volatile uint32_t *locate(struct native_engine *m,uint32_t reg,bool write)
{
	if(reg&3) return NULL;
	for(unsigned i=0;i<REGIONS;i++)
		if(reg>=regions[i].base && reg-regions[i].base<=regions[i].size-4 &&
		    (!write || regions[i].writable)) return m->addresses[i]+(reg-regions[i].base)/4;
	return NULL;
}
static bool read32(void *ctx,uint32_t reg,uint32_t *value)
{
	volatile uint32_t *p=locate(ctx,reg,false);
	if(!p) return false;
	*value=*p;return true;
}
static bool capability_register(uint32_t reg)
{
	for(unsigned head=0;head<4;head++)
		for(unsigned j=0;j<3;j++)if(reg==0x6101b4+head*0x800+j*4)return true;
	for(unsigned sor=0;sor<8;sor++)if(reg==0x6301c4+sor*0x800)return true;
	return false;
}
static bool write32(void *ctx,uint32_t reg,uint32_t value)
{
	/* Restrict writes even inside larger leases used by strided mirrors. */
	if(reg!=0x610490 && reg!=0x610090 && reg!=0x6100a0 && reg!=0x6100ac && reg!=0x6100b0 &&
	    reg!=0x6194e8 && !capability_register(reg)) return false;
	volatile uint32_t *p=locate(ctx,reg,true);
	if(!p) return false;
	*p=value;return true;
}
static bool time_us(void *ctx,uint64_t *value)
{
	(void)ctx;*value=timer_monotonic_ns()/1000;return true;
}
static bool delay_us(void *ctx,unsigned delay)
{
	(void)ctx;uint64_t start=timer_monotonic_ns(),previous=start;
	for(unsigned i=0;i<1000000;i++) {
		uint64_t now=timer_monotonic_ns();
		if(now<previous) return false;
		if(now-start>=(uint64_t)delay*1000) return true;
		previous=now;__asm__ volatile("pause");
	}
	return false;
}
static const char *status_name(enum nouveau_engine_status status)
{
	switch(status) {
	case NOUVEAU_ENGINE_OK:return "ok";
	case NOUVEAU_ENGINE_INVALID:return "invalid";
	case NOUVEAU_ENGINE_BUSY:return "channels/firmware busy";
	case NOUVEAU_ENGINE_IO_FAILED:return "register IO failed";
	case NOUVEAU_ENGINE_TIMEOUT:return "timeout";
	}
	return "unknown";
}
static void hex32(uint32_t value)
{
	static const char digits[]="0123456789abcdef";char text[9];
	for(unsigned i=0;i<8;i++)text[i]=digits[(value>>((7-i)*4))&15];
	text[8]=0;klog(text);
}
static void blocker_log(const struct nouveau_engine *engine)
{
	if(!engine->blocked_mask) return;
	klog("nouveau: HANDOFF blocked reg=0x");hex32(engine->blocked_reg);
	klog(" value=0x");hex32(engine->blocked_value);
	klog(" mask=0x");hex32(engine->blocked_mask);
	klog(" pending=0x");hex32(engine->blocked_value&engine->blocked_mask);
	klog("\n");
	if(engine->blocked_reg>=0x610490 && engine->blocked_reg<=0x610590) {
		klog("nouveau: HANDOFF blocked channel=0x");
		/* Fixed-width hexadecimal keeps the transport independent of formatting. */
		hex32((engine->blocked_reg-0x610490)/16);klog(" (control index)\n");
	}
}
void nouveau_engine_probe_log(const struct pci_device_lease *claim,const struct pci_bar_resource *bar)
{
	struct native_engine m={0};struct nouveau_engine engine={0};
	struct nouveau_engine_io io={&m,read32,write32,time_us,delay_us};
	/* Verify the clock advances before touching any hardware state. */
	if(!delay_us(&m,1)) {klog("nouveau: HANDOFF clock unavailable\n");return;}
	for(unsigned i=0;i<REGIONS;i++) {
		volatile void *address;
		bool ok=regions[i].writable?
		    pci_mmio_map(claim,bar,regions[i].base,regions[i].size,&m.leases[i]):
		    pci_mmio_map_readonly(claim,bar,regions[i].base,regions[i].size,&m.leases[i]);
		if(!ok || !pci_mmio_lease_address(&m.leases[i],&address)) {
			klog("nouveau: HANDOFF MMIO unavailable\n");goto out;
		}
		m.addresses[i]=address;
	}
	enum nouveau_engine_status status=nouveau_engine_take(&engine,&io);
	if(engine.snapshot_valid) {
		klog("nouveau: HANDOFF saved firmware=0x");hex32(engine.saved_firmware);
		klog(" supervisor=0x");hex32(engine.saved_supervisor);klog("\n");
	}
	if(engine.drain_attempted) {
		klog("nouveau: HANDOFF core drain after firmware=");klog(status_name(engine.drain_status));klog("\n");
		uint32_t control;
		if(read32(&m,0x610490,&control)) {
			klog("nouveau: HANDOFF core after drain=0x");hex32(control);klog("\n");
		}
	}
	klog("nouveau: HANDOFF take=");klog(status_name(status));klog("\n");
	if(status==NOUVEAU_ENGINE_BUSY) blocker_log(&engine);
	if(status==NOUVEAU_ENGINE_BUSY && !engine.snapshot_valid) {
		uint32_t firmware,supervisor;
		if(read32(&m,0x6194e8,&firmware) && read32(&m,0x6100ac,&supervisor)) {
			klog("nouveau: HANDOFF blocked firmware=0x");hex32(firmware);
			klog(" supervisor=0x");hex32(supervisor);klog("\n");
		}
	}
	if(status==NOUVEAU_ENGINE_OK) {
		status=nouveau_engine_capabilities(&engine);
		klog("nouveau: HANDOFF capabilities=");klog(status_name(status));klog("\n");
		if(status==NOUVEAU_ENGINE_BUSY) blocker_log(&engine);
	}
	if(engine.snapshot_valid) {
		if(nouveau_engine_restore(&engine)!=NOUVEAU_ENGINE_OK) {
			/* Keep every mapping and the transaction alive until reset. */
			klog("nouveau: HANDOFF rollback failed; GPU quarantined\n");kernel_halt();
		}
		klog("nouveau: HANDOFF restored; no VRAM upload/channel start\n");
	}
out:
	for(unsigned i=REGIONS;i>0;i--)
		if(m.leases[i-1].generation && !pci_mmio_release(&m.leases[i-1])) {
			klog("nouveau: HANDOFF MMIO release failed\n");kernel_halt();
		}
}
