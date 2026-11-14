#ifndef KERNEL_GPT_H
#define KERNEL_GPT_H
#include "kernel/block.h"
#define GPT_ENTRY_MAX 128U
struct gpt_partition {
 uint64_t first, last;
 uint32_t number;
 uint8_t type[16], uuid[16];
};
struct gpt_table {
 uint64_t first_usable, last_usable;
 uint8_t disk_uuid[16];
 uint32_t count, entry_count, entry_size, array_crc;
 bool backup, degraded;
 struct gpt_partition partitions[GPT_ENTRY_MAX];
};
/* Read-only validation. Zero means no protective GPT; errors publish nothing. */
int gpt_read(struct block_device *disk, struct gpt_table *table);
void gpt_discover(void);
#endif
