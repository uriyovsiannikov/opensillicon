#include <assert.h>
#include <string.h>
#include "../../user/libos/src/net/dns.h"

static const uint8_t query[] = {1, 'a', 0};
static uint8_t packet[512], addresses[8][4];
static size_t used, count;
static void reset(void) { memset(packet, 0, sizeof(packet)); used = 12; }
static void record(char owner, unsigned type, const uint8_t *data, size_t size)
{
	packet[used++] = 1; packet[used++] = (uint8_t)owner; packet[used++] = 0;
	packet[used++] = 0; packet[used++] = (uint8_t)type;
	packet[used++] = 0; packet[used++] = 1;
	used += 4;
	packet[used++] = (uint8_t)(size >> 8); packet[used++] = (uint8_t)size;
	memcpy(packet + used, data, size); used += size;
}
static int parse(unsigned records)
{
	return __libos_dns_answers(packet, used, 12, records, query, sizeof(query),
	    addresses, 8, &count);
}
int main(void)
{
	const uint8_t good[] = {1, 2, 3, 4}, bad[] = {6, 6, 6, 6};
	const uint8_t alias_b[] = {1, 'b', 0}, alias_a[] = {1, 'a', 0};
	reset(); record('z', 1, bad, 4); record('A', 1, good, 4);
	assert(parse(2) && count == 1 && !memcmp(addresses[0], good, 4));
	reset(); record('z', 1, bad, 4);
	assert(parse(1) && count == 0);
	reset(); record('b', 1, good, 4); record('a', 5, alias_b, 3);
	assert(parse(2) && count == 1 && !memcmp(addresses[0], good, 4));
	record('b', 5, alias_a, 3);
	assert(!parse(3));
	reset(); record('a', 1, good, 4);
	/* Compressed owner points to itself. */
	packet[12] = 0xc0; packet[13] = 12;
	assert(!parse(1));
	packet[13] = 255;
	assert(!parse(1));
	reset(); record('a', 5, alias_b, 2);
	assert(!parse(1));
	reset(); record('a', 1, good, 3);
	assert(!parse(1));
	reset(); record('a', 1, good, 4);
	assert(!__libos_dns_answers(packet, used - 1, 12, 1, query, sizeof(query),
	    addresses, 8, &count));
	/* CNAME data compressed to an earlier name is valid. */
	reset(); record('b', 1, good, 4);
	const uint8_t compressed[] = {0xc0, 12};
	record('a', 5, compressed, 2);
	assert(parse(2) && count == 1 && !memcmp(addresses[0], good, 4));
	assert(!parse(65535));
	return 0;
}
