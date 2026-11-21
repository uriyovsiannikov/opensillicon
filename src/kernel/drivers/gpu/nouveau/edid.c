/* SPDX-License-Identifier: BSD-2-Clause
 * Bounded EDID 1.x and CTA-861 mode extraction; no mode programming. */
#include "kernel/nouveau_edid.h"

static bool checksum(const uint8_t *p)
{
	uint8_t sum = 0;
	for (size_t i = 0; i < 128; i++) sum += p[i];
	return sum == 0;
}
static struct nouveau_edid_mode *mode(struct nouveau_edid *e, unsigned w, unsigned h, uint32_t rate,
	uint32_t clock, bool interlaced, bool preferred)
{
	if (w == 0 || h == 0 || w > UINT16_MAX || h > UINT16_MAX || rate < 1000 || rate > 1000000)
		return NULL;
	for (size_t i = 0; i < e->mode_count; i++) {
		struct nouveau_edid_mode *m = &e->modes[i];
		uint32_t diff = rate > m->refresh_millihz ? rate - m->refresh_millihz : m->refresh_millihz - rate;
		if (m->width == w && m->height == h && m->interlaced == interlaced && diff < 1000) {
			m->preferred |= preferred;
			if (clock && !m->detailed) {
				m->clock_khz = clock; m->refresh_millihz = rate;
			}
			return m;
		}
	}
	if (e->mode_count == NOUVEAU_EDID_MAX_MODES) { e->modes_truncated = true; return NULL; }
	struct nouveau_edid_mode *m = &e->modes[e->mode_count++];
	*m = (struct nouveau_edid_mode){.width=(uint16_t)w,.height=(uint16_t)h,
	    .refresh_millihz=rate,.clock_khz=clock,.interlaced=interlaced,.preferred=preferred};
	return m;
}

static void detailed(struct nouveau_edid *e, const uint8_t *p, bool preferred)
{
	uint32_t clock = ((uint32_t)p[0] | (uint32_t)p[1] << 8) * 10;
	if (clock == 0) return;
	unsigned w = p[2] | (p[4] & 0xf0) << 4, hb = p[3] | (p[4] & 15) << 8;
	unsigned h = p[5] | (p[7] & 0xf0) << 4, vb = p[6] | (p[7] & 15) << 8;
	if (w == 0 || h == 0 || hb == 0 || vb == 0) return;
	bool interlaced = (p[17] & 0x80) != 0;
	uint32_t vt = h + vb;
	/* Some CEA DTDs encode field height; other interlaced DTDs already
	 * encode frame height. Do not double every interlaced resolution. */
	if (interlaced && ((w == 1920 && h == 540) ||
	    ((w == 720 || w == 1440 || w == 2880) && (h == 240 || h == 288)))) {
		h *= 2; vt = vt * 2 + 1;
	}
	uint64_t rate = (uint64_t)clock * 1000000 * (interlaced ? 2 : 1) / ((w + hb) * vt);
	if (rate > 1000000) return;
	struct nouveau_edid_mode *m = mode(e,w,h,(uint32_t)rate,clock,interlaced,preferred);
	/* Sync positions are needed for modesetting, not just width/height/Hz.
	 * Composite/stereo/interlaced timings remain enumeration-only for now. */
	unsigned hf = p[8] | (p[11] & 0xc0) << 2, hs = p[9] | (p[11] & 0x30) << 4;
	unsigned vf = (p[10] >> 4) | (p[11] & 12) << 2, vs = (p[10] & 15) | (p[11] & 3) << 4;
	if (m && (!m->detailed || !m->preferred || preferred) && !interlaced &&
	    (p[17] & 0x18) == 0x18 && !(p[17] & 0x61) && hs && vs &&
	    hf + hs <= hb && vf + vs <= vb) {
		m->clock_khz=clock; m->refresh_millihz=(uint32_t)rate;
		m->hsync_start = w + hf; m->hsync_end = w + hf + hs; m->htotal = w + hb;
		m->vsync_start = h + vf; m->vsync_end = h + vf + vs; m->vtotal = h + vb;
		m->hsync_positive = (p[17] & 2) != 0; m->vsync_positive = (p[17] & 4) != 0;
		m->detailed = true;
	}
}

struct vic_mode { uint8_t vic; uint16_t w, h, hz; bool interlaced; };
static const struct vic_mode vics[] = {
	{1,640,480,60,false}, {2,720,480,60,false}, {3,720,480,60,false},
	{4,1280,720,60,false}, {5,1920,1080,60,true}, {16,1920,1080,60,false},
	{17,720,576,50,false}, {18,720,576,50,false}, {19,1280,720,50,false},
	{20,1920,1080,50,true}, {31,1920,1080,50,false}, {32,1920,1080,24,false},
	{33,1920,1080,25,false}, {34,1920,1080,30,false},
	{60,1280,720,24,false}, {61,1280,720,25,false}, {62,1280,720,30,false},
	{63,1920,1080,120,false}, {64,1920,1080,100,false},
	{93,3840,2160,24,false}, {94,3840,2160,25,false}, {95,3840,2160,30,false},
	{96,3840,2160,50,false}, {97,3840,2160,60,false},
	{98,4096,2160,24,false}, {99,4096,2160,25,false}, {100,4096,2160,30,false},
	{101,4096,2160,50,false}, {102,4096,2160,60,false}
};

