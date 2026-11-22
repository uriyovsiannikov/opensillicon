/* SPDX-License-Identifier: MIT
 * HDA diagnostics and one-shot PCM output test for confirmed analog routes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "arch/x86_64/pci_resource.h"
#include "kernel/hda_probe.h"
#include "kernel/timer.h"
#include "kernel/dma.h"
#include "kernel/hda_audio.h"
#include "kernel/fd.h"
#include "kernel/vfs.h"
#include "kernel/spinlock.h"
#include "kernel/smp.h"
#include "kernel/errno.h"
#include "abi/audio.h"

#define REPORT_SIZE 16384U
static char report[REPORT_SIZE];
static size_t used;
static bool busy, ready[2], truncated;
static char cached[2][REPORT_SIZE];
static size_t cached_length[2];
struct request { unsigned found; bool tone, attempted, stream; int error; };
struct probe { volatile uint8_t *mmio; uint64_t deadline; unsigned errors; };

static void text(const char *s)
{
    size_t n = strlen(s);
    if (truncated) return;
    if (n > sizeof(report) - 128 - used) { truncated = true; return; }
    memcpy(report + used, s, n); used += n;
}
static void hex(uint32_t value, unsigned digits)
{
    static const char chars[] = "0123456789abcdef";
    for (unsigned i = digits; i; --i) {
        char s[2] = {chars[(value >> ((i-1)*4)) & 15], 0}; text(s);
    }
}
static void field(const char *name, uint32_t value, unsigned digits)
{ text(name); hex(value, digits); }
static uint32_t r32(struct probe *p, unsigned offset)
{ return *(volatile uint32_t *)(p->mmio + offset); }
static uint16_t r16(struct probe *p, unsigned offset)
{ return *(volatile uint16_t *)(p->mmio + offset); }
static void w32(struct probe *p, unsigned offset, uint32_t value)
{ *(volatile uint32_t *)(p->mmio + offset) = value; }
static void w16(struct probe *p, unsigned offset, uint16_t value)
{ *(volatile uint16_t *)(p->mmio + offset) = value; }
static bool wait_bits(struct probe *p, unsigned offset, uint32_t mask, uint32_t value)
{
    uint64_t start = timer_monotonic_ns();
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((r32(p, offset) & mask) == value) return true;
        if (timer_monotonic_ns() - start >= UINT64_C(100000000) ||
            timer_monotonic_ns() >= p->deadline) break;
        if ((i & 1023) == 1023) timer_wait_for_interrupt();
        __asm__ volatile ("pause");
    }
    return false;
}
static uint32_t verb(struct probe *p, unsigned cad, unsigned nid, unsigned operation, unsigned param)
{
    if (p->errors >= 4 || timer_monotonic_ns() >= p->deadline) return UINT32_MAX;
    if (!wait_bits(p, 0x68, 1, 0)) { ++p->errors; return UINT32_MAX; }
    w16(p, 0x68, 2); /* Clear the old immediate-response-valid bit (W1C). */
    w32(p, 0x60, (cad << 28) | (nid << 20) | (operation << 8) | param);
    w16(p, 0x68, 1);
    if (!wait_bits(p, 0x68, 3, 2)) { ++p->errors; return UINT32_MAX; }
    uint32_t result = r32(p, 0x64);
    w16(p, 0x68, 2);
    return result;
}
static uint32_t parameter(struct probe *p, unsigned c, unsigned n, unsigned param)
{ return verb(p, c, n, 0xf00, param); }

