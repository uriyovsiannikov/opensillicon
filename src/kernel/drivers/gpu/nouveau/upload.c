/* SPDX-License-Identifier: BSD-2-Clause
 * Reversible instance upload, independent of the MMIO transport. */
#include "kernel/nouveau_upload.h"
static bool owned(struct nouveau_upload *u)
{
	struct nouveau_vram_range range;
	return u && u->io && u->io->read32 && u->io->write32 && u->io->owns &&
	    u->backup && nouveau_vram_pool_query(u->allocation,&range) &&
	    range.start==u->address && range.size==u->length &&
	    u->io->owns(u->io->context,u->address,u->length);
}
enum nouveau_upload_status nouveau_upload_save(struct nouveau_upload *u,
	const struct nouveau_upload_io *io,struct nouveau_vram_allocation allocation,
	uint32_t *backup,size_t length)
{
	struct nouveau_vram_range range;
	if(!u || u->state!=NOUVEAU_UPLOAD_EMPTY || !io || !io->owns ||
	    !io->read32 || !io->write32 || !backup || ((uintptr_t)backup&3) ||
	    !length || length>NOUVEAU_UPLOAD_LIMIT || (length&3) ||
	    !nouveau_vram_pool_query(allocation,&range) || (range.start&3) ||
	    range.size!=length) return NOUVEAU_UPLOAD_INVALID;
	if(!io->owns(io->context,range.start,range.size)) return NOUVEAU_UPLOAD_NOT_OWNED;
	for(size_t i=0;i<length/4;i++)
		if(!io->read32(io->context,range.start+i*4,&backup[i])) return NOUVEAU_UPLOAD_READ_FAILED;
	*u=(struct nouveau_upload){.state=NOUVEAU_UPLOAD_SAVED,.allocation=allocation,
	    .address=range.start,.length=length,.backup=backup,.io=io};
	return NOUVEAU_UPLOAD_OK;
}
enum nouveau_upload_status nouveau_upload_restore(struct nouveau_upload *u)
{
	if(!u || (u->state!=NOUVEAU_UPLOAD_DIRTY && u->state!=NOUVEAU_UPLOAD_VERIFIED &&
	    u->state!=NOUVEAU_UPLOAD_QUARANTINED)) return NOUVEAU_UPLOAD_INVALID;
	u->state=NOUVEAU_UPLOAD_QUARANTINED;
	if(!owned(u)) return NOUVEAU_UPLOAD_NOT_OWNED;
	/* Continue past a failed write so every original DWORD gets a restore
	 * attempt; read-back is mandatory even when callbacks report success. */
	bool ok=true;
	for(size_t i=0;i<u->length/4;i++)
		if(!u->io->write32(u->io->context,u->address+i*4,u->backup[i])) ok=false;
	for(size_t i=0;i<u->length/4;i++) {
		uint32_t value;
		if(!u->io->read32(u->io->context,u->address+i*4,&value) || value!=u->backup[i]) ok=false;
	}
	if(!ok) return NOUVEAU_UPLOAD_RESTORE_FAILED;
	u->state=NOUVEAU_UPLOAD_RESTORED;return NOUVEAU_UPLOAD_OK;
}
enum nouveau_upload_status nouveau_upload_image(struct nouveau_upload *u,const uint32_t *image)
{
	if(!u || u->state!=NOUVEAU_UPLOAD_SAVED || !image || ((uintptr_t)image&3))
		return NOUVEAU_UPLOAD_INVALID;
	if(!owned(u)) return NOUVEAU_UPLOAD_NOT_OWNED;
	/* Aliasing the backup could destroy rollback data while uploading. */
	uintptr_t a=(uintptr_t)image,b=(uintptr_t)u->backup;
	if(a>UINTPTR_MAX-u->length || b>UINTPTR_MAX-u->length ||
	    (a<b+u->length && b<a+u->length)) return NOUVEAU_UPLOAD_INVALID;
	u->state=NOUVEAU_UPLOAD_DIRTY;
	for(size_t i=0;i<u->length/4;i++)
		if(!u->io->write32(u->io->context,u->address+i*4,image[i])) goto rollback;
	for(size_t i=0;i<u->length/4;i++) {
		uint32_t value;
		if(!u->io->read32(u->io->context,u->address+i*4,&value) || value!=image[i]) goto rollback;
	}
	u->state=NOUVEAU_UPLOAD_VERIFIED;return NOUVEAU_UPLOAD_OK;
rollback:
	return nouveau_upload_restore(u)==NOUVEAU_UPLOAD_OK ?
	    NOUVEAU_UPLOAD_FAILED_RESTORED:NOUVEAU_UPLOAD_RESTORE_FAILED;
}
bool nouveau_upload_pin(struct nouveau_upload *u)
{
	if(!u || u->state!=NOUVEAU_UPLOAD_VERIFIED || !owned(u)) return false;
	u->state=NOUVEAU_UPLOAD_PINNED;return true;
}
bool nouveau_upload_unpin(struct nouveau_upload *u)
{
	if(!u || u->state!=NOUVEAU_UPLOAD_PINNED || !owned(u)) return false;
	u->state=NOUVEAU_UPLOAD_VERIFIED;return true;
}
bool nouveau_upload_finish(struct nouveau_upload *u)
{
	if(!u || (u->state!=NOUVEAU_UPLOAD_SAVED && u->state!=NOUVEAU_UPLOAD_RESTORED)) return false;
	if(!nouveau_vram_pool_free(&u->allocation)) return false;
	*u=(struct nouveau_upload){0};return true;
}
