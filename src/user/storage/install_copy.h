#ifndef OPENSILLICON_INSTALL_COPY_H
#define OPENSILLICON_INSTALL_COPY_H
#include <stddef.h>
#include <stdint.h>
#define INSTALL_PATH_MAX 256U
struct install_entry {
	char path[INSTALL_PATH_MAX];
	uint64_t size, inode;
	uint32_t mode, uid, gid, crc;
};
struct install_tree {
	struct install_entry *entries;
	size_t count, capacity;
	uint64_t file_bytes, required_blocks;
	char source[INSTALL_PATH_MAX], failed_path[INSTALL_PATH_MAX];
};
int install_tree_scan(struct install_tree *, const char *source);
int install_tree_recheck(struct install_tree *);
int install_tree_target_check(const struct install_tree *, const char *target);
int install_tree_copy(struct install_tree *, const char *target,
    int (*progress)(void *, const char *, size_t, size_t), void *context);
int install_tree_verify(struct install_tree *, const char *target,
    int (*progress)(void *, const char *, size_t, size_t), void *context);
void install_tree_free(struct install_tree *);
#endif
