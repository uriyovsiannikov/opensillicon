#include "dns.h"
#include <string.h>

#define DNS_NAME_MAX 255U
#define DNS_RECORD_MAX (512U / 11U)

static unsigned u16(const uint8_t *p)
{
	return ((unsigned)p[0] << 8) | p[1];
}

static int decode_name(const uint8_t *packet, size_t length, size_t *offset,
    uint8_t output[DNS_NAME_MAX], size_t *size)
{
	size_t cursor = *offset, used = 0, steps = 0;
	int jumped = 0;
	while (cursor < length && steps++ < length) {
		unsigned label = packet[cursor++];
		if ((label & 0xc0) == 0xc0) {
			if (cursor >= length) return 0;
			if (!jumped) *offset = cursor + 1;
			cursor = ((size_t)(label & 63) << 8) | packet[cursor];
			jumped = 1;
			continue;
		}
		if (label > 63 || label > length - cursor ||
		    used + 1 + label > DNS_NAME_MAX) return 0;
		output[used++] = (uint8_t)label;
		if (label == 0) {
			if (!jumped) *offset = cursor;
			*size = used;
			return 1;
		}
		memcpy(output + used, packet + cursor, label);
		used += label;
		cursor += label;
	}
	return 0;
}

static int same_name(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	if (an != bn) return 0;
	for (size_t i = 0; i < an; i++) {
		unsigned x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y) return 0;
	}
	return 1;
}

int __libos_dns_answers(const uint8_t *packet, size_t length, size_t offset,
    unsigned count, const uint8_t *query, size_t query_size,
    uint8_t (*addresses)[4], size_t capacity, size_t *address_count)
{
	struct record { size_t owner, data, size; unsigned type, class; } records[DNS_RECORD_MAX];
	uint8_t owner[DNS_NAME_MAX], target[DNS_NAME_MAX], wanted[DNS_NAME_MAX];
	size_t owner_size, target_size, wanted_size = query_size;
	*address_count = 0;
	if (length > 512 || count > DNS_RECORD_MAX || !query_size ||
	    query_size > sizeof(wanted)) return 0;
	memcpy(wanted, query, query_size);
	for (unsigned i = 0; i < count; i++) {
		struct record *r = &records[i];
		r->owner = offset;
		if (!decode_name(packet, length, &offset, owner, &owner_size) ||
		    offset > length || length - offset < 10) return 0;
		r->type = u16(packet + offset);
		r->class = u16(packet + offset + 2);
		r->size = u16(packet + offset + 8);
		offset += 10;
		r->data = offset;
		if (r->size > length - offset) return 0;
		if (r->class == 1 && r->type == 1 && r->size != 4) return 0;
		if (r->class == 1 && r->type == 5) {
			size_t end = offset;
			if (!decode_name(packet, length, &end, target, &target_size) ||
			    end != offset + r->size) return 0;
		}
		offset += r->size;
	}
	/* Resolve aliases before collecting addresses, regardless of record order.
	 * More transitions than records implies a CNAME cycle. */
	for (unsigned hop = 0; ; hop++) {
		int found = 0;
		for (unsigned i = 0; i < count; i++) {
			struct record *r = &records[i];
			if (r->class != 1 || r->type != 5) continue;
			size_t cursor = r->owner;
			if (!decode_name(packet, length, &cursor, owner, &owner_size)) return 0;
			if (!same_name(owner, owner_size, wanted, wanted_size)) continue;
			/* Multiple aliases for one owner are ambiguous. */
			if (found) return 0;
			cursor = r->data;
			if (!decode_name(packet, length, &cursor, target, &target_size)) return 0;
			found = 1;
		}
		if (!found) break;
		if (hop >= count) return 0;
		memcpy(wanted, target, target_size);
		wanted_size = target_size;
	}
	for (unsigned i = 0; i < count; i++) {
		struct record *r = &records[i];
		if (r->class != 1 || r->type != 1) continue;
		size_t cursor = r->owner;
		if (!decode_name(packet, length, &cursor, owner, &owner_size)) return 0;
		if (same_name(owner, owner_size, wanted, wanted_size) &&
		    *address_count < capacity)
			memcpy(addresses[(*address_count)++], packet + r->data, 4);
	}
	return 1;
}
