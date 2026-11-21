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
 * all copies or substantial busions of the Software.
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
/* NVKM GF119 I2C and GM200 AUX adapted for a bounded boot-time EDID probe.
 * Copyright 2012, 2014, 2015 Red Hat Inc. SPDX-License-Identifier: MIT
 * See upstream/i2c/ and README.md for the original sources and license. */
#include "kernel/nouveau_edid.h"

struct saved_reg { uint32_t address, mask, value; };
struct ddc {
	const struct nouveau_ddc_io *io;
	struct nouveau_ddc_result *r;
	struct saved_reg saved[10];
	unsigned saved_count, budget;
	uint32_t bus, aux;
	bool aux_owned, aux_acquired, aux_started;
};
static uint32_t rd(struct ddc *d, uint32_t reg) { return d->io->read(d->io->context, reg); }
static void wr(struct ddc *d, uint32_t reg, uint32_t value) { d->io->write(d->io->context, reg, value); }
static void mask(struct ddc *d, uint32_t reg, uint32_t bits, uint32_t value)
{
	wr(d, reg, (rd(d, reg) & ~bits) | (value & bits));
}
static bool delay(struct ddc *d, unsigned us)
{
	if (us > d->budget || !d->io->delay_us(d->io->context, us)) {
		d->r->status = NOUVEAU_DDC_TIMEOUT; return false;
	}
	d->budget -= us; return true;
}
static void save(struct ddc *d, uint32_t reg, uint32_t bits)
{
	d->saved[d->saved_count++] = (struct saved_reg){reg,bits,rd(d,reg) & bits};
}
static bool restore(struct ddc *d)
{
	bool ok = true;
	while (d->saved_count) {
		struct saved_reg *s = &d->saved[--d->saved_count];
		mask(d, s->address, s->mask, s->value);
		if ((rd(d,s->address) & s->mask) != s->value) ok = false;
	}
	return ok;
}
static bool wait_bits(struct ddc *d, uint32_t reg, uint32_t bits, uint32_t value, unsigned usec)
{
	for (unsigned i = 0; i < usec; i++) {
		d->r->last_control = rd(d,reg);
		if ((d->r->last_control & bits) == value) return true;
		if (!delay(d,1)) return false;
	}
	d->r->status = NOUVEAU_DDC_TIMEOUT; return false;
}

static void scl(struct ddc *d, bool high) { mask(d,d->bus,1,high ? 1 : 0); }
static void sda(struct ddc *d, bool high) { mask(d,d->bus,2,high ? 2 : 0); }
static bool raise_scl(struct ddc *d)
{
	scl(d,true);
	for (unsigned i = 0; i < 2200; i++) {
		if (!delay(d,1)) return false;
		if (rd(d,d->bus) & 0x10) return true;
	}
	d->r->status = NOUVEAU_DDC_TIMEOUT; return false;
}
static bool start(struct ddc *d)
{
	sda(d,true);
	if (!delay(d,1) || !raise_scl(d)) return false;
	if (!(rd(d,d->bus) & 0x20)) { d->r->status = NOUVEAU_DDC_BUSY; return false; }
	sda(d,false);
	if (!delay(d,5)) return false;
	scl(d,false); return delay(d,5);
}
static bool stop(struct ddc *d)
{
	scl(d,false); sda(d,false);
	if (!delay(d,1) || !raise_scl(d) || !delay(d,5)) return false;
	sda(d,true); return delay(d,5);
}
static bool write_bit(struct ddc *d, bool bit)
{
	sda(d,bit);
	if (!delay(d,1) || !raise_scl(d) || !delay(d,5)) return false;
	scl(d,false); return delay(d,5);
}
static int read_bit(struct ddc *d)
{
	sda(d,true);
	if (!delay(d,1) || !raise_scl(d) || !delay(d,5)) return -1;
	int bit = (rd(d,d->bus) & 0x20) != 0;
	scl(d,false);
	return delay(d,5) ? bit : -1;
}
static bool write_byte(struct ddc *d, uint8_t byte)
{
	for (unsigned i = 0; i < 8; i++) if (!write_bit(d,(byte & (0x80U >> i)) != 0)) return false;
	int ack = read_bit(d);
	if (ack < 0) return false;
	if (ack) { d->r->status = NOUVEAU_DDC_NACK; return false; }
	return true;
}
static bool read_byte(struct ddc *d, uint8_t *byte, bool last)
{
	*byte = 0;
	for (unsigned i = 0; i < 8; i++) {
		int bit = read_bit(d);
		if (bit < 0) return false;
		*byte = (uint8_t)((*byte << 1) | bit);
	}
	return write_bit(d,last);
}
static bool bit_block(struct ddc *d, unsigned block, uint8_t *data)
{
	bool ok = true;
	if (block >= 2) {
		ok = start(d) && write_byte(d,0x60) && write_byte(d,(uint8_t)(block / 2));
		if (!stop(d)) ok = false;
	}
	if (ok) ok = start(d) && write_byte(d,0xa0) && write_byte(d,(uint8_t)((block & 1) * 128)) &&
	    start(d) && write_byte(d,0xa1);
	for (unsigned i = 0; ok && i < 128; i++) ok = read_byte(d,&data[i],i == 127);
	/* Even a timeout gets a small independent cleanup budget for STOP. */
	unsigned budget = d->budget;
	d->budget = 3000;
	if (!stop(d)) ok = false;
	d->budget = budget;
	return ok;
}

