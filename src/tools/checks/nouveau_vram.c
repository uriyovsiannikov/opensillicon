/* PRAMIN boundary, IO failure/restore and native lease lifecycle checks. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/nouveau_vram.h"
struct model { uint32_t window,saved;unsigned reads,writes,fail_read,fail_write,bad_readback; };
static uint32_t pattern(uint64_t address){return (uint32_t)(address/4)^0xa5a55a5a;}
static bool read_model(void *context,uint32_t reg,uint32_t *value)
{
	struct model *m=context;m->reads++;
	if(m->reads==m->fail_read)return false;
	if(reg==0x1700){*value=m->window;if(m->reads==m->bad_readback)*value^=16;return true;}
	assert(reg>=0x700000&&reg<0x800000&&!(reg&3));
	*value=pattern(((uint64_t)m->window<<16)+(reg-0x700000));return true;
}
static bool write_model(void *context,uint32_t reg,uint32_t value)
{
	struct model *m=context;assert(reg==0x1700);m->writes++;m->window=value;
	/* Failed writes may already have reached hardware. */
	return m->writes!=m->fail_write;
}
static uint32_t aperture[0x100000/4],control;
static unsigned maps,map_calls,address_calls,fail_map,fail_address,allocations;
static bool fail_alloc;
static unsigned decoded_contexts,decoded_bar_pdbs;
static const struct pci_device_lease claim={.generation=1};
static const struct pci_bar_resource bar={.type=PCI_BAR_MEMORY32,.base=0xa3000000,.aperture_size=0x1000000};
static bool map_common(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t offset,size_t length,struct pci_mmio_lease *lease,bool writable)
{
	assert(c==&claim&&b==&bar);map_calls++;
	if(map_calls==fail_map)return false;
	void *p;
	if(offset==0x1700){assert(length==4&&writable);p=&control;}
	else {assert(offset==0x700000&&length==0x100000&&!writable);p=aperture;}
	lease->generation=1;lease->address=(uintptr_t)p;maps++;return true;
}
bool pci_mmio_map(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t offset,size_t length,struct pci_mmio_lease *lease)
{return map_common(c,b,offset,length,lease,true);}
bool pci_mmio_map_readonly(const struct pci_device_lease *c,const struct pci_bar_resource *b,
	uint64_t offset,size_t length,struct pci_mmio_lease *lease)
{return map_common(c,b,offset,length,lease,false);}
bool pci_mmio_lease_address(const struct pci_mmio_lease *lease,volatile void **address)
{
	assert(lease->generation);address_calls++;
	if(address_calls==fail_address)return false;
	*address=(volatile void *)(uintptr_t)lease->address;return true;
}
bool pci_mmio_release(struct pci_mmio_lease *lease)
{assert(lease->generation&&maps);lease->generation=0;maps--;return true;}
void *kheap_alloc(size_t size)
{
	assert((size==NOUVEAU_INSTANCE_SIZE||size==4096)&&control==0x200);
	if(fail_alloc)return NULL;
	void *p=malloc(size);assert(p);allocations++;return p;
}
bool kheap_free(void *p)
{assert(allocations==1&&!maps&&control==0x200);allocations--;free(p);return true;}
void klog(const char *s){if(!strcmp(s," VRAM start=0x"))decoded_contexts++;if(!strcmp(s,"0000000011223c00"))decoded_bar_pdbs++;}
void klog_u64(uint64_t value){(void)value;}
_Noreturn void kernel_halt(void){abort();}
static void reset(void)
{
	assert(!maps&&!allocations);control=0x200;
	map_calls=address_calls=fail_map=fail_address=decoded_contexts=decoded_bar_pdbs=0;fail_alloc=false;
}