static void describe_codec(struct probe *p, unsigned cad, uint32_t vendor)
{
    field("codec cad=", cad, 1); field(" vendor=", vendor, 8);
    field(" revision=", parameter(p,cad,0,2), 8); text("\n");
    uint32_t groups = parameter(p,cad,0,4);
    if (groups == UINT32_MAX) return;
    unsigned first = (groups >> 16) & 255, count = groups & 255;
    for (unsigned fg = first; fg < first + count && fg < 256; ++fg) {
        uint32_t type = parameter(p,cad,fg,5);
        if ((type & 255) != 1) continue;
        field(" AFG nid=",fg,2); field(" subsystem=",verb(p,cad,fg,0xf20,0),8);
        field(" PCM=",parameter(p,cad,fg,0xa),8); field(" formats=",parameter(p,cad,fg,0xb),8);text("\n");
        uint32_t nodes = parameter(p,cad,fg,4);
        if (nodes == UINT32_MAX) continue;
        unsigned start = (nodes >> 16) & 255, total = nodes & 255;
        for (unsigned n = start; n < start+total && n < 256 && p->errors < 4; ++n) {
            if (timer_monotonic_ns() >= p->deadline) break;
            uint32_t caps = parameter(p,cad,n,9);
            if (caps == UINT32_MAX) continue;
            unsigned kind = (caps >> 20) & 15;
            if (kind > 4) continue;
            const char *names[] = {" DAC", " ADC", " mixer", " selector", " pin"};
            text(names[kind]); field(" nid=",n,2);field(" wcaps=",caps,8);
            if (kind == 4) {
                field(" pincap=",parameter(p,cad,n,0xc),8);
                uint32_t cfg = verb(p,cad,n,0xf1c,0);
                field(" cfg=",cfg,8);
                field(" sense=",verb(p,cad,n,0xf09,0),8);
                if (((cfg >> 24) & 15) == 1) text(" REAR");
                if (((cfg >> 20) & 15) == 0) text(" LINE-OUT");
                if (((cfg >> 12) & 15) == 4) text(" GREEN");
            }
            if (caps & 2) field(" inamp=",parameter(p,cad,n,0xd),8);
            if (caps & 4) field(" outamp=",parameter(p,cad,n,0x12),8);
            text("\n");
            if (!(caps & (1U << 8))) continue;
            uint32_t length = parameter(p,cad,n,0xe);
            if (length == UINT32_MAX) continue;
            unsigned entries = length & 127, step = (length & 128) ? 2 : 4;
            field("  connections nid=",n,2);text((length & 128) ? " long:" : " short:");
            for (unsigned j = 0; j < entries && p->errors < 4; j += step) {
                text(" ");hex(verb(p,cad,n,0xf02,j),8);
            }
            text("\n");
        }
    }
}

struct tone_route { unsigned pin, mixer, dac; };

static bool set_verb(struct probe *p, unsigned cad, unsigned nid, unsigned op, unsigned value)
{
    unsigned old = p->errors;
    uint32_t response = verb(p,cad,nid,op,value);
    return old == p->errors && response != UINT32_MAX;
}

static bool pci_set_command(const struct pci_device_info *d, uint16_t value)
{
    uint32_t readback;
    return pci_config_write(d->segment,d->bus,d->device,d->function,4,16,value) &&
        pci_config_read(d->segment,d->bus,d->device,d->function,4,16,&readback) &&
        readback == value;
}

static bool route_valid(struct probe *p, unsigned cad, uint32_t vendor, struct tone_route *r)
{
    if (vendor == 0x10ec0235) *r = (struct tone_route){0x1b,0x0c,0x02};
    else if (vendor == 0x1af40022) *r = (struct tone_route){0x03,0,0x02};
    else {text("Unsupported codec for test playback\n");return false;}
    uint32_t pin = parameter(p,cad,r->pin,9), dac = parameter(p,cad,r->dac,9);
    uint32_t cfg = verb(p,cad,r->pin,0xf1c,0);
    if (((pin>>20)&15)!=4 || ((dac>>20)&15)!=0 || !(dac&1) ||
        !(parameter(p,cad,r->pin,0xc)&0x10) || (cfg>>30)!=0 ||
        ((cfg>>20)&15)!=0 || ((cfg>>12)&15)!=4 ||
        (vendor==0x10ec0235 && ((cfg>>24)&15)!=1)) return false;
    uint32_t connections = parameter(p,cad,r->pin,0xe);
    if (!connections || (connections&128) ||
        (verb(p,cad,r->pin,0xf02,0)&255)!=(r->mixer?r->mixer:r->dac)) return false;
    if (r->mixer) {
        uint32_t mix=parameter(p,cad,r->mixer,9), len=parameter(p,cad,r->mixer,0xe);
        if (((mix>>20)&15)!=2 || !len || (len&128) ||
            (verb(p,cad,r->mixer,0xf02,0)&255)!=r->dac) return false;
    }
    uint32_t pcm=parameter(p,cad,(dac&0x10)?r->dac:1,0xa);
    uint32_t formats=parameter(p,cad,(dac&0x10)?r->dac:1,0xb);
    return pcm!=UINT32_MAX && (pcm&(1U<<6)) && (pcm&(1U<<17)) && (formats&1);
}