/* GM200 offsets differ from the G94 AUX block. Request ownership for the
 * session; no DPCD/link-training writes are issued. */
static bool aux_begin(struct ddc *d)
{
	uint32_t ctrl = d->aux + 0x24;
	if (!wait_bits(d,ctrl,0x07710000,0,1000)) { d->r->status = NOUVEAU_DDC_BUSY; return false; }
	/* Restore autodpcd after the transaction registers, before releasing PAD. */
	save(d,d->aux + 0x38,0x10000);
	save(d,ctrl,0x0000f1ff);
	save(d,d->aux + 0x20,UINT32_MAX);
	for (unsigned i = 0; i < 4; i++) save(d,d->aux + i * 4,UINT32_MAX);
	d->aux_owned = true;
	mask(d,ctrl,0x00700000,0x00100000);
	if (!wait_bits(d,ctrl,0x07000000,0x01000000,1000)) return false;
	d->aux_acquired = true;
	d->r->last_status = rd(d,d->aux + 0x28);
	if (!(d->r->last_status & 0x10000000)) { d->r->status = NOUVEAU_DDC_NO_SINK; return false; }
	mask(d,d->aux + 0x38,0x10000,0);
	return true;
}
static bool aux_packet(struct ddc *d, uint8_t command, uint8_t address,
	uint8_t *bytes, unsigned *length)
{
	uint32_t ctrl_reg = d->aux + 0x24;
	uint32_t ctrl = rd(d,ctrl_reg) & ~0x0001f1ffU;
	ctrl |= (uint32_t)command << 12;
	ctrl |= *length ? *length - 1 : 0x100;
	wr(d,d->aux + 0x20,address);
	if (!(command & 1)) {
		for (unsigned i = 0; i < 4; i++) {
			uint32_t value = 0;
			for (unsigned j = 0; j < 4 && i * 4 + j < *length; j++)
				value |= (uint32_t)bytes[i * 4 + j] << (j * 8);
			wr(d,d->aux + 4 * i,value);
		}
	}
	for (unsigned attempt = 0; attempt < 8; attempt++) {
		wr(d,ctrl_reg,0x80000000U | ctrl); wr(d,ctrl_reg,ctrl);
		if (attempt && !delay(d,400)) return false;
		wr(d,ctrl_reg,0x10000 | ctrl);
		d->aux_started = true;
		if (!wait_bits(d,ctrl_reg,0x10000,0,2000)) return false;
		uint32_t stat = rd(d,d->aux + 0x28);
		d->r->last_status = stat;
		wr(d,d->aux + 0x28,stat); /* W1C acknowledgement, as NVKM's mask(0,0). */
		if (stat & 0x100) { d->r->status = NOUVEAU_DDC_TIMEOUT; return false; }
		if (stat & 0xe00) { d->r->status = NOUVEAU_DDC_IO; return false; }
		unsigned reply = (stat >> 16) & 15;
		if (reply == 2 || reply == 8) continue; /* native/I2C DEFER */
		if (reply) { d->r->status = NOUVEAU_DDC_NACK; return false; }
		if (command & 1) {
			unsigned count = stat & 31;
			if (count > *length) { d->r->status = NOUVEAU_DDC_IO; return false; }
			if (!count) continue;
			for (unsigned i = 0; i < count; i += 4) {
				uint32_t value = rd(d,d->aux + 0x10 + i);
				for (unsigned j = 0; j < 4 && i + j < count; j++)
					bytes[i+j] = (uint8_t)(value >> (j * 8));
			}
			*length = count;
		}
		return true;
	}
	d->r->status = NOUVEAU_DDC_TIMEOUT; return false;
}
static bool aux_block(struct ddc *d, unsigned block, uint8_t *data)
{
	unsigned length = 1;
	if (block >= 2) {
		uint8_t segment = (uint8_t)(block / 2);
		if (!aux_packet(d,0,0x30,&segment,&length)) return false;
	}
	uint8_t offset = (uint8_t)((block & 1) * 128);
	length = 1;
	if (!aux_packet(d,4,0x50,&offset,&length)) return false;
	for (unsigned pos = 0; pos < 128;) {
		length = 128 - pos;
		if (length > 16) length = 16;
		uint8_t command = pos + length < 128 ? 5 : 1;
		if (!aux_packet(d,command,0x50,data + pos,&length)) return false;
		pos += length;
	}
	return true;
}