struct compare_model { unsigned cpu_calls,gpu_calls,fail_cpu,fail_gpu;bool flat,change,wrong; };
static bool compare_cpu(void *context,uint64_t offset,uint32_t *words,size_t count)
{
	struct compare_model *m=context;assert(count==16&&offset+64<=8192);m->cpu_calls++;
	if(m->cpu_calls==m->fail_cpu)return false;
	for(unsigned i=0;i<count;i++)words[i]=m->flat?7:pattern(offset+i*4);
	if(m->change && !(m->cpu_calls&1)) words[0]^=1;
	return true;
}
static bool compare_gpu(void *context,uint64_t address,uint32_t *words,size_t count)
{
	struct compare_model *m=context;assert(count==16&&address>=0x1000000);m->gpu_calls++;
	if(m->gpu_calls==m->fail_gpu)return false;
	for(unsigned i=0;i<count;i++)words[i]=m->flat?7:pattern(address-0x1000000+i*4);
	if(m->wrong && address==0x1000000+8192-64) words[15]^=1;
	return true;
}
bool framebuffer_device_boot_read(uint64_t offset,uint32_t *words,size_t count)
{
	assert(control==0x200&&count==16&&offset+64<=8192);
	for(unsigned i=0;i<count;i++)words[i]=pattern(offset+i*4);
	return true;
}
int main(void)
{
	uint32_t words[16];struct model m={.window=0x200,.saved=0x200};
	struct nouveau_pramin_io io={.context=&m,.read32=read_model,.write32=write_model};
	assert(nouveau_vram_read(&io,0x400000,0xffff0,words,sizeof(words))==NOUVEAU_VRAM_OK);
	for(unsigned i=0;i<16;i++)assert(words[i]==pattern(0xffff0+i*4));
	assert(m.window==m.saved&&m.writes==3);unsigned total_reads=m.reads;
	for(unsigned i=1;i<=total_reads;i++){
		m=(struct model){.window=0x200,.saved=0x200,.fail_read=i};
		enum nouveau_vram_status status=nouveau_vram_read(&io,0x400000,0xffff0,words,sizeof(words));
		assert(status==(i==total_reads?NOUVEAU_VRAM_RESTORE_FAILED:NOUVEAU_VRAM_IO_FAILED));
		assert(m.window==m.saved);
	}
	for(unsigned i=1;i<=3;i++){
		m=(struct model){.window=0x200,.saved=0x200,.fail_write=i};
		assert(nouveau_vram_read(&io,0x400000,0xffff0,words,sizeof(words))==
		    (i==3?NOUVEAU_VRAM_RESTORE_FAILED:NOUVEAU_VRAM_IO_FAILED));
		assert(m.window==m.saved);
	}
	m=(struct model){.window=0x200,.saved=0x200,.bad_readback=2};
	assert(nouveau_vram_read(&io,0x400000,0xffff0,words,sizeof(words))==NOUVEAU_VRAM_IO_FAILED&&m.window==m.saved);
	m=(struct model){.window=0x200,.saved=0x200,.bad_readback=total_reads};
	assert(nouveau_vram_read(&io,0x400000,0xffff0,words,sizeof(words))==NOUVEAU_VRAM_RESTORE_FAILED);
	m=(struct model){.window=0,.saved=0};
	assert(nouveau_vram_read(&io,0x400000,0,words,sizeof(words))==NOUVEAU_VRAM_OK&&!m.writes);
	m=(struct model){.window=0x200,.saved=0x200};
	assert(nouveau_vram_read(&io,UINT64_C(1)<<40,(UINT64_C(1)<<40)-64,words,64)==NOUVEAU_VRAM_OK);
	assert(words[15]==pattern((UINT64_C(1)<<40)-4)&&m.window==m.saved);
	m=(struct model){.window=0x200,.saved=0x200};
	assert(nouveau_vram_read(&io,64,0,words,64)==NOUVEAU_VRAM_OK);
	assert(nouveau_vram_read(&io,64,4,words,64)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,64,1,words,4)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,64,0,words,3)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,0,0,words,4)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,UINT64_MAX,0,words,4)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,0x400000,0,words,65540)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(&io,64,0,(uint32_t *)((uint8_t *)words+1),4)==NOUVEAU_VRAM_INVALID);
	assert(nouveau_vram_read(NULL,64,0,words,4)==NOUVEAU_VRAM_INVALID);
	struct nouveau_display_snapshot s={.instance=0x10009,.head_count=1};
	s.base[0].dma[0]=0xf0000001; /* Lookup must be bounded even with opaque firmware data. */
	for(unsigned i=0;i<sizeof(aperture)/4;i++)aperture[i]=0xdeadbeef;
	/* Handle f0000001 folds to 31; base channel 1 XORs 32 -> slot 63.
	 * Firmware object above f000 must be readable despite staging reservation. */
	aperture[63*2]=0xf0000001;aperture[63*2+1]=(1U<<27)|(0xf020U<<9)|1;
	const uint32_t object[]={0x49,0x1000,0x100f,0,0,0};
	memcpy(aperture+0xf020/4,object,sizeof(object));
	uint32_t *copy=malloc(sizeof(aperture));assert(copy);memcpy(copy,aperture,sizeof(aperture));
	reset();nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));
	assert(control==0x200&&!maps&&!allocations&&map_calls==2);
	assert(decoded_contexts==1&&!memcmp(aperture,copy,sizeof(aperture)));free(copy);
	for(unsigned i=1;i<=2;i++){
		reset();fail_map=i;nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));assert(control==0x200&&!maps&&!allocations);
		reset();fail_address=i;nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));assert(control==0x200&&!maps&&!allocations);
	}
	reset();fail_alloc=true;nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));assert(control==0x200&&!maps&&!allocations);
	reset();s.instance=0;nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));assert(!map_calls);
	s.instance=0x10009;nouveau_firmware_instance_log(&claim,&bar,&s,0x100000);assert(!map_calls);
	/* BAR VM context capture is independent from display instance=1. */
	aperture[0x200/4]=0x11223c00;aperture[0x204/4]=0;
	aperture[0x208/4]=0x1ffffff;aperture[0x20c/4]=0;
	s.instance=1;reset();
	nouveau_firmware_instance_log(&claim,&bar,&s,UINT64_C(0x100000000));assert(!map_calls);
	nouveau_firmware_bar_log(&claim,&bar,3,0x80001000,UINT64_C(0x100000000));
	assert(map_calls==2&&!maps&&!allocations&&control==0x200&&decoded_bar_pdbs==1);
	reset();nouveau_firmware_bar_log(&claim,&bar,3,0xc0001000,UINT64_C(0x100000000));
	assert(map_calls==2&&!maps&&!allocations&&control==0x200&&decoded_bar_pdbs==1);
	for(unsigned i=1;i<=2;i++) {
		reset();fail_map=i;nouveau_firmware_bar_log(&claim,&bar,1,0x80001000,UINT64_C(0x100000000));
		assert(!maps&&!allocations&&control==0x200);
		reset();fail_address=i;nouveau_firmware_bar_log(&claim,&bar,3,0x80001000,UINT64_C(0x100000000));
		assert(!maps&&!allocations&&control==0x200);
	}
	reset();fail_alloc=true;nouveau_firmware_bar_log(&claim,&bar,3,0x80001000,UINT64_C(0x100000000));
	assert(!maps&&!allocations&&control==0x200);
	reset();nouveau_firmware_bar_log(&claim,&bar,1,1,UINT64_C(0x100000000));assert(!map_calls);
	nouveau_firmware_bar_log(&claim,&bar,1,0xc0001000,UINT64_C(0x100000000));assert(!map_calls);
	nouveau_firmware_bar_log(&claim,&bar,3,0xb0001000,UINT64_C(0x100000000));assert(!map_calls);
	nouveau_firmware_bar_log(&claim,&bar,3,0x80001000,0x100000);assert(!map_calls);
	struct compare_model compare={0};
	struct nouveau_scanout_reader reader={.context=&compare,.cpu=compare_cpu,.gpu=compare_gpu};
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_SAMPLE_MATCH);
	assert(compare.cpu_calls==256&&compare.gpu_calls==128);
	compare=(struct compare_model){.wrong=true};
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_MISMATCH);
	assert(compare.gpu_calls==128); /* Must include final 64 bytes of the span. */
	compare=(struct compare_model){.flat=true};
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_FLAT);
	compare=(struct compare_model){.change=true};
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_UNSTABLE);
	for(unsigned i=1;i<=256;i++) {
		compare=(struct compare_model){.fail_cpu=i};
		assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_IO_FAILED);
	}
	for(unsigned i=1;i<=128;i++) {
		compare=(struct compare_model){.fail_gpu=i};
		assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,8192)==NOUVEAU_SCANOUT_IO_FAILED);
	}
	assert(nouveau_scanout_compare(&reader,0x1001000,0x1000000,8192)==NOUVEAU_SCANOUT_INVALID);
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000001,8192)==NOUVEAU_SCANOUT_INVALID);
	assert(nouveau_scanout_compare(&reader,0x4000000,0x1000000,0)==NOUVEAU_SCANOUT_INVALID);
	/* Native reader restores the register before each second CPU sample. */
	for(unsigned i=0;i<sizeof(aperture)/4;i++)aperture[i]=pattern(i*4);
	reset();nouveau_scanout_probe_log(&claim,&bar,0x40000000,UINT64_C(0x100000000),0x1000000,8192);
	assert(!maps&&!allocations&&control==0x200&&map_calls==2);
	for(unsigned i=0;i<sizeof(aperture)/4;i++)assert(aperture[i]==pattern(i*4));
	for(unsigned i=1;i<=2;i++) {
		reset();fail_map=i;nouveau_scanout_probe_log(&claim,&bar,0x40000000,UINT64_C(0x100000000),0x1000000,8192);
		assert(!maps&&!allocations&&control==0x200);
		reset();fail_address=i;nouveau_scanout_probe_log(&claim,&bar,0x40000000,UINT64_C(0x100000000),0x1000000,8192);
		assert(!maps&&!allocations&&control==0x200);
	}
	reset();nouveau_scanout_probe_log(&claim,&bar,0x80000000,UINT64_C(0x100000000),0x1000000,8192);assert(!map_calls);
	puts("NOUVEAU_VRAM_PASS: PRAMIN window crossing, 40-bit bounds, every IO failure, restore verification, native readonly aperture/cleanup, scanout sample match/mismatch/flat/unstable and every read failure");
}