static bool configure_route(struct probe *p,unsigned cad,const struct tone_route *r)
{
    if (!set_verb(p,cad,1,0x705,0)) return false;
    unsigned nodes[] = {r->dac,r->mixer,r->pin};
    for (unsigned i=0;i<3;++i) {
        unsigned n=nodes[i];if (!n) continue;
        uint32_t caps=parameter(p,cad,n,9);
        if ((caps&(1U<<10)) && !set_verb(p,cad,n,0x705,0)) return false;
    }
    timer_wait_for_interrupt();
    uint32_t amp=parameter(p,cad,r->dac,0x12);
    if (amp==UINT32_MAX) return false;
    unsigned gain=amp&127, steps=(amp>>8)&127;
    unsigned attenuation=24/(((amp>>16)&127)+1); /* Approximately -6 dB. */
    if (gain>steps) gain=steps;
    gain=gain>attenuation?gain-attenuation:0;
    if (!set_verb(p,cad,r->dac,0x300,0xb000|gain)) return false;
    if (r->mixer) {
        uint32_t count=parameter(p,cad,r->mixer,0xe)&127;
        if (count>16) return false;
        for(unsigned i=0;i<count;++i)
            if(!set_verb(p,cad,r->mixer,0x300,0x7000|(i<<8)|(i?0x80:0))) return false;
    }
    uint32_t pcaps=parameter(p,cad,r->pin,0xc), wcaps=parameter(p,cad,r->pin,9);
    if ((wcaps&4) && !set_verb(p,cad,r->pin,0x300,0xb000)) return false;
    if ((pcaps&(1U<<16)) && !set_verb(p,cad,r->pin,0x70c,2)) return false;
    if (!set_verb(p,cad,r->pin,0x701,0) || !set_verb(p,cad,r->pin,0x707,0x40) ||
        !set_verb(p,cad,r->dac,0x200,0x0011) || !set_verb(p,cad,r->dac,0x706,0x10)) return false;
    field("route DAC=",r->dac,2);field(" mixer=",r->mixer,2);field(" pin=",r->pin,2);
    field(" gain=",gain,2);text(" format=0011 (48000 Hz, stereo, signed 16-bit)\n");
    field(" pin-control=",verb(p,cad,r->pin,0xf07,0),2);
    field(" converter=",verb(p,cad,r->dac,0xf06,0),2);
    field(" format=",verb(p,cad,r->dac,0xa00,0),4);text("\n");
    return p->errors==0;
}

/* Return false only when hardware could not be stopped and leases must stay pinned. */
static bool play_tone(struct probe *p,const struct pci_device_info *d,unsigned cad,uint32_t vendor)
{
    struct tone_route route;
    if (!route_valid(p,cad,vendor,&route)) {text("Rear green output route validation failed\n");return true;}
    struct dma_buffer buffer={0};
    struct dma_buffer_info info;
    bool owned=false, started=false, route_touched=false;
    unsigned stream=0x80+((r16(p,0)>>8)&15)*0x20;
    const uint32_t pcm_bytes=48000*2*2*2, half=pcm_bytes/2;
    const char *stage="DMA boot contract";
    if (!dma_identity_addressing_available()) goto finish;
    stage="output stream availability";
    if (((r16(p,0)>>12)&15)==0) goto finish;
    stage="DMA allocation";
    if (!dma_alloc(4096+pcm_bytes,4096,(r16(p,0)&1)?64:32,&buffer) ||
        !dma_buffer_query(buffer,&info)) goto finish;
    uint32_t *bdl=info.cpu_virtual_address;
    int16_t *pcm=(int16_t *)((uint8_t *)info.cpu_virtual_address+4096);
    uint64_t address=info.device_address+4096;
    memset(info.cpu_virtual_address,0,4096+pcm_bytes);
    for(unsigned i=0;i<2;++i) {
        uint64_t a=address+(uint64_t)i*half;
        bdl[4*i]=(uint32_t)a;bdl[4*i+1]=(uint32_t)(a>>32);
        bdl[4*i+2]=half;bdl[4*i+3]=1;
    }
    static const int16_t sine[64] = {0,490,975,1451,1913,2357,2778,3172,3536,3865,4157,4410,4619,4785,4904,4976,5000,4976,4904,4785,4619,4410,4157,3865,3536,3172,2778,2357,1913,1451,975,490,0,-490,-975,-1451,-1913,-2357,-2778,-3172,-3536,-3865,-4157,-4410,-4619,-4785,-4904,-4976,-5000,-4976,-4904,-4785,-4619,-4410,-4157,-3865,-3536,-3172,-2778,-2357,-1913,-1451,-975,-490};
    uint32_t phase=0, increment=(uint32_t)((UINT64_C(440)<<32)/48000);
    for(unsigned i=0;i<48000*2;++i) {
        int sample=sine[phase>>26];
        unsigned ramp=i<480?i:(i>=48000*2-480?48000*2-1-i:480);
        sample=sample*(int)ramp/480;
        pcm[2*i]=pcm[2*i+1]=(int16_t)sample;phase+=increment;
    }
    stage="stream reset";
    w32(p,stream,1);
    if(!wait_bits(p,stream,1,1)) goto finish;
    w32(p,stream,0);
    if(!wait_bits(p,stream,1,0)) goto finish;
    stage="codec route";
    route_touched=true;
    if(!configure_route(p,cad,&route)) goto finish;
    stage="DMA ownership";
    uint64_t dma_address;
    if(!dma_device_acquire(buffer,&dma_address)) goto finish;
    owned=true;
    w32(p,0x70,0);w32(p,0x74,0); /* No DMA position buffer. */
    w32(p,stream+0x18,(uint32_t)dma_address);w32(p,stream+0x1c,(uint32_t)(dma_address>>32));
    w32(p,stream+8,pcm_bytes);w16(p,stream+0xc,1);w16(p,stream+0x12,0x11);
    *(volatile uint8_t *)(p->mmio+stream+3)=0x1c;
    w32(p,stream,1U<<20);
    stage="PCI bus mastering";
    if(!pci_set_command(d,d->command|6)) goto finish;
    stage="stream RUN";
    started=true;w32(p,stream,(1U<<20)|2);
    if(!wait_bits(p,stream,2,2)) goto finish;
    uint64_t start=timer_monotonic_ns();
    p->deadline=start+UINT64_C(3000000000);
    uint32_t maximum=0, last=0;unsigned errors=0;
    while(timer_monotonic_ns()-start<UINT64_C(2000000000)) {
        last=r32(p,stream+4);if(last>maximum)maximum=last;
        errors=*(volatile uint8_t *)(p->mmio+stream+3)&0x18;
        if(errors || timer_monotonic_ns()>=p->deadline) break;
        timer_wait_for_interrupt();
    }
    field("DMA max-position=",maximum,8);field(" final-position=",last,8);
    field(" stream-status=",*(volatile uint8_t *)(p->mmio+stream+3),2);text("\n");
    if(!maximum || errors) {stage="DMA progress/error";goto finish;}
    text("TONE DMA PASS: 440 Hz for 2 seconds; confirm sound at rear green jack\n");
    stage=NULL;
finish:
    bool stopped=true;
    if(started) {w32(p,stream,1U<<20);stopped=wait_bits(p,stream,2,0);}
    bool master_off=pci_set_command(d,(d->command|2)&~4U);
    if(route_touched) {
        p->errors=0;p->deadline=timer_monotonic_ns()+UINT64_C(500000000);
        (void)set_verb(p,cad,route.pin,0x707,0);
        (void)set_verb(p,cad,route.dac,0x300,0xb080);
        (void)set_verb(p,cad,route.dac,0x706,0);
        if(parameter(p,cad,route.pin,0xc)&(1U<<16)) (void)set_verb(p,cad,route.pin,0x70c,0);
    }
    if(stage) {text("Tone failed at: ");text(stage);text("\n");}
    if(!stopped && !master_off) {
        text("DMA could not be quiesced; buffers and controller leases retained until reboot\n");
        return false;
    }
    if(owned) (void)dma_device_complete(buffer);
    if(buffer.generation) (void)dma_free(buffer);
    return true;
}

