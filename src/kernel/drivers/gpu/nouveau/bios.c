/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* NVKM BIOS formats adapted from Linux v6.12; see upstream/bios/.
 * Copyright 2012, 2014 Red Hat Inc. SPDX-License-Identifier: MIT
 * Local changes add explicit bounds and limit support to modern DCB tables. */
#include "kernel/nouveau_bios.h"

static bool span(size_t size, size_t offset, size_t length)
{
	return offset <= size && length <= size - offset;
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16;
}

static bool
image_header_checked(const uint8_t *data, size_t size,
	struct nouveau_bios_image *image, enum nouveau_rom_header_error *reason)
{
	*reason = NOUVEAU_ROM_HEADER_SHORT;
	if (image == NULL)
		return false;
	*image = (struct nouveau_bios_image){0};
	if (data == NULL || !span(size, 0, 0x1a))
		return false;
	uint16_t signature = rd16(data);
	*reason = NOUVEAU_ROM_SIGNATURE;
	if (signature != 0xaa55 && signature != 0xbb77 && signature != 0x4e56)
		return false;
	size_t pcir = rd16(data + 0x18);
	*reason = NOUVEAU_ROM_PCIR_POINTER;
	if (pcir < 0x1a || !span(size, pcir, 0x18))
		return false;
	uint32_t magic = rd32(data + pcir);
	*reason = NOUVEAU_ROM_PCIR_SIGNATURE;
	if (magic != 0x52494350 && magic != 0x53494752 && magic != 0x5344504e)
		return false;
	size_t header = rd16(data + pcir + 0x0a);
	*reason = NOUVEAU_ROM_PCIR_LENGTH;
	if (header < 0x18 || !span(size, pcir, header))
		return false;
	image->vendor = rd16(data + pcir + 4);
	image->device = rd16(data + pcir + 6);
	image->size = (uint32_t)rd16(data + pcir + 0x10) * 512U;
	image->type = data[pcir + 0x14];
	image->last = (data[pcir + 0x15] & 0x80) != 0;
	if (image->type == 0x70) {
		image->last = true;
	} else {
		size_t npde = (pcir + header + 15U) & ~(size_t)15U;
		if (span(size, npde, 4) && rd32(data + npde) == 0x4544504e) {
			*reason = NOUVEAU_ROM_NPDE_LENGTH;
			if (!span(size, npde, 0x0b))
				return false;
			image->size = (uint32_t)rd16(data + npde + 8) * 512U;
			image->last = (data[npde + 10] & 0x80) != 0;
			if (npde + 0x0b > image->size)
				return false;
		}
	}
	*reason = NOUVEAU_ROM_IMAGE_LENGTH;
	if (image->size == 0 || image->size > NOUVEAU_BIOS_LIMIT ||
	    pcir + header > image->size)
		return false;
	*reason = NOUVEAU_ROM_HEADER_OK;
	return true;
}

bool
nouveau_bios_image_header(const uint8_t *data, size_t size,
	struct nouveau_bios_image *image)
{
	enum nouveau_rom_header_error reason;
	return image_header_checked(data, size, image, &reason);
}

