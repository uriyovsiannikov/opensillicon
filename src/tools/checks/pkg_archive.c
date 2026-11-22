#include "format.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *fixture(uint32_t count, size_t *length)
{
	*length = PKG_HEADER_SIZE + (size_t)count * PKG_ENTRY_SIZE;
	uint8_t *bytes = calloc(1, *length);
	assert(bytes != NULL);
	memcpy(bytes, PKG_ARCHIVE_MAGIC, 8);
	pkg_put_u32(bytes + 8, (uint32_t)*length);
	pkg_put_u32(bytes + 12, count);
	strcpy((char *)bytes + 16, "table-check");
	strcpy((char *)bytes + 80, "1.0");
	for (uint32_t i = 0; i < count; i++) {
		uint8_t *header = bytes + PKG_HEADER_SIZE + i * PKG_ENTRY_SIZE;
		snprintf((char *)header, PKG_PATH_SIZE, "usr/share/table/file-%04u", i);
		pkg_put_u32(header + 192, 0644);
		pkg_sha256("", 0, header + 200);
	}
	return bytes;
}

int main(void)
{
	struct pkg_archive archive;
	size_t length;
	uint8_t *bytes = fixture(PKG_MAX_FILES, &length);
	assert(pkg_parse(bytes, length, 0, &archive) == NULL);
	assert(archive.count == PKG_MAX_FILES);
	assert(strcmp(archive.files[129].path, "usr/share/table/file-0129") == 0);
	pkg_archive_release(&archive);
	/* Installation records have the same file table without payloads. */
	memcpy(bytes, PKG_RECORD_MAGIC, 8);
	pkg_put_u32(bytes + 8, PKG_INSTALLED);
	assert(pkg_parse(bytes, length, 1, &archive) == NULL);
	assert(archive.installed == PKG_INSTALLED && archive.count == PKG_MAX_FILES);
	pkg_archive_release(&archive);
	memcpy(bytes, PKG_ARCHIVE_MAGIC, 8);
	pkg_put_u32(bytes + 8, (uint32_t)length);
	uint8_t *last = bytes + length - PKG_ENTRY_SIZE;
	/* Rejections after table allocation must also release that table. */
	for (unsigned i = 0; i < 8; i++) {
		strcpy((char *)last, "usr/share/table/file-0000");
		assert(pkg_parse(bytes, length, 0, &archive) != NULL);
		strcpy((char *)last, "usr/share/table/file-0000/child");
		assert(pkg_parse(bytes, length, 0, &archive) != NULL);
		strcpy((char *)last, "usr/share/table/../escape");
		assert(pkg_parse(bytes, length, 0, &archive) != NULL);
	}
	strcpy((char *)last, "usr/share/table/file-last");
	last[200] ^= 1;
	assert(pkg_parse(bytes, length, 0, &archive) != NULL);
	last[200] ^= 1;
	pkg_put_u32(last + 196, 1);
	assert(pkg_parse(bytes, length, 0, &archive) != NULL);
	pkg_put_u32(last + 196, 0);
	pkg_put_u32(bytes + 12, UINT32_MAX);
	assert(pkg_parse(bytes, length, 0, &archive) != NULL);
	pkg_put_u32(bytes + 12, PKG_MAX_FILES);
	pkg_put_u32(bytes + 8, PKG_HEADER_SIZE);
	assert(pkg_parse(bytes, PKG_HEADER_SIZE, 0, &archive) != NULL);
	free(bytes);
	bytes = fixture(PKG_MAX_FILES + 1, &length);
	assert(pkg_parse(bytes, length, 0, &archive) != NULL);
	free(bytes);
	puts("package archive bounds and error cleanup passed");
	return 0;
}
