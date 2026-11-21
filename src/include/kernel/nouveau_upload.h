/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_UPLOAD_H
#define KERNEL_NOUVEAU_UPLOAD_H
#include "kernel/nouveau_vram_pool.h"
#define NOUVEAU_UPLOAD_LIMIT 65536U
enum nouveau_upload_state {
	NOUVEAU_UPLOAD_EMPTY, NOUVEAU_UPLOAD_SAVED, NOUVEAU_UPLOAD_DIRTY,
	NOUVEAU_UPLOAD_VERIFIED, NOUVEAU_UPLOAD_PINNED,
	NOUVEAU_UPLOAD_QUARANTINED, NOUVEAU_UPLOAD_RESTORED
};
enum nouveau_upload_status {
	NOUVEAU_UPLOAD_OK, NOUVEAU_UPLOAD_INVALID, NOUVEAU_UPLOAD_NOT_OWNED,
	NOUVEAU_UPLOAD_READ_FAILED, NOUVEAU_UPLOAD_FAILED_RESTORED,
	NOUVEAU_UPLOAD_RESTORE_FAILED
};
struct nouveau_upload_io {
	void *context;
	bool (*read32)(void *,uint64_t address,uint32_t *value);
	bool (*write32)(void *,uint64_t address,uint32_t value);
	/* Must attest real engine/VRAM ownership and no firmware/channel access.
	 * A logical range allocation alone must NOT make this return true. */
	bool (*owns)(void *,uint64_t address,uint64_t size);
};
struct nouveau_upload {
	enum nouveau_upload_state state;
	struct nouveau_vram_allocation allocation;
	uint64_t address;
	size_t length;
	uint32_t *backup;
	const struct nouveau_upload_io *io;
};
/* Takes responsibility for the allocation only on successful backup. Caller
 * must keep io, backup and pool alive until finish. All objects are noncopyable.
 * Backup is caller storage of exactly range.size bytes, <=64KiB. */
enum nouveau_upload_status nouveau_upload_save(struct nouveau_upload *,
	const struct nouveau_upload_io *,struct nouveau_vram_allocation allocation,
	uint32_t *backup,size_t length);
/* Write + read-back the complete image; any failure automatically restores
 * and verifies the backup. Failure to restore quarantines the transaction. */
enum nouveau_upload_status nouveau_upload_image(struct nouveau_upload *,const uint32_t *image);
enum nouveau_upload_status nouveau_upload_restore(struct nouveau_upload *);
/* Pin BEFORE binding to hardware (also before a potentially failed bind).
 * Unpin only after hardware unbind + verified stop of ALL consumers. */
bool nouveau_upload_pin(struct nouveau_upload *);
bool nouveau_upload_unpin(struct nouveau_upload *);
/* Free logical allocation only after verified restoration (or no writes).
 * Does not free the caller's backup or io. */
bool nouveau_upload_finish(struct nouveau_upload *);
#endif