static bool cta(struct nouveau_edid *e, const uint8_t *p)
{
	unsigned end = p[2];
	if (end == 0) return true;
	if (end < 4 || end > 127) return false;
	/* Validate the data collection first, so malformed lengths add no modes. */
	for (unsigned pos = 4; pos < end;) {
		unsigned length = p[pos] & 31;
		if (length > end - pos - 1) return false;
		pos += 1 + length;
	}
	for (unsigned pos = 4; pos < end;) {
		unsigned length = p[pos] & 31;
		if ((p[pos] >> 5) == 2) {
			for (unsigned j = 1; j <= length; j++) {
				uint8_t svd = p[pos + j];
				if (svd >= 193) continue; /* extended VIC, not a native flag */
				uint8_t vic = svd & 127;
				for (size_t k = 0; k < sizeof(vics)/sizeof(vics[0]); k++) {
					const struct vic_mode *v = &vics[k];
					if (v->vic == vic) mode(e, v->w, v->h, v->hz * 1000U, 0, v->interlaced, false);
				}
			}
		}
		pos += 1 + length;
	}
	for (unsigned pos = end; pos + 18 <= 127; pos += 18) detailed(e, p + pos, false);
	return true;
}

bool nouveau_edid_parse(const uint8_t *p, size_t size, struct nouveau_edid *e)
{
	if (e == NULL) return false;
	*e = (struct nouveau_edid){0};
	if (p == NULL || size < 128 || size > 128 * NOUVEAU_EDID_MAX_BLOCKS || size % 128)
		return false;
	static const uint8_t header[8] = {0,255,255,255,255,255,255,0};
	for (unsigned i = 0; i < 8; i++) if (p[i] != header[i]) return false;
	if (!checksum(p) || p[18] != 1 || p[19] > 4) return false;
	uint16_t manufacturer = (uint16_t)p[8] << 8 | p[9];
	for (unsigned i = 0; i < 3; i++) {
		unsigned letter = (manufacturer >> ((2 - i) * 5)) & 31;
		if (letter == 0 || letter > 26) return false;
		e->manufacturer[i] = (char)('A' + letter - 1);
	}
	e->product = (uint16_t)p[10] | (uint16_t)p[11] << 8;
	e->serial = (uint32_t)p[12] | (uint32_t)p[13] << 8 | (uint32_t)p[14] << 16 | (uint32_t)p[15] << 24;
	e->version = p[18]; e->revision = p[19]; e->declared_extensions = p[126];
	e->blocks = (uint8_t)(size / 128);
	if (e->blocks > (unsigned)p[126] + 1) e->blocks = p[126] + 1;
	e->truncated = (unsigned)e->blocks < (unsigned)p[126] + 1;
	for (unsigned i = 0; i < 4; i++) {
		const uint8_t *d = p + 54 + i * 18;
		if (d[0] || d[1]) detailed(e, d, i == 0 && (p[24] & 2));
		else if (d[2] == 0 && d[3] == 0xfc && d[4] == 0) {
			unsigned j;
			for (j = 0; j < 13 && d[5+j] != 10 && d[5+j] != 0; j++)
				e->name[j] = d[5+j] >= 32 && d[5+j] < 127 ? (char)d[5+j] : '?';
			while (j && e->name[j-1] == ' ') j--;
			e->name[j] = 0;
		}
	}
	static const struct { uint8_t byte, bit; uint16_t w,h,hz; bool interlaced; } established[] = {
		{35,7,720,400,70,false},{35,6,720,400,88,false},{35,5,640,480,60,false},
		{35,4,640,480,67,false},{35,3,640,480,72,false},{35,2,640,480,75,false},
		{35,1,800,600,56,false},{35,0,800,600,60,false},{36,7,800,600,72,false},
		{36,6,800,600,75,false},{36,5,832,624,75,false},{36,4,1024,768,87,true},
		{36,3,1024,768,60,false},{36,2,1024,768,70,false},{36,1,1024,768,75,false},
		{36,0,1280,1024,75,false},{37,7,1152,870,75,false}
	};
	for (size_t i=0; i<sizeof(established)/sizeof(established[0]); i++) {
		if (p[established[i].byte] & (1U << established[i].bit))
			mode(e, established[i].w, established[i].h, established[i].hz * 1000U, 0, established[i].interlaced, false);
	}
	for (unsigned i = 38; i < 54; i += 2) {
		if (p[i] == 0 || (p[i] == 1 && p[i+1] == 1)) continue;
		unsigned w = (p[i] + 31) * 8, h;
		switch (p[i+1] >> 6) {
		case 0: h = e->revision < 3 ? w : w * 10 / 16; break;
		case 1: h = w * 3 / 4; break;
		case 2: h = w * 4 / 5; break;
		default: h = w * 9 / 16; break;
		}
		mode(e, w, h, ((p[i+1] & 63) + 60) * 1000U, 0, false, false);
	}
	for (unsigned i = 1; i < e->blocks; i++) {
		const uint8_t *ext = p + 128 * i;
		if (!checksum(ext) || (ext[0] == 2 && !cta(e, ext))) e->bad_extensions++;
	}
	return true;
}
