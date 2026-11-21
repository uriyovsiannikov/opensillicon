/* SPDX-License-Identifier: BSD-2-Clause
 * Empty GP102 core queue: PUT stays zero, no methods are submitted. */
#include "kernel/nouveau_channel.h"
#include "kernel/nouveau_engine.h"
#include "kernel/dma.h"
#include "kernel/log.h"
#include "arch/x86_64/pci_resource.h"

struct core_probe {
	struct nouveau_engine *engine;
	struct dma_buffer push;
	uint64_t device_address;
	volatile uint32_t *descriptor,*put;
	uint32_t saved_descriptor[3],saved_put,saved_control;
	bool held;
};
static void hex32(uint32_t value)
{
	static const char digits[]="0123456789abcdef";char text[9];
	for(unsigned i=0;i<8;i++)text[i]=digits[(value>>((7-i)*4))&15];
	text[8]=0;klog(text);
}
static bool engine_bound(struct core_probe *p)
{
	struct nouveau_engine *e=p->engine;uint32_t value;
	return e && e->snapshot_valid && e->caps_ready && e->io && e->io->read32 &&
	    e->io->write32 && e->io->time_us && e->io->delay_us &&
	    e->state==NOUVEAU_ENGINE_BOUND && e->instance_touched && e->instance &&
	    e->instance->state==NOUVEAU_UPLOAD_PINNED &&
	    e->io->read32(e->io->context,0x6194e8,&value) && value!=UINT32_MAX && !(value&3) &&
	    e->io->read32(e->io->context,0x610010,&value) &&
	    value==((uint32_t)(e->instance->address>>8)|9);
}
static bool read_reg(void *ctx,uint32_t reg,uint32_t *value)
{
	struct core_probe *p=ctx;
	if(reg==0x640000) {*value=*p->put;return true;}
	if(reg!=0x610490)return false;
	return p->engine->io->read32(p->engine->io->context,reg,value);
}
static bool write_reg(void *ctx,uint32_t reg,uint32_t value)
{
	struct core_probe *p=ctx;
	if(!p->held || !engine_bound(p))return false;
	if(reg>=0x611494 && reg<=0x61149c && !(reg&3)) {
		p->descriptor[(reg-0x611494)/4]=value;return true;
	}
	/* This adapter cannot expose a nonzero PUT/doorbell, even by mistake. */
	if(reg==0x640000 && !value) {*p->put=0;return true;}
	if(reg!=0x610490)return false;
	return p->engine->io->write32(p->engine->io->context,reg,value);
}
static bool time_us(void *ctx,uint64_t *value)
{
	struct core_probe *p=ctx;
	return p->engine->io->time_us(p->engine->io->context,value);
}
static bool delay_us(void *ctx,unsigned value)
{
	struct core_probe *p=ctx;
	return p->engine->io->delay_us(p->engine->io->context,value);
}
static bool acquire(void *ctx)
{
	struct core_probe *p=ctx;uint32_t value;
	if(p->held || !engine_bound(p))return false;
	for(unsigned i=0;i<17;i++)
		if(!p->engine->io->read32(p->engine->io->context,0x610490+i*16,&value) ||
		    value==UINT32_MAX || (value&0x801e0003))return false;
	uint64_t address;
	if(!dma_device_acquire(p->push,&address))return false;
	if(address!=p->device_address) {
		if(!dma_device_complete(p->push)) {klog("nouveau: CORECHANNEL DMA rollback failed\n");kernel_halt();}
		return false;
	}
	p->held=true;return true;
}
static bool release(void *ctx)
{
	struct core_probe *p=ctx;uint32_t control;
	if(!p->held || !engine_bound(p) ||
	    !read_reg(p,0x610490,&control) || (control&0x801e0003))return false;
	/* Restore device pointers before giving push storage back to the CPU.
	 * Status bits are hardware state, not writable configuration snapshots. */
	for(unsigned i=0;i<3;i++)p->descriptor[i]=p->saved_descriptor[i];
	*p->put=p->saved_put;
	const uint32_t requests=0x01000013;
	if(!write_reg(p,0x610490,(control&~requests)|(p->saved_control&requests)) ||
	    !read_reg(p,0x610490,&control) || (control&0x801e0003) ||
	    (control&requests)!=(p->saved_control&requests) || *p->put!=p->saved_put)return false;
	for(unsigned i=0;i<3;i++)if(p->descriptor[i]!=p->saved_descriptor[i])return false;
	if(!dma_device_complete(p->push))return false;
	p->held=false;return true;
}
void nouveau_core_channel_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,struct nouveau_engine *engine)
{
	struct core_probe p={.engine=engine};struct pci_mmio_lease leases[2]={{0}};
	struct nouveau_channel channel={0};
	struct nouveau_channel_io io={&p,read_reg,write_reg,time_us,delay_us,acquire,release};
	if(!engine_bound(&p) || !dma_identity_addressing_available()) {
		klog("nouveau: CORECHANNEL bound instance/HOST DMA unavailable\n");return;
	}
	if(!dma_alloc(4096,4096,40,&p.push)) {
		klog("nouveau: CORECHANNEL push allocation unavailable\n");return;
	}
	struct dma_buffer_info info;
	if(!dma_buffer_query(p.push,&info) || info.owner!=DMA_OWNER_CPU ||
	    !info.cpu_virtual_address || ((uintptr_t)info.cpu_virtual_address&3) ||
	    info.requested_size!=4096 || info.allocated_size<4096 || (info.device_address&4095) ||
	    info.device_address>=(UINT64_C(1)<<40) || 4096>(UINT64_C(1)<<40)-info.device_address)goto out;
	p.device_address=info.device_address;
	for(unsigned i=0;i<1024;i++)((uint32_t *)info.cpu_virtual_address)[i]=0;
	volatile void *address;
	if(!pci_mmio_map(claim,bar,0x611494,12,&leases[0]) ||
	    !pci_mmio_lease_address(&leases[0],&address)) {
		klog("nouveau: CORECHANNEL descriptor MMIO unavailable\n");goto out;
	}
	p.descriptor=address;
	if(!pci_mmio_map(claim,bar,0x640000,4,&leases[1]) ||
	    !pci_mmio_lease_address(&leases[1],&address)) {
		klog("nouveau: CORECHANNEL PUT MMIO unavailable\n");goto out;
	}
	p.put=address;p.saved_put=*p.put;
	for(unsigned i=0;i<3;i++) {
		p.saved_descriptor[i]=p.descriptor[i];
		if(p.saved_descriptor[i]==UINT32_MAX)goto out;
	}
	if(p.saved_put==UINT32_MAX || !read_reg(&p,0x610490,&p.saved_control) ||
	    p.saved_control==UINT32_MAX || (p.saved_control&0x801e1013) ||
	    !nouveau_channel_prepare(&channel,0,(uint32_t)(p.device_address>>8)|3,0))goto out;
	enum nouveau_channel_status status=nouveau_channel_start(&channel,&io);
	uint32_t control;
	klog("nouveau: CORECHANNEL start status=0x");hex32(status);
	if(read_reg(&p,0x610490,&control)) {klog(" control=0x");hex32(control);}
	klog(" PUT=0x");hex32(*p.put);klog("\n");
	if(status==NOUVEAU_CHANNEL_OK && *p.put==0 && read_reg(&p,0x610490,&control) &&
	    control!=UINT32_MAX && !(control&0x80000000) &&
	    p.descriptor[0]==((uint32_t)(p.device_address>>8)|3) &&
	    p.descriptor[1]==0x10000 && p.descriptor[2]==1) {
		klog("nouveau: CORECHANNEL running empty PUT=0 (no methods submitted)\n");
	} else klog("nouveau: CORECHANNEL start failed; stopping before rollback\n");
	if(channel.resources_held) {
		if(nouveau_channel_stop(&channel,&io)!=NOUVEAU_CHANNEL_OK ||
		    nouveau_channel_release(&channel,&io)!=NOUVEAU_CHANNEL_OK) {
			klog("nouveau: CORECHANNEL stop/restore failed; DMA and instance retained\n");kernel_halt();
		}
		klog("nouveau: CORECHANNEL stopped; descriptors/PUT restored; DMA returned\n");
	}
out:
	for(unsigned i=2;i>0;i--)
		if(leases[i-1].generation && !pci_mmio_release(&leases[i-1])) {
			klog("nouveau: CORECHANNEL MMIO cleanup failed\n");kernel_halt();
		}
	if(p.push.generation && !dma_free(p.push)) {
		klog("nouveau: CORECHANNEL push cleanup failed\n");kernel_halt();
	}
}
