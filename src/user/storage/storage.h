#ifndef OPENSILLICON_STORAGE_H
#define OPENSILLICON_STORAGE_H
#include <stddef.h>
#include <stdint.h>
struct storage_io {
	void *context;
	uint64_t bytes;
	uint32_t sector;
	int (*read)(void *, uint64_t, void *, size_t);
	int (*write)(void *, uint64_t, const void *, size_t);
	int (*flush)(void *);
};
struct storage_device {
	int fd;
	struct storage_io io;
};
int storage_open(struct storage_device *, const char *path, int writable);
int storage_close(struct storage_device *);
uint32_t storage_crc32(const void *, size_t);
uint32_t storage_crc32_update(uint32_t previous, const void *, size_t);
void storage_le16(void *, uint16_t);
void storage_le32(void *, uint32_t);
void storage_le64(void *, uint64_t);
struct sfs_layout {
	uint32_t blocks, inodes, bb_start, bb_count, ib_start, ib_count;
	uint32_t it_start, it_count, data_start;
};
int sfs_plan(const struct storage_io *, uint32_t inodes, struct sfs_layout *);
int sfs_format(const struct storage_io *, uint32_t inodes);
struct gpt_layout {
	uint64_t sectors, first_usable, last_usable, table_sectors;
	uint64_t esp_first, esp_sectors, root_first, root_sectors;
};
int gpt_layout_plan(
    const struct storage_io *, uint32_t esp_mib, struct gpt_layout *);
int gpt_layout_write(
    const struct storage_io *, uint32_t esp_mib, const uint8_t uuids[48]);
struct esp_file {
	void *context;
	uint64_t bytes;
	int (*read)(void *, uint64_t, void *, size_t);
};
/* Input order: BOOTX64.EFI, limine.conf, kernel.elf, init.cpi. */
struct fat32_layout {
	uint32_t sectors, fat_sectors, data_start, clusters;
};
int fat32_plan(const struct storage_io *, struct fat32_layout *);
/* Returns the config length, or a negative errno. Must fit in one sector. */
int esp_limine_config(char *, size_t, const uint8_t root_uuid[16]);
int esp_format(const struct storage_io *, uint32_t hidden_sectors,
    uint32_t serial, const struct esp_file files[4]);
/* Replaces the installed kernel, initramfs and Limine configuration. */
int esp_update_boot(const struct storage_io *, const struct esp_file *kernel,
    const struct esp_file *initramfs, const uint8_t root_uuid[16]);
#endif
