/* SPDX-License-Identifier: BSD-2-Clause
 * Reversible GP107 instance upload/bind. Does not start channels. */
#include "kernel/nouveau_engine.h"
#include "kernel/nouveau_channel.h"
#include "kernel/nouveau_memory.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "arch/x86_64/pci_resource.h"

struct upload_mapping {
	volatile uint32_t *window,*words;
	struct nouveau_engine *engine;
	uint64_t base;
	uint32_t saved_window;
};
static bool owns(void *context,uint64_t address,uint64_t size)
{
	struct upload_mapping *m=context;struct nouveau_engine *e=m->engine;
	if(!e || !e->snapshot_valid || !e->io || !e->io->read32 || !e->caps_ready ||
	    !((e->state==NOUVEAU_ENGINE_QUIESCENT && !e->instance && !e->instance_touched) ||
	    (e->state==NOUVEAU_ENGINE_QUARANTINED && e->instance && e->instance->address==m->base)) ||
	    address!=m->base || size!=NOUVEAU_INSTANCE_SIZE) return false;
	uint32_t value;
	if(!e->io->read32(e->io->context,0x1704,&value) || value ||
	    !e->io->read32(e->io->context,0x1714,&value) || value!=0x40000000 ||
	    !e->io->read32(e->io->context,0x6194e8,&value) || value==UINT32_MAX || (value&3) ||
	    !e->io->read32(e->io->context,0x610010,&value) || value!=e->saved_instance) return false;
	for(unsigned i=0;i<17;i++)
		if(!e->io->read32(e->io->context,0x610490+i*16,&value) || value==UINT32_MAX ||
		    (value&0x801e0003)) return false;
	return true;
}
static bool select_word(struct upload_mapping *m,uint64_t address,size_t *index)
{
	if(address<m->base || address-m->base>=NOUVEAU_INSTANCE_SIZE || (address&3)) return false;
	uint32_t window=(uint32_t)((address&~UINT64_C(0xfffff))>>16);
	if(*m->window!=window) {
		*m->window=window;
		if(*m->window!=window) return false;
	}
	*index=(address&0xfffff)/4;return true;
}
static bool read_word(void *context,uint64_t address,uint32_t *value)
{
	struct upload_mapping *m=context;size_t index;
	if(!select_word(m,address,&index)) return false;
	*value=m->words[index];return true;
}
static bool write_word(void *context,uint64_t address,uint32_t value)
{
	struct upload_mapping *m=context;size_t index;
	if(!select_word(m,address,&index)) return false;
	m->words[index]=value;return true;
}
static void fatal(const char *message)
{
	klog(message);kernel_halt();
}
void nouveau_instance_upload_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar,struct nouveau_engine *engine,
	const struct nouveau_upload_probe_layout *layout)
{
	/* Narrow initial hardware profile: no previous display instance or BAR
	 * VM objects, one firmware head, sampled scanout at VRAM zero. Sampling
	 * is not an MMU translation; protect the entire first 64MiB regardless. */
	if(!layout || !layout->snapshot || !engine || engine->saved_instance!=1 ||
	    layout->head || layout->scanout_sample || !layout->scanout_size ||
	    layout->scanout_size>0x1000000 || layout->vram<=0x5000000 ||
	    layout->vram>(UINT64_C(1)<<40) ||
	    layout->snapshot->bar1_instance || layout->snapshot->bar3_instance!=0x40000000 ||
	    !layout->snapshot->heads[0].arm.timing_valid) {
		klog("nouveau: UPLOAD profile unsupported; no VRAM writes\n");return;
	}
	for(unsigned i=1;i<4;i++) if(layout->snapshot->heads[i].arm.timing_valid ||
	    layout->snapshot->heads[i].asy.timing_valid || layout->snapshot->heads[i].supervisor_mask) {
		klog("nouveau: UPLOAD extra scanout active; no VRAM writes\n");return;
	}
	struct nouveau_vram_pool pool={0};struct nouveau_vram_allocation allocation={0};
	struct nouveau_vram_range excluded[]={{0,0x4000000},{layout->vram-0x1000000,0x1000000}},range;
	struct pci_mmio_lease leases[2]={{0}};struct upload_mapping m={.engine=engine};
	uint32_t *image=NULL,*backup=NULL;bool window_saved=false;
	struct nouveau_upload upload={0};
	struct nouveau_upload_io io={&m,read_word,write_word,owns};
	if(!nouveau_vram_pool_init(&pool,layout->vram,excluded,2) ||
	    !nouveau_vram_pool_alloc(&pool,NOUVEAU_INSTANCE_SIZE,NOUVEAU_INSTANCE_SIZE,&allocation) ||
	    !nouveau_vram_pool_query(allocation,&range)) goto out;
	m.base=range.start;
	if(!owns(&m,m.base,NOUVEAU_INSTANCE_SIZE)) {
		klog("nouveau: UPLOAD engine ownership unavailable\n");goto out;
	}
	image=kheap_alloc(NOUVEAU_INSTANCE_SIZE);backup=kheap_alloc(NOUVEAU_INSTANCE_SIZE);
	uint32_t planned_instance;
	if(!image || !backup ||
	    !nouveau_instance_prepare(image,NOUVEAU_INSTANCE_SIZE,layout->vram,layout->head) ||
	    !nouveau_instance_relocate(image,NOUVEAU_INSTANCE_SIZE,layout->vram,m.base,
	    NOUVEAU_INSTANCE_SIZE,&planned_instance)) {
		klog("nouveau: UPLOAD CPU staging unavailable; no VRAM writes\n");goto out;
	}
	volatile void *address;
	if(!pci_mmio_map(claim,bar,0x1700,4,&leases[0]) ||
	    !pci_mmio_lease_address(&leases[0],&address)) goto out;
	m.window=address;m.saved_window=*m.window;
	if(m.saved_window==UINT32_MAX) goto out;
	window_saved=true;
	if(!pci_mmio_map(claim,bar,0x700000,0x100000,&leases[1]) ||
	    !pci_mmio_lease_address(&leases[1],&address)) goto out;
	m.words=address;
	enum nouveau_upload_status status=nouveau_upload_save(&upload,&io,allocation,backup,NOUVEAU_INSTANCE_SIZE);
	if(status!=NOUVEAU_UPLOAD_OK) {
		klog("nouveau: UPLOAD backup unavailable; no VRAM writes\n");goto out;
	}
	/* Upload owns the allocation from here; finish frees it exactly once. */
	allocation=(struct nouveau_vram_allocation){0};
	status=nouveau_upload_image(&upload,image);
	if(status==NOUVEAU_UPLOAD_OK) {
		klog("nouveau: UPLOAD instance/RAMHT verified bytes=65536 (not bound)\n");
		enum nouveau_engine_status bound=nouveau_engine_bind(engine,&upload);
		if(bound==NOUVEAU_ENGINE_OK) {
			klog("nouveau: BIND instance readback verified (channels stopped)\n");
			if(layout->test_core_channel)nouveau_core_channel_probe_log(claim,bar,engine);
			else klog("nouveau: CORECHANNEL disabled (experimental start/stop)\n");
		} else {
			klog("nouveau: BIND rejected/failed; restoring original instance\n");
		}
		if(engine->instance) {
			/* May have reached hardware even if bind reported failure. */
			if(nouveau_engine_unbind(engine)!=NOUVEAU_ENGINE_OK)
				fatal("nouveau: BIND unbind/VRAM restore failed; firmware held\n");
			klog("nouveau: BIND original instance restored and verified\n");
		} else if(nouveau_upload_restore(&upload)!=NOUVEAU_UPLOAD_OK) {
			fatal("nouveau: UPLOAD VRAM restore failed; firmware held\n");
		}
		klog("nouveau: UPLOAD original VRAM restored and verified\n");
	} else if(status==NOUVEAU_UPLOAD_RESTORE_FAILED) {
		fatal("nouveau: UPLOAD rollback failed; firmware held\n");
	} else {
		klog("nouveau: UPLOAD rejected or failed; original VRAM retained\n");
	}
	if(!nouveau_upload_finish(&upload)) fatal("nouveau: UPLOAD allocation cleanup failed\n");
out:
	/* Restore PRAMIN before handing engine control back to firmware. */
	if(window_saved) {
		*m.window=m.saved_window;
		if(*m.window!=m.saved_window) fatal("nouveau: UPLOAD PRAMIN restore failed\n");
		klog("nouveau: UPLOAD PRAMIN window restored\n");
	}
	for(unsigned i=2;i>0;i--)
		if(leases[i-1].generation && !pci_mmio_release(&leases[i-1])) fatal("nouveau: UPLOAD MMIO cleanup failed\n");
	if(allocation.generation && !nouveau_vram_pool_free(&allocation)) fatal("nouveau: UPLOAD range cleanup failed\n");
	if(backup && !kheap_free(backup)) fatal("nouveau: UPLOAD backup cleanup failed\n");
	if(image && !kheap_free(image)) fatal("nouveau: UPLOAD image cleanup failed\n");
}