#define AUDIO_PERIOD 4096U
#define AUDIO_PERIODS 8U
#define AUDIO_RING (AUDIO_PERIOD*AUDIO_PERIODS)
#define AUDIO_QUEUE (128U*1024U)
static struct spinlock audio_lock=SPINLOCK_INITIALIZER;
static struct {
    bool active, running, closing, fault, pinned;
    struct probe probe;
    struct pci_device_info device;
    struct pci_device_lease claim;
    struct pci_mmio_lease mapping;
    struct dma_buffer dma;
    struct dma_buffer_info info;
    struct tone_route route;
    struct vnode *node;
    unsigned cad, stream, retire, volume;
    unsigned valid[AUDIO_PERIODS];
    uint8_t queue[AUDIO_QUEUE];
    size_t head, count;
    uint64_t written, played, underruns, last_tick;
} audio;

static void fill_period(unsigned slot)
{
    unsigned n=audio.count<AUDIO_PERIOD?(unsigned)audio.count:AUDIO_PERIOD;
    uint8_t *target=(uint8_t *)audio.info.cpu_virtual_address+4096+slot*AUDIO_PERIOD;
    size_t first=AUDIO_QUEUE-audio.head;if(first>n)first=n;
    memcpy(target,audio.queue+audio.head,first);
    memcpy(target+first,audio.queue,n-first);
    audio.head=(audio.head+n)%AUDIO_QUEUE;audio.count-=n;
    memset(target+n,0,AUDIO_PERIOD-n);
    if(audio.volume!=100) {
        int16_t *samples=(int16_t *)target;
        for(unsigned i=0;i<n/2;++i)samples[i]=(int16_t)((int32_t)samples[i]*(int32_t)audio.volume/100);
    }
    audio.valid[slot]=n;
    __asm__ volatile("mfence" ::: "memory");
}

