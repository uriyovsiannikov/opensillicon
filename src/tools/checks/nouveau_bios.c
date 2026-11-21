/* Host tests for bounded NVKM BIOS parsing and PRAMIN resource restoration. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "kernel/nouveau_bios.h"

#define ROM_SIZE 8192U
static alignas(4) uint8_t rom[NOUVEAU_BIOS_LIMIT];
static alignas(4) uint8_t prom_rom[NOUVEAU_BIOS_LIMIT];
static uint32_t fuse, bios_pointer, control;
static uint32_t prom_control, expected_prom_control;
static unsigned maps, allocations, map_calls, address_calls, fail_map, fail_address;
static bool fail_alloc;
static const struct pci_device_lease claim = {.generation = 1};
static const struct pci_bar_resource bar = {
	.type = PCI_BAR_MEMORY32, .base = 0xa3000000, .aperture_size = 0x1000000
};

static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }
static void checksum(uint8_t *p, size_t size)
{
	p[size - 1] = 0;
	uint8_t sum = 0;
	for (size_t i = 0; i < size; i++) sum += p[i];
	p[size - 1] = (uint8_t)-sum;
}

static void image(uint8_t *p, size_t size, uint8_t type, bool last)
{
	wr16(p, 0xaa55); wr16(p + 0x18, 0x40);
	wr32(p + 0x40, 0x52494350);
	wr16(p + 0x44, 0x10de); wr16(p + 0x46, 0x1c82);
	wr16(p + 0x4a, 0x18); wr16(p + 0x50, size / 512);
	p[0x54] = type; p[0x55] = last ? 0x80 : 0;
}

static void fixture(void)
{
	assert(maps == 0 && allocations == 0);
	memset(rom, 0, sizeof(rom));
	image(rom, ROM_SIZE, 0, true);
	memcpy(rom + 0x100, "\xff\xb8" "BIT", 5);
	rom[0x106] = 0; rom[0x107] = 1;
	rom[0x108] = 12; rom[0x109] = 6; rom[0x10a] = 1;
	rom[0x10c] = 'i'; rom[0x10d] = 2;
	wr16(rom + 0x10e, 5); wr16(rom + 0x110, 0x180);
	memcpy(rom + 0x180, "\x70\x60\x07\x86\x01", 5);
	wr16(rom + 0x36, 0x300);
	rom[0x300] = 0x40; rom[0x301] = 0x16;
	rom[0x302] = 4; rom[0x303] = 8;
	wr32(rom + 0x306, 0x4edcbdcb);
	wr16(rom + 0x314, 0x400);
	wr32(rom + 0x316, 0x01000112); /* TMDS, connector 0, OR 1, head 0, I2C 1 */
	wr32(rom + 0x31a, 0x10);
	wr32(rom + 0x31e, 0x02001326); /* DP, connector 1, OR 2, heads 0/1, I2C 2 */
	wr32(rom + 0x322, 0x04400010); /* 4 lanes, 5.4 Gbps, link 1 */
	wr32(rom + 0x326, 0x0000000f);
	wr32(rom + 0x32e, 0x0000000e);
	rom[0x400] = 0x40; rom[0x401] = 4; rom[0x402] = 2; rom[0x403] = 4;
	rom[0x404] = 0x61; rom[0x405] = 0x20;
	rom[0x408] = 0x46; rom[0x409] = 0xb1;
	rom[0x40a] = 0x52; rom[0x40b] = 0x19;
	checksum(rom, ROM_SIZE);
	memcpy(prom_rom, rom, ROM_SIZE);
	fuse = 0; bios_pointer = 0x00800009; control = 0x1234;
	prom_control = expected_prom_control = 0xa5;
	map_calls = address_calls = fail_map = fail_address = 0;
	fail_alloc = false;
}

static bool map_common(uint64_t off, size_t size, bool writable,
	struct pci_mmio_lease *lease)
{
	map_calls++;
	if (fail_map == map_calls) return false;
	void *p = NULL;
	switch (off) {
	case 0x021c04: assert(size == 4 && !writable); p = &fuse; break;
	case 0x619f04: assert(size == 4 && !writable); p = &bios_pointer; break;
	case 0x001700: assert(size == 4 && writable); p = &control; break;
	case 0x700000: assert(size == NOUVEAU_BIOS_LIMIT && !writable); p = rom; break;
	case 0x088050:
		assert(size == 4 && writable && maps == 0 && allocations == 0);
		assert(control == 0x1234); p = &prom_control; break;
	case 0x300000:
		assert(size == NOUVEAU_BIOS_LIMIT && !writable); p = prom_rom; break;
	default: abort();
	}
	maps++;
	lease->generation = 1; lease->address = (uintptr_t)p;
	return true;
}

