#include <errno.h>
#include <string.h>
#include "storage.h"
#define SFS_BLOCK 4096U
static uint32_t
ceiling(uint64_t a, uint32_t b)
{
	return (uint32_t)((a + b - 1) / b);
}
int
sfs_plan(const struct storage_io *io, uint32_t inodes, struct sfs_layout *p)
{
	if (io == NULL || p == NULL ||
	    (io->sector != 512 && io->sector != 4096) ||
	    io->bytes % SFS_BLOCK || io->bytes / SFS_BLOCK > UINT32_MAX ||
	    io->bytes == 0 || inodes == 0)
		return -EINVAL;
	uint32_t blocks = (uint32_t)(io->bytes / SFS_BLOCK);
	*p = (struct sfs_layout){.blocks = blocks,
	    .inodes = inodes,
	    .bb_start = 1,
	    .bb_count = ceiling(blocks, 32768),
	    .ib_count = ceiling(inodes, 32768),
	    .it_count = ceiling(inodes, 32)};
	p->ib_start = p->bb_start + p->bb_count;
	p->it_start = p->ib_start + p->ib_count;
	uint64_t data = (uint64_t)p->it_start + p->it_count;
	if (data >= blocks)
		return -ENOSPC;
	p->data_start = (uint32_t)data;
	return 0;
}
static int
put_block(const struct storage_io *io, uint32_t block, const void *p)
{
	return io->write(
	    io->context, (uint64_t)block * SFS_BLOCK, p, SFS_BLOCK);
}
int
sfs_format(const struct storage_io *io, uint32_t inodes)
{
	struct sfs_layout p;
	int error = sfs_plan(io, inodes, &p);
	uint8_t block[SFS_BLOCK];
	if (error)
		return error;
	if (!io->write || !io->read || !io->flush)
		return -EINVAL;
	/* Publish the superblock only after all metadata is durable. */
	memset(block, 0, sizeof(block));
	if ((error = put_block(io, 0, block)) ||
	    (error = io->flush(io->context)))
		return error;
	for (uint32_t i = 0; i < p.bb_count; i++) {
		memset(block, 0, sizeof(block));
		for (uint32_t bit = 0; bit < 32768; bit++) {
			uint64_t index = (uint64_t)i * 32768 + bit;
			if (index <= p.data_start || index >= p.blocks)
				block[bit / 8] |= (uint8_t)(1U << (bit % 8));
		}
		if ((error = put_block(io, p.bb_start + i, block)))
			return error;
	}
	for (uint32_t i = 0; i < p.ib_count; i++) {
		memset(block, 0, sizeof(block));
		for (uint32_t bit = 0; bit < 32768; bit++) {
			uint64_t index = (uint64_t)i * 32768 + bit;
			if (index == 0 || index >= p.inodes)
				block[bit / 8] |= (uint8_t)(1U << (bit % 8));
		}
		if ((error = put_block(io, p.ib_start + i, block)))
			return error;
	}
	for (uint32_t i = 0; i < p.it_count; i++) {
		memset(block, 0, sizeof(block));
		if (i == 0) {
			storage_le16(block, 0040755);
			storage_le64(block + 16, SFS_BLOCK);
			storage_le32(block + 24, p.data_start);
		}
		if ((error = put_block(io, p.it_start + i, block)))
			return error;
	}
	memset(block, 0, sizeof(block));
	storage_le32(block, 1);
	storage_le16(block + 4, 12);
	block[6] = 1;
	block[7] = 2;
	block[8] = '.';
	storage_le32(block + 12, 1);
	storage_le16(block + 16, SFS_BLOCK - 12);
	block[18] = 2;
	block[19] = 2;
	block[20] = '.';
	block[21] = '.';
	if ((error = put_block(io, p.data_start, block)) ||
	    (error = io->flush(io->context)))
		return error;
	memset(block, 0, sizeof(block));
	memcpy(block, "SILFS1\0\0", 8);
	storage_le16(block + 8, 2);
	storage_le16(block + 10, 80);
	storage_le32(block + 12, SFS_BLOCK);
	uint32_t words[] = {p.blocks, p.inodes, 1, p.bb_start, p.bb_count,
	    p.ib_start, p.ib_count, p.it_start, p.it_count, p.data_start};
	for (unsigned i = 0; i < 10; i++)
		storage_le32(block + 28 + i * 4, words[i]);
	storage_le32(block + 72, storage_crc32(block, sizeof(block)));
	if ((error = put_block(io, 0, block)) ||
	    (error = io->flush(io->context)))
		return error;
	uint8_t verify[SFS_BLOCK];
	if ((error = io->read(io->context, 0, verify, sizeof(verify))))
		return error;
	return memcmp(block, verify, sizeof(block)) == 0 ? 0 : -EIO;
}