void hda_audio_tick(void)
{
    uint64_t flags=spinlock_lock_irqsave(&audio_lock);
    bool wake=false;
    if(!audio.active || !audio.running) goto done;
    struct probe *p=&audio.probe;
    uint64_t now=timer_monotonic_ns();
    unsigned status=*(volatile uint8_t *)(p->mmio+audio.stream+3);
    uint32_t pos=r32(p,audio.stream+4);
    /* If a whole ring elapsed without service, ownership can no longer be
     * inferred from LPIB alone. Stop rather than overwrite live DMA memory. */
    if(audio.closing || (status&0x18) || pos>=AUDIO_RING ||
        now-audio.last_tick>=UINT64_C(170000000) ||
        !(r32(p,audio.stream)&2)) {
        w32(p,audio.stream,1U<<20);
        audio.running=false;audio.fault=!audio.closing;wake=true;goto done;
    }
    audio.last_tick=now;
    unsigned current=pos/AUDIO_PERIOD;
    while(audio.retire!=current) {
        unsigned slot=audio.retire;
        audio.played+=audio.valid[slot];
        if(audio.valid[slot]==0 && audio.played<audio.written) ++audio.underruns;
        fill_period(slot);
        audio.retire=(slot+1)%AUDIO_PERIODS;
        wake=true;
    }
    *(volatile uint8_t *)(p->mmio+audio.stream+3)=0x1c;
    if(audio.played==audio.written) {
        w32(p,audio.stream,1U<<20);audio.running=false;wake=true;
    }
done:
    spinlock_unlock_irqrestore(&audio_lock,flags);
    if(wake)fd_readiness_wake();
}

static int audio_start(bool force)
{
    uint64_t flags=spinlock_lock_irqsave(&audio_lock);
    bool start=audio.active&&!audio.running&&!audio.fault&&audio.count&&
        (force||audio.count>=AUDIO_RING);
    spinlock_unlock_irqrestore(&audio_lock,flags);
    if(!start)return 0;
    struct probe *p=&audio.probe;p->errors=0;p->deadline=timer_monotonic_ns()+UINT64_C(500000000);
    w32(p,audio.stream,1);
    bool ok=wait_bits(p,audio.stream,1,1);
    w32(p,audio.stream,0);
    ok=ok&&wait_bits(p,audio.stream,1,0);
    if(ok)ok=pci_set_command(&audio.device,audio.device.command|6);
    flags=spinlock_lock_irqsave(&audio_lock);
    if(ok) {
        for(unsigned i=0;i<AUDIO_PERIODS;++i)fill_period(i);
        audio.retire=0;
        uint64_t address=audio.info.device_address;
        w32(p,0x70,0);w32(p,0x74,0);
        w32(p,audio.stream+0x18,(uint32_t)address);
        w32(p,audio.stream+0x1c,(uint32_t)(address>>32));
        w32(p,audio.stream+8,AUDIO_RING);w16(p,audio.stream+0xc,AUDIO_PERIODS-1);
        w16(p,audio.stream+0x12,0x11);
        *(volatile uint8_t *)(p->mmio+audio.stream+3)=0x1c;
        audio.last_tick=timer_monotonic_ns();audio.running=true;
        w32(p,audio.stream,(1U<<20)|2);
    } else audio.fault=true;
    spinlock_unlock_irqrestore(&audio_lock,flags);
    if(!ok)fd_readiness_wake();
    return ok?0:-EIO;
}

static void audio_mute(void)
{
    struct probe *p=&audio.probe;p->errors=0;p->deadline=timer_monotonic_ns()+UINT64_C(500000000);
    (void)set_verb(p,audio.cad,audio.route.pin,0x707,0);
    (void)set_verb(p,audio.cad,audio.route.dac,0x300,0xb080);
    (void)set_verb(p,audio.cad,audio.route.dac,0x706,0);
    if(parameter(p,audio.cad,audio.route.pin,0xc)&(1U<<16))
        (void)set_verb(p,audio.cad,audio.route.pin,0x70c,0);
}

void hda_audio_poll(void)
{
    struct smp_cpu *cpu=smp_current_cpu();if(cpu&&!cpu->bsp)return;
    uint64_t flags=spinlock_lock_irqsave(&audio_lock);
    bool close=audio.active&&audio.closing;
    if(close){audio.active=false;audio.running=false;}
    spinlock_unlock_irqrestore(&audio_lock,flags);
    if(!close)return;
    struct probe *p=&audio.probe;p->deadline=timer_monotonic_ns()+UINT64_C(500000000);
    w32(p,audio.stream,1U<<20);
    bool stopped=wait_bits(p,audio.stream,2,0);
    bool master_off=pci_set_command(&audio.device,(audio.device.command|2)&~4U);
    audio_mute();
    struct vnode *node=audio.node;audio.node=NULL;
    if(stopped||master_off) {
        if(audio.pinned)(void)dma_descriptor_ring_unpin(audio.dma);
        if(audio.dma.generation)(void)dma_free(audio.dma);
        (void)pci_set_command(&audio.device,audio.device.command&~4U);
        if(audio.mapping.generation)(void)pci_mmio_release(&audio.mapping);
        if(audio.claim.generation)(void)pci_device_release(&audio.claim);
        flags=spinlock_lock_irqsave(&audio_lock);
        audio.closing=false;
        spinlock_unlock_irqrestore(&audio_lock,flags);
    } else {
        flags=spinlock_lock_irqsave(&audio_lock);
        audio.active=true;audio.closing=false;audio.fault=true;
        spinlock_unlock_irqrestore(&audio_lock,flags);
    }
    vnode_put(node);fd_readiness_wake();
}