bool pci_mmio_map(const struct pci_device_lease *c, const struct pci_bar_resource *b,
	uint64_t off, size_t size, struct pci_mmio_lease *l)
{
	assert(c == &claim && b == &bar);
	return map_common(off, size, true, l);
}
bool pci_mmio_map_readonly(const struct pci_device_lease *c,
	const struct pci_bar_resource *b, uint64_t off, size_t size, struct pci_mmio_lease *l)
{
	assert(c == &claim && b == &bar);
	return map_common(off, size, false, l);
}
bool pci_mmio_lease_address(const struct pci_mmio_lease *l, volatile void **p)
{
	assert(l->generation == 1);
	address_calls++;
	if (fail_address == address_calls) return false;
	*p = (volatile void *)(uintptr_t)l->address;
	return true;
}
bool pci_mmio_release(struct pci_mmio_lease *l)
{
	assert(l->generation == 1 && maps > 0);
	l->generation = 0; maps--; return true;
}
void *kheap_alloc(size_t size)
{
	assert(size == NOUVEAU_BIOS_LIMIT && control == 0x1234);
	assert(prom_control == expected_prom_control);
	if (fail_alloc) return NULL;
	void *p = malloc(size); assert(p); allocations++; return p;
}
bool kheap_free(void *p)
{
	assert(allocations == 1 && control == 0x1234);
	assert(prom_control == expected_prom_control);
	allocations--; free(p); return true;
}
void klog(const char *s) { (void)s; }
_Noreturn void kernel_halt(void) { abort(); }

static struct nouveau_bios_report probe(enum nouveau_bios_status expected)
{
	struct nouveau_bios_report r;
	assert(nouveau_bios_pramin_probe(&claim, &bar, &r) == expected);
	assert(control == 0x1234 && maps == 0 && allocations == 0);
	return r;
}

struct reader_state { size_t limit; unsigned calls, fail_call; };
static bool read_rom(void *ctx, uint32_t offset, uint8_t *data, size_t size)
{
	struct reader_state *s = ctx;
	s->calls++;
	if (s->calls == s->fail_call || offset > s->limit || size > s->limit - offset)
		return false;
	memcpy(data, rom + offset, size); return true;
}

static uint32_t random_state = 0x137;
static uint32_t random32(void)
{
	random_state ^= random_state << 13; random_state ^= random_state >> 17;
	random_state ^= random_state << 5; return random_state;
}