void nouveau_ddc_read(const struct nouveau_ddc_io *io,
	const struct nouveau_i2c_entry *entry, bool aux, struct nouveau_ddc_result *r)
{
	if (r == NULL) return;
	*r = (struct nouveau_ddc_result){.status = NOUVEAU_DDC_UNSUPPORTED};
	if (io == NULL || entry == NULL || !io->read || !io->write || !io->delay_us ||
	    (entry->type != 0x80 && entry->type != (aux ? 6 : 5)) ||
	    (aux ? entry->aux >= 8 : entry->drive >= 32) ||
	    (entry->share != 0xff && entry->share >= 8)) return;
	struct ddc d = {.io = io, .r = r, .budget = 200000};
	r->status = NOUVEAU_DDC_OK;
	if (entry->share != 0xff) {
		uint32_t pad = 0xd970 + entry->share * 0x50;
		save(&d,pad + 12,1); save(&d,pad,0xc003);
		mask(&d,pad,0xc003,aux ? 2 : 0xc001); mask(&d,pad + 12,1,0);
	}
	bool ready;
	if (aux) {
		d.aux = 0xd930 + entry->aux * 0x50;
		ready = aux_begin(&d);
	} else {
		d.bus = 0xd014 + entry->drive * 0x20;
		save(&d,d.bus,7); mask(&d,d.bus,7,7);
		ready = (rd(&d,d.bus) & 0x30) == 0x30;
		if (!ready) r->status = NOUVEAU_DDC_BUSY;
	}
	if (ready) {
		bool ok = aux ? aux_block(&d,0,r->data) : bit_block(&d,0,r->data);
		if (ok) {
			r->bytes = 128;
			/* Never trust the extension count before base EDID validation. */
			struct nouveau_edid base;
			if (!nouveau_edid_parse(r->data,128,&base)) r->status = NOUVEAU_DDC_EDID_INVALID;
			else {
				unsigned blocks = (unsigned)r->data[126] + 1;
				if (blocks > NOUVEAU_EDID_MAX_BLOCKS) { blocks = NOUVEAU_EDID_MAX_BLOCKS; r->extensions_unread = true; }
				for (unsigned i = 1; i < blocks; i++) {
					ok = aux ? aux_block(&d,i,r->data + i * 128) : bit_block(&d,i,r->data + i * 128);
					if (!ok) { r->extensions_unread = true; break; }
					r->bytes += 128;
				}
				r->status = NOUVEAU_DDC_OK; /* base remains usable on extension failure */
			}
		}
		/* E-DDC segment pointer is stateful. Reset it after accessing blocks 2+. */
		if (r->bytes > 256 || (r->bytes == 256 && r->extensions_unread)) {
			enum nouveau_ddc_status status = r->status;
			d.budget = 5000;
			if (aux) { unsigned length = 1; uint8_t zero = 0;
				r->segment_reset_failed = !aux_packet(&d,0,0x30,&zero,&length); }
			else { r->segment_reset_failed = !(start(&d) && write_byte(&d,0x60) && write_byte(&d,0));
				if (!stop(&d)) r->segment_reset_failed = true; }
			r->status = status;
		}
	}
	if (d.aux_owned) {
		/* End an outstanding MOT transaction even on NACK/timeout. Reset and
		 * drop ownership before restoring payload/configuration registers. */
		enum nouveau_ddc_status status = r->status;
		uint32_t last_control = r->last_control, last_status = r->last_status;
		d.budget = 5000;
		unsigned zero = 0; uint8_t unused = 0;
		if (d.aux_acquired && d.aux_started) (void)aux_packet(&d,0,0x50,&unused,&zero);
		uint32_t ctrl = rd(&d,d.aux + 0x24) & ~0x80010000U;
		wr(&d,d.aux + 0x24,ctrl | 0x80000000U); wr(&d,d.aux + 0x24,ctrl);
		mask(&d,d.aux + 0x24,0x00710000,0);
		if (!wait_bits(&d,d.aux + 0x24,0x07710000,0,1000)) status = NOUVEAU_DDC_RESTORE;
		r->status = status;
		if (status != NOUVEAU_DDC_RESTORE) {
			r->last_control = last_control; r->last_status = last_status;
		}
	}
	if (!aux) r->last_control = rd(&d,d.bus);
	if (!restore(&d)) r->status = NOUVEAU_DDC_RESTORE;
}