static int audio_attach(struct probe *p,const struct pci_device_info *d,unsigned cad,uint32_t vendor,
    const struct pci_device_lease *claim,const struct pci_mmio_lease *mapping)
{
    struct tone_route route;
    if(!route_valid(p,cad,vendor,&route))return -ENODEV;
    if(!dma_identity_addressing_available()||((r16(p,0)>>12)&15)==0)return -ENODEV;
    memset(&audio,0,sizeof(audio));audio.volume=70;audio.route=route;audio.cad=cad;
    audio.probe=*p;audio.device=*d;audio.claim=*claim;audio.mapping=*mapping;
    audio.stream=0x80+((r16(p,0)>>8)&15)*0x20;
    if(!dma_alloc(4096+AUDIO_RING,4096,(r16(p,0)&1)?64:32,&audio.dma)||
        !dma_buffer_query(audio.dma,&audio.info))goto fail;
    memset(audio.info.cpu_virtual_address,0,4096+AUDIO_RING);
    uint32_t *bdl=audio.info.cpu_virtual_address;
    for(unsigned i=0;i<AUDIO_PERIODS;++i){
        uint64_t a=audio.info.device_address+4096+i*AUDIO_PERIOD;
        bdl[4*i]=(uint32_t)a;bdl[4*i+1]=(uint32_t)(a>>32);bdl[4*i+2]=AUDIO_PERIOD;bdl[4*i+3]=1;
    }
    if(!configure_route(&audio.probe,cad,&route))goto fail;
    if(!dma_descriptor_ring_pin(audio.dma))goto fail;
    audio.pinned=true;
    uint64_t flags=spinlock_lock_irqsave(&audio_lock);
    audio.active=true;
    spinlock_unlock_irqrestore(&audio_lock,flags);
    return 0;
fail:
    audio_mute();
    if(audio.pinned)(void)dma_descriptor_ring_unpin(audio.dma);
    if(audio.dma.generation)(void)dma_free(audio.dma);
    return -EIO;
}