bool
nouveau_bios_capture_ex(nouveau_bios_reader reader, void *context,
	uint8_t *data, size_t capacity, size_t *size,
	struct nouveau_bios_capture_info *info)
{
	struct nouveau_bios_capture_info local = {0};
	if (info == NULL)
		info = &local;
	*info = (struct nouveau_bios_capture_info){0};
	if (size == NULL)
		return false;
	*size = 0;
	if (reader == NULL || data == NULL || capacity > NOUVEAU_BIOS_LIMIT)
		return false;
	size_t base = 0;
	for (unsigned int i = 0; i < NOUVEAU_BIOS_MAX_IMAGES; i++) {
		info->offset = (uint32_t)base;
		info->image_index = (uint8_t)i;
		info->header_bytes = 0;
		info->pcir_offset = info->pcir_length = 0;
		info->pcir_magic = 0;
		info->declared_size = 0;
		info->header_error = NOUVEAU_ROM_HEADER_OK;
		for (size_t j = 0; j < sizeof(info->first_bytes); j++)
			info->first_bytes[j] = 0;
		/* Headers must fit in the first 4 KiB of each image. */
		size_t header = capacity - base;
		if (header > 4096)
			header = 4096;
		info->error = NOUVEAU_ROM_READ_FAILED;
		if (header < 0x1a || !reader(context, (uint32_t)base, data + base, header))
			break;
		info->header_bytes = (uint32_t)header;
		for (size_t j = 0; j < sizeof(info->first_bytes) && j < header; j++)
			info->first_bytes[j] = data[base + j];
		info->pcir_offset = rd16(data + base + 0x18);
		if (span(header, info->pcir_offset, 4))
			info->pcir_magic = rd32(data + base + info->pcir_offset);
		if (span(header, info->pcir_offset, 12))
			info->pcir_length = rd16(data + base + info->pcir_offset + 10);
		struct nouveau_bios_image image;
		info->error = NOUVEAU_ROM_HEADER_INVALID;
		bool header_ok = image_header_checked(data + base, header, &image,
		    &info->header_error);
		info->declared_size = image.size;
		if (!header_ok)
			break;
		info->error = NOUVEAU_ROM_IMAGE_TOO_LARGE;
		if (!span(capacity, base, image.size))
			break;
		info->error = NOUVEAU_ROM_READ_FAILED;
		if (image.size > header && !reader(context, (uint32_t)(base + header),
		    data + base + header, image.size - header))
			break;
		base += image.size;
		info->valid_size = (uint32_t)base;
		if (image.last) {
			info->complete = true;
			info->error = NOUVEAU_ROM_CAPTURE_OK;
			break;
		}
		info->error = NOUVEAU_ROM_IMAGE_LIMIT;
	}
	/* NVKM shadow_image keeps a valid parent's score when its next image
	 * cannot be read. Never expose the failed image to the table parsers. */
	*size = base;
	return base != 0;
}

bool
nouveau_bios_capture(nouveau_bios_reader reader, void *context,
	uint8_t *data, size_t capacity, size_t *size)
{
	return nouveau_bios_capture_ex(reader, context, data, capacity, size, NULL);
}

struct bios_view {
	const uint8_t *data;
	size_t size;
	uint32_t image0_size, imaged_addr;
};

/* NVKM's logical image-D pointers are relative to the end of image zero. */
static const uint8_t *pointer(const struct bios_view *bios, uint32_t address,
	size_t length)
{
	size_t offset = address;
	if (bios->imaged_addr != 0 && address >= bios->image0_size)
		offset = (size_t)(address - bios->image0_size) + bios->imaged_addr;
	if (!span(bios->size, offset, length))
		return NULL;
	/* A logical range may not straddle the remapping boundary. */
	if (bios->imaged_addr && address < bios->image0_size &&
	    length > bios->image0_size - address)
		return NULL;
	return bios->data + offset;
}

static uint32_t find(const uint8_t *data, size_t size, const uint8_t *needle,
	size_t length)
{
	if (length > size)
		return 0;
	for (size_t i = 0; i <= size - length; i++) {
		size_t j = 0;
		while (j < length && data[i + j] == needle[j])
			j++;
		if (j == length)
			return (uint32_t)i;
	}
	return 0;
}

static bool parse_bit(const struct bios_view *bios, struct nouveau_bios_report *r)
{
	if (r->bit_offset == 0)
		return true;
	/* BIT itself was found in the physical snapshot; only its payload pointers
	 * use the logical image-D translation. */
	const uint8_t *bit = bios->data + r->bit_offset;
	if (!span(bios->size, r->bit_offset, 12) || bit[8] != 12 || bit[9] < 6 ||
	    !span(bios->size, r->bit_offset + 12, (size_t)bit[9] * bit[10]))
		return false;
	for (unsigned int i = 0; i < bit[10]; i++) {
		const uint8_t *entry = bit + 12 + i * bit[9];
		uint16_t length = rd16(entry + 2), offset = rd16(entry + 4);
		if (length != 0 && (offset == 0 || pointer(bios, offset, length) == NULL))
			return false;
		if (entry[0] == 'i') {
			const uint8_t *v = pointer(bios, offset, length);
			if (length < 4 || v == NULL)
				return false;
			r->version[0] = v[3]; r->version[1] = v[2];
			r->version[2] = v[1]; r->version[3] = v[0];
			r->version[4] = length >= 5 ? v[4] : 0;
			r->version_valid = true;
		}
	}
	return true;
}