int main(void)
{
	struct nouveau_bios_report r;
	struct nouveau_bios_image header;
	fixture();
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK);
	assert(r.rom_size == ROM_SIZE && r.image_count == 1 && r.checksum_valid);
	assert(r.chain_complete);
	assert(r.vendor == 0x10de && r.device == 0x1c82 && r.version_valid);
	assert(r.version[0] == 0x86 && r.version[3] == 0x70 && r.version[4] == 1);
	assert(r.bit_offset == 0x100 && r.dcb_version == 0x40);
	assert(r.output_count == 2 && r.dcb_entries == 4 && r.connector_count == 2);
	assert(r.outputs[0].type == 2 && r.outputs[0].i2c == 1);
	assert(r.outputs[1].type == 6 && r.outputs[1].connector == 1);
	assert(r.outputs[1].or_mask == 2 && r.outputs[1].heads == 3);
	assert(r.outputs[1].dp_lanes == 4 && r.outputs[1].dp_bandwidth == 20);
	assert(r.connectors[1].type == 0x46 && r.connectors[1].location == 1);
	assert(r.connectors[1].hpd == 0x1b && r.connectors[1].dp == 2);
	assert(r.connectors[1].di == 5 && r.connectors[1].sr == 1 && r.connectors[1].lcdid == 1);
	/* CCB index is not the register channel number. */
	wr16(rom + 0x304,0x500);
	rom[0x500]=0x41; rom[0x501]=4; rom[0x502]=8; rom[0x503]=4;
	for (unsigned i=0; i<8; i++) wr32(rom+0x504+i*4,0x3ff);
	wr32(rom+0x504+6*4,2|(4<<5));
	wr32(rom+0x504+7*4,3|(1<<5));
	wr32(rom+0x504+4*4,5|(31<<5));
	assert(nouveau_bios_parse(rom,ROM_SIZE,&r)==NOUVEAU_BIOS_OK && r.i2c_valid);
	assert(r.i2c_version==0x41 && r.i2c_count==8 && r.i2c[0].type==0xff);
	assert(r.i2c[6].type==0x80 && r.i2c[6].drive==2 && r.i2c[6].aux==4 && r.i2c[6].share==4);
	assert(r.i2c[4].drive==5 && r.i2c[4].aux==0xff && r.i2c[4].share==0xff);
	rom[0x500]=0x42;
	assert(nouveau_bios_parse(rom,ROM_SIZE,&r)==NOUVEAU_BIOS_OK && !r.i2c_valid);
	rom[0x500]=0x41; rom[0x502]=33;
	assert(nouveau_bios_parse(rom,ROM_SIZE,&r)==NOUVEAU_BIOS_OK && !r.i2c_valid);
	wr16(rom+0x304,ROM_SIZE-2);
	assert(nouveau_bios_parse(rom,ROM_SIZE,&r)==NOUVEAU_BIOS_OK && !r.i2c_valid);
	fixture();
	assert(!nouveau_bios_image_header(NULL, 0, &header));
	assert(!nouveau_bios_image_header(rom, ROM_SIZE, NULL));
	assert(nouveau_bios_parse(NULL, ROM_SIZE, &r) == NOUVEAU_BIOS_ROM_INVALID);
	assert(nouveau_bios_parse(rom, ROM_SIZE, NULL) == NOUVEAU_BIOS_ROM_INVALID);
	for (size_t size = 0; size < ROM_SIZE; size += 7)
		assert(nouveau_bios_parse(rom, size, &r) == NOUVEAU_BIOS_ROM_INVALID);
	rom[ROM_SIZE - 1]++;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK && !r.checksum_valid);
	fixture(); wr16(rom + 0x36, 0);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_MISSING);
	fixture(); wr16(rom + 0x36, ROM_SIZE - 2);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x300] = 0x42;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x301] = 9;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x303] = 4;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x306] ^= 1;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x302] = 33;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x402] = 1;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_CONNECTOR_INVALID);
	fixture(); rom[0x403] = 1;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_CONNECTOR_INVALID);
	fixture(); wr16(rom + 0x314, ROM_SIZE - 1);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_CONNECTOR_INVALID);
	fixture(); wr16(rom + 0x314, 0);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK && r.connector_count == 0);
	fixture(); rom[0x109] = 5;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_BIT_INVALID);
	fixture(); wr16(rom + 0x110, ROM_SIZE - 2);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_BIT_INVALID);
	fixture(); wr16(rom + 0x10e, 4);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK && r.version[4] == 0);
	fixture(); memset(rom + 0x100, 0, 5);
	memcpy(rom + 0x200, "\xff\x7f" "NV\0", 5);
	rom[0x20d] = 0x86;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK);
	assert(r.bmp_offset == 0x200 && r.version[0] == 0x86);
	fixture(); wr32(rom + 0x60, 0x4544504e);
	wr16(rom + 0x68, ROM_SIZE / 512); rom[0x6a] = 0x80;
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_OK);
	wr16(rom + 0x68, 0);
	assert(nouveau_bios_parse(rom, ROM_SIZE, &r) == NOUVEAU_BIOS_ROM_INVALID);
	/* Image-D is physically after an EFI image but logically after image 0. */
	fixture(); rom[0x55] = 0;
	image(rom + ROM_SIZE, 4096, 3, false);
	image(rom + ROM_SIZE + 4096, ROM_SIZE, 0xe0, true);
	memcpy(rom + ROM_SIZE + 4096 + 0x300, rom + 0x300, 0x200);
	wr16(rom + 0x36, ROM_SIZE + 0x300);
	wr16(rom + ROM_SIZE + 4096 + 0x314, ROM_SIZE + 0x400);
	assert(nouveau_bios_parse(rom, ROM_SIZE * 2 + 4096, &r) == NOUVEAU_BIOS_OK);
	assert(r.image_count == 3 && r.imaged_addr == ROM_SIZE + 4096 && r.output_count == 2);
	uint8_t *snapshot = malloc(NOUVEAU_BIOS_LIMIT); assert(snapshot);
	struct reader_state state = {sizeof(rom), 0, 0};
	size_t captured;
	assert(nouveau_bios_capture(read_rom, &state, snapshot, sizeof(rom), &captured));
	assert(captured == ROM_SIZE * 2 + 4096 && memcmp(snapshot, rom, captured) == 0);
	fixture();
	state = (struct reader_state){sizeof(rom), 0, 2};
	assert(!nouveau_bios_capture(read_rom, &state, snapshot, sizeof(rom), &captured));
	assert(captured == 0);
	assert(!nouveau_bios_capture(read_rom, &state, snapshot, 128, &captured));
	/* Upstream keeps a valid parent even when the next image is not in VRAM. */
	fixture(); rom[0x55] = 0;
	struct nouveau_bios_capture_info info;
	state = (struct reader_state){sizeof(rom), 0, 0};
	assert(nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(captured == ROM_SIZE && info.valid_size == ROM_SIZE && !info.complete);
	assert(info.image_index == 1 && info.offset == ROM_SIZE);
	assert(info.error == NOUVEAU_ROM_HEADER_INVALID && info.header_bytes == 4096);
	assert(info.first_bytes[0] == 0);
	assert(nouveau_bios_parse(snapshot, captured, &r) == NOUVEAU_BIOS_OK);
	assert(!r.chain_complete && r.image_count == 1 && r.output_count == 2);
	/* An absent image-D cannot satisfy a pointer outside the valid prefix. */
	wr16(snapshot + 0x36, ROM_SIZE + 0x300);
	assert(nouveau_bios_parse(snapshot, captured, &r) == NOUVEAU_BIOS_DCB_INVALID);
	fixture(); rom[0x55] = 0;
	state = (struct reader_state){sizeof(rom), 0, 3};
	assert(nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(info.error == NOUVEAU_ROM_READ_FAILED && info.valid_size == ROM_SIZE);
	/* Failure inside image 1's body preserves only image 0, never partial data. */
	image(rom + ROM_SIZE, ROM_SIZE, 3, true);
	state = (struct reader_state){sizeof(rom), 0, 4};
	assert(nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(info.error == NOUVEAU_ROM_READ_FAILED && captured == ROM_SIZE);
	assert(nouveau_bios_parse(snapshot, captured, &r) == NOUVEAU_BIOS_OK);
	assert(r.rom_size == ROM_SIZE && r.image_count == 1 && !r.chain_complete);
	/* NVIDIA extension images may have a different PCIR vendor field. */
	fixture(); rom[0x55] = 0;
	image(rom + ROM_SIZE, 4096, 0xe0, true);
	wr16(rom + ROM_SIZE + 0x44, 0);
	assert(nouveau_bios_parse(rom, ROM_SIZE + 4096, &r) == NOUVEAU_BIOS_OK);
	assert(r.chain_complete && r.image_count == 2);
	fixture(); rom[0] = 0;
	state = (struct reader_state){sizeof(rom), 0, 0};
	assert(!nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(captured == 0 && info.error == NOUVEAU_ROM_HEADER_INVALID && info.offset == 0);
	assert(info.header_error == NOUVEAU_ROM_SIGNATURE);
	assert(info.pcir_offset == 0x40 && info.pcir_magic == 0x52494350 && info.pcir_length == 0x18);
	fixture(); wr16(rom + 0x4a, 0);
	assert(!nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(info.header_error == NOUVEAU_ROM_PCIR_LENGTH && info.pcir_length == 0);
	fixture(); wr32(rom + 0x40, 0);
	assert(!nouveau_bios_capture_ex(read_rom, &state, snapshot, sizeof(rom),
	    &captured, &info));
	assert(info.header_error == NOUVEAU_ROM_PCIR_SIGNATURE && info.pcir_magic == 0);
	free(snapshot);
	fixture(); r = probe(NOUVEAU_BIOS_OK); assert(r.output_count == 2);
	assert(r.acquisition_flags == 7 && r.vram_pointer == 0x00800009);
	assert(r.window_before == 0x1234 && r.window_target == 0x8000);
	for (unsigned i = 1; i <= 4; i++) {
		fixture(); fail_map = i; probe(NOUVEAU_BIOS_MAP_FAILED);
		fixture(); fail_address = i; probe(NOUVEAU_BIOS_MAP_FAILED);
	}
	fixture(); fuse = 1; probe(NOUVEAU_BIOS_DISPLAY_DISABLED); assert(map_calls == 1);
	fixture(); bios_pointer = 1; probe(NOUVEAU_BIOS_VRAM_UNAVAILABLE);
	fixture(); bios_pointer = 0xa; probe(NOUVEAU_BIOS_VRAM_UNAVAILABLE);
	fixture(); bios_pointer = 9; r = probe(NOUVEAU_BIOS_OK); /* fallback address */
	assert(r.window_target == 0x1243);
	fixture(); fail_alloc = true; probe(NOUVEAU_BIOS_NO_MEMORY);
	fixture(); rom[0] = 0; probe(NOUVEAU_BIOS_ROM_INVALID);
	fixture(); rom[0x109] = 0; probe(NOUVEAU_BIOS_BIT_INVALID);
	fixture(); rom[0x303] = 0; probe(NOUVEAU_BIOS_DCB_INVALID);
	/* The hardware report: PRAMIN points at a BEEF block rather than a ROM.
	 * PROM must be tried only after restoring PRAMIN and releasing its leases. */
	fixture();
	static const uint8_t beef_header[32] = {
	    0xef,0xbe,0,0,0,0,0,0,0,0x80,0,0x42,0,0,0,0,
	    8,5,1,0,0x0a,0,0,0,0,0,1,0,0,0,0x80,0x14
	};
	memcpy(rom, beef_header, sizeof(beef_header));
	assert(nouveau_bios_probe(&claim, &bar, &r) == NOUVEAU_BIOS_OK);
	assert(r.source == NOUVEAU_BIOS_SOURCE_PROM && r.fallback_attempted);
	assert(r.pramin_status == NOUVEAU_BIOS_ROM_INVALID);
	assert(r.pramin_capture.header_error == NOUVEAU_ROM_SIGNATURE);
	assert(r.pramin_capture.first_bytes[0] == 0xef && r.pramin_capture.first_bytes[1] == 0xbe);
	assert(r.prom_control_before == 0xa5 && r.prom_control_target == 0xa4);
	assert(r.output_count == 2 && r.version[0] == 0x86);
	assert(control == 0x1234 && prom_control == 0xa5 && maps == 0 && allocations == 0);
	assert(map_calls == 6);
	fixture();
	assert(nouveau_bios_probe(&claim, &bar, &r) == NOUVEAU_BIOS_OK);
	assert(r.source == NOUVEAU_BIOS_SOURCE_PRAMIN && !r.fallback_attempted && map_calls == 4);
	for (unsigned i = 1; i <= 2; i++) {
		fixture(); fail_map = i;
		assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_MAP_FAILED);
		assert(maps == 0 && allocations == 0 && prom_control == 0xa5);
		fixture(); fail_address = i;
		assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_MAP_FAILED);
		assert(maps == 0 && allocations == 0 && prom_control == 0xa5);
	}
	fixture(); fail_alloc = true;
	assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_NO_MEMORY);
	assert(maps == 0 && allocations == 0 && prom_control == 0xa5);
	fixture(); prom_control = expected_prom_control = 0xa4;
	assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_OK);
	assert(prom_control == 0xa4 && r.prom_control_before == 0xa4 && r.prom_control_target == 0xa4);
	fixture(); prom_rom[0] = 0;
	assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_ROM_INVALID);
	assert(maps == 0 && allocations == 0 && prom_control == 0xa5);
	fixture(); prom_control = expected_prom_control = UINT32_MAX;
	assert(nouveau_bios_prom_probe(&claim, &bar, &r) == NOUVEAU_BIOS_WINDOW_FAILED);
	assert(maps == 0 && allocations == 0 && prom_control == UINT32_MAX);
	fixture(); memcpy(rom, beef_header, sizeof(beef_header)); prom_rom[0] = 0;
	assert(nouveau_bios_probe(&claim, &bar, &r) == NOUVEAU_BIOS_ROM_INVALID);
	assert(r.fallback_attempted && r.source == NOUVEAU_BIOS_SOURCE_PROM);
	assert(r.capture.header_error == NOUVEAU_ROM_SIGNATURE && r.capture.first_bytes[0] == 0);
	assert(r.pramin_capture.first_bytes[0] == 0xef);
	assert(maps == 0 && allocations == 0 && prom_control == 0xa5 && control == 0x1234);
	/* Exact-sized buffers let ASan catch reads past the supplied bound. */
	for (unsigned i = 0; i < 12000; i++) {
		size_t size = random32() % (ROM_SIZE + 1);
		uint8_t *fuzz = malloc(size ? size : 1); assert(fuzz);
		fixture(); memcpy(fuzz, rom, size);
		for (unsigned n = 0; n < 8 && size; n++)
			fuzz[random32() % size] = (uint8_t)random32();
		(void)nouveau_bios_parse(fuzz, size, &r);
		free(fuzz);
	}
	puts("NOUVEAU_BIOS_PASS: ROM/PCIR/NPDE, BIT/BMP, DCB/connectors, image-D, BEEF/PROM fallback, register cleanup, 12000 mutations");
	return 0;
}