static int audio_write(void *context,uint64_t offset,const void *buffer,size_t length,size_t *amount)
{
    (void)context;(void)offset;*amount=0;
    if(length%4)return -EINVAL;
    if(!length)return 0;
    uint64_t flags=spinlock_lock_irqsave(&audio_lock);
    if(audio.fault||audio.closing){spinlock_unlock_irqrestore(&audio_lock,flags);return -EIO;}
    size_t n=AUDIO_QUEUE-audio.count;if(n>length)n=length;
    if(audio.written>UINT64_MAX-n){spinlock_unlock_irqrestore(&audio_lock,flags);return -EOVERFLOW;}
    size_t tail=(audio.head+audio.count)%AUDIO_QUEUE,first=AUDIO_QUEUE-tail;
    if(first>n)first=n;
    memcpy(audio.queue+tail,buffer,first);memcpy(audio.queue,(const uint8_t *)buffer+first,n-first);
    audio.count+=n;audio.written+=n;
    spinlock_unlock_irqrestore(&audio_lock,flags);
    if(!n)return -EAGAIN;
    int error=audio_start(false);if(error)return error;
    *amount=n;return 0;
}
static int audio_ioctl(void *context,uint64_t request,void *argument)
{
    (void)context;
    if(request==OS_AUDIO_DRAIN){
        int error=audio_start(true);if(error)return error;
        uint64_t f=spinlock_lock_irqsave(&audio_lock);
        int result=audio.fault?-EIO:audio.played==audio.written?0:-EAGAIN;
        spinlock_unlock_irqrestore(&audio_lock,f);return result;
    }
    if(request==OS_AUDIO_RESET){
        uint64_t f=spinlock_lock_irqsave(&audio_lock);audio.running=false;
        w32(&audio.probe,audio.stream,1U<<20);spinlock_unlock_irqrestore(&audio_lock,f);
        audio.probe.deadline=timer_monotonic_ns()+UINT64_C(500000000);
        if(!wait_bits(&audio.probe,audio.stream,2,0))return -EIO;
        f=spinlock_lock_irqsave(&audio_lock);
        audio.head=audio.count=0;audio.written=audio.played=0;audio.fault=false;
        memset(audio.valid,0,sizeof(audio.valid));
        spinlock_unlock_irqrestore(&audio_lock,f);fd_readiness_wake();return 0;
    }
    if(!argument)return -EINVAL;
    uint64_t f=spinlock_lock_irqsave(&audio_lock);int result=0;
    if(request==OS_AUDIO_SETVOLUME){
        uint32_t value=*(uint32_t *)argument;if(value>100)result=-EINVAL;else audio.volume=value;
    } else if(request==OS_AUDIO_GETINFO){
        struct os_audio_info *info=argument;
        *info=(struct os_audio_info){48000,2,16,audio.volume,AUDIO_QUEUE+AUDIO_RING,
            (uint32_t)(audio.written-audio.played),AUDIO_PERIOD,
            (audio.running?OS_AUDIO_RUNNING:0)|(audio.fault?OS_AUDIO_ERROR:0),
            audio.written,audio.played,audio.underruns};
    } else result=-ENOTTY;
    spinlock_unlock_irqrestore(&audio_lock,f);return result;
}
static uint32_t audio_poll(void *context,uint32_t events)
{
    (void)context;uint64_t f=spinlock_lock_irqsave(&audio_lock);
    uint32_t ready=audio.fault?FD_POLL_ERROR:audio.count<AUDIO_QUEUE?(events&FD_POLL_WRITE):0;
    spinlock_unlock_irqrestore(&audio_lock,f);return ready;
}
static void audio_close(void *context)
{
    (void)context;uint64_t f=spinlock_lock_irqsave(&audio_lock);audio.closing=true;
    spinlock_unlock_irqrestore(&audio_lock,f);hda_audio_poll();
}
static int audio_stat(void *context,struct fd_metadata *m)
{
    (void)context;struct vnode *n=audio.node;if(!n)return -ENODEV;
    *m=(struct fd_metadata){.inode=n->inode,.mode=n->mode,.type=n->type,.uid=n->uid,.gid=n->gid};return 0;
}
static const struct fd_operations audio_operations={
    .write=audio_write,.ioctl=audio_ioctl,.poll=audio_poll,.close=audio_close,.stat=audio_stat
};
bool hda_audio_is_object(const struct fd_object *object)
{ return object&&object->operations==&audio_operations; }

static bool probe_device(const struct pci_device_info *d, void *unused)
{
    /* The requested target is the motherboard analog output, not GPU HDMI. */
    if (d->vendor_id != 0x8086 || d->class_code != 4 || d->subclass != 3) return true;
    struct request *request=unused;
    if((request->tone||request->stream) && request->attempted) return true;
    ++request->found;
    struct pci_device_lease claim = {0};
    struct pci_mmio_lease mapping = {0};
    struct pci_bar_resource bar;
    struct probe p = {.deadline = timer_monotonic_ns() + UINT64_C(8000000000)};
    volatile void *address;
    const char *stage = "PCI claim";
    bool changed = false, reset = false, retain = false;
    field("Intel HDA device=",d->device_id,4);field(" bus=",d->bus,2);
    field(" slot=",d->device,2);field(" function=",d->function,1);text("\n");
    if (!pci_device_claim(d,&claim)) goto done;
    stage="BAR0 sizing";
    if (!pci_bar_probe_size(&claim,0,&bar) || bar.aperture_size < 0x4000) goto done;
    stage="BAR0 mapping";
    if (!pci_mmio_map(&claim,&bar,0,0x4000,&mapping) ||
        !pci_mmio_lease_address(&mapping,&address)) goto done;
    p.mmio=address;
    stage="PCI memory decode";
    changed = true;
    /* No diagnostic-owned DMA buffers: bus mastering stays disabled. */
    if (!pci_config_write(d->segment,d->bus,d->device,d->function,4,16,(d->command|2)&~4U)) goto done;
    uint32_t command;
    if (!pci_config_read(d->segment,d->bus,d->device,d->function,4,16,&command) ||
        command != ((d->command|2)&~4U)) goto done;
    uint16_t gcap=r16(&p,0);
    field(" GCAP=",gcap,4);field(" version=",((uint32_t)p.mmio[3]<<8)|p.mmio[2],4);field(" GCTL=",r32(&p,8),8);text("\n");
    stage="controller registers";
    if (gcap == 0xffff || gcap == 0) goto done;
    stage="active streams (probe refused)";
    unsigned streams=((gcap>>12)&15)+((gcap>>8)&15)+((gcap>>3)&31);
    if (streams > 30) goto done;
    for (unsigned i=0;i<streams;++i) if (r32(&p,0x80+i*0x20)&2) goto done;
    reset = true;
    w32(&p,0x20,0);
    *(volatile uint8_t *)(p.mmio+0x4c)=0;
    *(volatile uint8_t *)(p.mmio+0x5c)=0;
    stage="reset assert";
    w32(&p,8,0);
    if (!wait_bits(&p,8,1,0)) goto done;
    timer_wait_for_interrupt();
    stage="reset release";
    w32(&p,8,1);
    if (!wait_bits(&p,8,1,1)) goto done;
    timer_wait_for_interrupt();
    uint16_t present=r16(&p,0xe)&0x7fff;
    field(" STATESTS=",present,4);text("\n");
    stage="codec discovery";
    for (unsigned cad=0;cad<15;++cad) {
        if (!(present&(1U<<cad))) continue;
        uint32_t vendor=parameter(&p,cad,0,0);
        if (vendor && vendor!=UINT32_MAX) {
            if(!request->tone&&!request->stream) describe_codec(&p,cad,vendor);
            else if(vendor==0x10ec0235 || vendor==0x1af40022) {
                request->attempted=true;
                field("Test codec cad=",cad,1);field(" vendor=",vendor,8);text("\n");
                if(request->stream) {
                    request->error=audio_attach(&p,d,cad,vendor,&claim,&mapping);
                    retain=request->error==0;
                } else retain=!play_tone(&p,d,cad,vendor);
                break;
            }
        }
    }
    if (p.errors) {field(" Immediate-command timeouts=",p.errors,2);text("\n");}
    else if (!present) text(" No codecs reported after reset\n");
    stage=NULL;
done:
    if (stage) {text(" probe failed at: ");text(stage);text("\n");}
    if(retain) return true;
    if (changed) (void)pci_config_write(d->segment,d->bus,d->device,d->function,4,16,reset ? d->command&~4U : d->command);
    if (mapping.generation) (void)pci_mmio_release(&mapping);
    if (claim.generation) (void)pci_device_release(&claim);
    return true;
}