static bool parse_connectors(const struct bios_view *bios,
	struct nouveau_bios_report *r)
{
	if (r->connector_offset == 0)
		return true;
	const uint8_t *table = pointer(bios, r->connector_offset, 4);
	if (table == NULL || (table[0] != 0x30 && table[0] != 0x40) ||
	    table[1] < 4 || table[3] < 2 || table[2] > NOUVEAU_DCB_MAX_CONNECTORS)
		return false;
	const uint8_t *all = pointer(bios, r->connector_offset,
	    (size_t)table[1] + table[2] * table[3]);
	if (all == NULL)
		return false;
	r->connector_version = table[0];
	r->connector_count = table[2];
	for (unsigned int i = 0; i < r->connector_count; i++) {
		const uint8_t *entry = all + table[1] + i * table[3];
		struct nouveau_dcb_connector *c = &r->connectors[i];
		c->type = entry[0]; c->location = entry[1] & 0x0f;
		c->hpd = (entry[1] & 0x30) >> 4; c->dp = (entry[1] & 0xc0) >> 6;
		if (table[3] >= 4) {
			c->hpd |= (entry[2] & 3) << 2;
			c->dp |= entry[2] & 0x0c;
			c->di = entry[2] >> 4;
			c->hpd |= (entry[3] & 7) << 4;
			c->sr = (entry[3] & 8) >> 3;
			c->lcdid = (entry[3] & 0x70) >> 4;
		}
	}
	return true;
}

static enum nouveau_bios_status parse_dcb(const struct bios_view *bios,
	struct nouveau_bios_report *r)
{
	const uint8_t *ptr = pointer(bios, 0x36, 2);
	if (ptr == NULL)
		return NOUVEAU_BIOS_DCB_INVALID;
	r->dcb_offset = rd16(ptr);
	if (r->dcb_offset == 0)
		return NOUVEAU_BIOS_DCB_MISSING;
	const uint8_t *dcb = pointer(bios, r->dcb_offset, 10);
	if (dcb == NULL)
		return NOUVEAU_BIOS_DCB_INVALID;
	r->dcb_version = dcb[0];
	if (dcb[0] < 0x30 || dcb[0] >= 0x42 || dcb[1] < 10 || dcb[3] < 8 ||
	    dcb[2] > NOUVEAU_DCB_MAX_OUTPUTS || rd32(dcb + 6) != 0x4edcbdcb)
		return NOUVEAU_BIOS_DCB_INVALID;
	const uint8_t *all = pointer(bios, r->dcb_offset,
	    (size_t)dcb[1] + dcb[2] * dcb[3]);
	if (all == NULL)
		return NOUVEAU_BIOS_DCB_INVALID;
	r->dcb_entries = dcb[2];
	if (dcb[1] >= 0x16)
		r->connector_offset = rd16(dcb + 0x14);
	if (!parse_connectors(bios, r))
		return NOUVEAU_BIOS_CONNECTOR_INVALID;
	for (unsigned int i = 0; i < dcb[2]; i++) {
		const uint8_t *entry = all + dcb[1] + i * dcb[3];
		uint32_t conn = rd32(entry), conf = rd32(entry + 4);
		uint8_t type = conn & 15;
		if (conn == 0 || conn == UINT32_MAX || type == 0xe)
			break;
		if (type == 0xf)
			continue;
		struct nouveau_dcb_output *o = &r->outputs[r->output_count++];
		o->index = i; o->connection = conn; o->configuration = conf;
		o->type = type; o->i2c = (conn >> 4) & 15;
		o->heads = (conn >> 8) & 15; o->connector = (conn >> 12) & 15;
		o->bus = (conn >> 16) & 15; o->location = (conn >> 20) & 3;
		o->or_mask = (conn >> 24) & 15;
		if (r->connector_offset && o->connector >= r->connector_count)
			return NOUVEAU_BIOS_CONNECTOR_INVALID;
		if (dcb[0] >= 0x40 && (type == 2 || type == 3 || type == 6)) {
			o->link = (conf >> 4) & 3;
			if (o->location != 0)
				o->external_device = (conf >> 8) & 255;
			if (type == 6) {
				unsigned int lanes = (conf >> 24) & 15;
				o->dp_lanes = lanes == 15 || lanes == 4 ? 4 :
				    lanes == 3 || lanes == 2 ? 2 : 1;
				unsigned int bw = (conf >> 21) & 7;
				o->dp_bandwidth = bw == 0 ? 6 : bw == 1 ? 10 : bw == 2 ? 20 : 30;
			}
		}
	}
	return NOUVEAU_BIOS_OK;
}

/* CCB 4.1 encodes PMGR drive/aux independently; 0x1f means absent.
 * Retain a valid DCB even if the optional CCB is malformed. No bus accesses
 * are allowed unless i2c_valid is true. */
static void parse_i2c(const struct bios_view *bios, struct nouveau_bios_report *r)
{
	const uint8_t *dcb = pointer(bios, r->dcb_offset, 6);
	if (dcb == NULL)
		return;
	r->i2c_offset = rd16(dcb + 4);
	if (r->i2c_offset == 0)
		return;
	const uint8_t *table = pointer(bios, r->i2c_offset, 4);
	if (table == NULL)
		return;
	r->i2c_version = table[0];
	if (table[0] < 0x30 || table[0] > 0x41 || table[1] < 4 || table[3] < 4 ||
	    table[2] > NOUVEAU_CCB_MAX_ENTRIES)
		return;
	const uint8_t *all = pointer(bios, r->i2c_offset,
	    (size_t)table[1] + table[2] * table[3]);
	if (all == NULL)
		return;
	r->i2c_count = table[2];
	for (unsigned int i = 0; i < r->i2c_count; i++) {
		const uint8_t *p = all + table[1] + i * table[3];
		struct nouveau_i2c_entry *e = &r->i2c[i];
		e->raw = rd32(p);
		e->drive = e->aux = e->share = 0xff;
		if (table[0] >= 0x41) {
			e->drive = p[0] & 31;
			e->aux = (rd16(p) >> 5) & 31;
			if (e->drive == 31) e->drive = 0xff;
			if (e->aux == 31) e->aux = 0xff;
			e->share = e->aux;
			e->type = e->drive == 0xff && e->aux == 0xff ? 0xff : 0x80;
		} else {
			e->type = p[3];
			if (e->type == 0x80) {
				e->drive = p[0] & 31; e->aux = (rd16(p) >> 5) & 31;
				if (e->drive == 31) e->drive = 0xff;
				if (e->aux == 31) e->aux = 0xff;
				e->share = e->aux;
			}
			if (e->type == 5) e->drive = p[0] & 15;
			if (e->type == 6) e->aux = p[0] & 15;
			if ((e->type == 5 || e->type == 6) && (p[1] & 1))
				e->share = e->type == 6 ? e->aux : p[1] >> 1;
		}
	}
	r->i2c_valid = true;
}

enum nouveau_bios_status
nouveau_bios_parse(const uint8_t *data, size_t size,
	struct nouveau_bios_report *r)
{
	if (r == NULL)
		return NOUVEAU_BIOS_ROM_INVALID;
	*r = (struct nouveau_bios_report){0};
	if (data == NULL || size > NOUVEAU_BIOS_LIMIT)
		return NOUVEAU_BIOS_ROM_INVALID;
	size_t base = 0;
	r->checksum_valid = true;
	for (unsigned int i = 0; i < NOUVEAU_BIOS_MAX_IMAGES; i++) {
		struct nouveau_bios_image image;
		if (base >= size || !nouveau_bios_image_header(data + base, size - base,
		    &image) || !span(size, base, image.size))
			break;
		if (i == 0) {
			if (image.vendor != 0x10de)
				return NOUVEAU_BIOS_ROM_INVALID;
			r->image0_size = image.size;
			r->vendor = image.vendor; r->device = image.device;
		} else if (image.type == 0xe0 && r->imaged_addr == 0) {
			r->imaged_addr = (uint32_t)base;
		}
		if (image.type == 0) {
			uint8_t sum = 0;
			for (size_t j = 0; j < image.size; j++)
				sum += data[base + j];
			if (sum != 0)
				r->checksum_valid = false;
		}
		base += image.size;
		r->image_count++;
		if (image.last) { r->chain_complete = true; break; }
	}
	if (r->image_count == 0)
		return NOUVEAU_BIOS_ROM_INVALID;
	r->rom_size = (uint32_t)base;
	struct bios_view bios = {data, base, r->image0_size, r->imaged_addr};
	static const uint8_t bit_magic[] = {0xff, 0xb8, 'B', 'I', 'T'};
	static const uint8_t bmp_magic[] = {0xff, 0x7f, 'N', 'V', 0};
	r->bit_offset = find(data, base, bit_magic, sizeof(bit_magic));
	r->bmp_offset = find(data, base, bmp_magic, sizeof(bmp_magic));
	if (!parse_bit(&bios, r))
		return NOUVEAU_BIOS_BIT_INVALID;
	if (!r->version_valid && r->bmp_offset && span(base, r->bmp_offset, 14)) {
		const uint8_t *bmp = data + r->bmp_offset;
		r->version[0] = bmp[13]; r->version[1] = bmp[12];
		r->version[2] = bmp[11]; r->version[3] = bmp[10];
		r->version_valid = true;
	}
	enum nouveau_bios_status status = parse_dcb(&bios, r);
	if (status == NOUVEAU_BIOS_OK)
		parse_i2c(&bios, r);
	return status;
}