static int snapshot(char *buffer, size_t capacity, size_t *length, bool tone)
{
    if (!buffer || !length) return -22;
    if (__atomic_test_and_set(&busy,__ATOMIC_ACQUIRE)) return -11;
    unsigned mode=tone?1:0;
    if (!ready[mode]) {
        used=0;truncated=false;
        text(tone ? "HDA one-shot tone test (first call per boot plays; later calls show cached report)\n" :
            "HDA codec diagnostic v1 (Intel only; controller reset, no playback)\n");
        struct request request={.tone=tone};
        if (!pci_enumerate(probe_device,&request)) text("PCI enumeration failed\n");
        if (!request.found) text("No Intel HDA controller found\n");
        if(tone && !request.attempted) text("No supported analog codec found for tone test\n");
        if (truncated) {
            static const char suffix[] = "\nReport truncated (16 KiB limit)\n";
            memcpy(report+used,suffix,sizeof(suffix)-1);used+=sizeof(suffix)-1;
        }
        memcpy(cached[mode],report,used);cached_length[mode]=used;
        ready[mode]=true;
    }
    int result=0;
    if (cached_length[mode]>capacity) result=-12;
    else {memcpy(buffer,cached[mode],cached_length[mode]);*length=cached_length[mode];}
    __atomic_clear(&busy,__ATOMIC_RELEASE);
    return result;
}

int hda_probe_snapshot(char *buffer,size_t capacity,size_t *length) {return snapshot(buffer,capacity,length,false);}
int hda_tone_snapshot(char *buffer,size_t capacity,size_t *length) {return snapshot(buffer,capacity,length,true);}

int hda_audio_open(struct vnode *node,uint32_t flags,struct fd_object **result)
{
    if((flags&OS_O_ACCMODE)!=OS_O_WRONLY)return -EINVAL;
    hda_audio_poll();
    if(__atomic_test_and_set(&busy,__ATOMIC_ACQUIRE))return -EBUSY;
    if(audio.active||audio.closing){__atomic_clear(&busy,__ATOMIC_RELEASE);return -EBUSY;}
    used=0;truncated=false;
    struct request request={.stream=true,.error=-ENODEV};
    bool enumerated=pci_enumerate(probe_device,&request);
    int error=enumerated?request.error:-EIO;
    if(!error){
        if(!vnode_get(node))error=-ENOMEM;
        else {
            audio.node=node;
            *result=fd_object_create(&audio_operations,&audio);
            if(!*result)error=-ENOMEM;
        }
        if(error){audio.closing=true;hda_audio_poll();}
    }
    __atomic_clear(&busy,__ATOMIC_RELEASE);return error;
}
