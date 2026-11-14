#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "storage.h"
#include "install_copy.h"
#define ENTRY_LIMIT 8192U
#define FILE_LIMIT (1036ULL * 4096ULL)
static int
join(char path[INSTALL_PATH_MAX], const char *root, const char *relative)
{
	size_t a = strlen(root), b = strlen(relative);
	while (a > 1 && root[a - 1] == '/')
		a--;
	if (a == 1 && root[0] == '/')
		a = 0;
	if (strcmp(relative, "/") == 0)
		b = 0;
	if (a + b >= INSTALL_PATH_MAX)
		return -ENAMETOOLONG;
	memcpy(path, root, a);
	memcpy(path + a, relative, b);
	path[a + b] = 0;
	if (!a && !b)
		strcpy(path, "/");
	return 0;
}
static int
metadata(const char *path, struct stat *st)
{
	return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW) == 0 ? 0
								     : -errno;
}
static int
same(const struct stat *st, const struct install_entry *e)
{
	return (uint64_t)st->st_ino == e->inode &&
	    (uint64_t)st->st_size == e->size && st->st_mode == e->mode &&
	    st->st_uid == e->uid && st->st_gid == e->gid;
}
static int
checksum_fd(int fd, uint64_t expected, uint32_t *crc)
{
	uint8_t buffer[4096];
	uint64_t size = 0;
	*crc = 0;
	for (;;) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if (n < 0)
			return -errno;
		if (!n)
			break;
		size += (size_t)n;
		if (size > expected)
			return -EAGAIN;
		*crc = storage_crc32_update(*crc, buffer, (size_t)n);
	}
	return size == expected ? 0 : -EAGAIN;
}
static int
append(struct install_tree *tree, const char *relative)
{
	if (tree->count == ENTRY_LIMIT)
		return -E2BIG;
	char path[INSTALL_PATH_MAX];
	int error = join(path, tree->source, relative);
	if (error)
		return error;
	struct stat st;
	if ((error = metadata(path, &st)))
		return error;
	if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
		return -EINVAL;
	if (S_ISREG(st.st_mode) &&
	    (st.st_size < 0 || (uint64_t)st.st_size > FILE_LIMIT))
		return -EFBIG;
	if (tree->count == tree->capacity) {
		size_t capacity = tree->capacity ? tree->capacity * 2 : 128;
		void *p =
		    realloc(tree->entries, capacity * sizeof(*tree->entries));
		if (!p)
			return -ENOMEM;
		tree->entries = p;
		tree->capacity = capacity;
	}
	struct install_entry *e = &tree->entries[tree->count];
	memset(e, 0, sizeof(*e));
	strcpy(e->path, relative);
	e->size = (uint64_t)st.st_size;
	e->inode = st.st_ino;
	e->mode = st.st_mode;
	e->uid = st.st_uid;
	e->gid = st.st_gid;
	if (S_ISREG(st.st_mode)) {
		int fd = open(path, O_RDONLY | O_NOFOLLOW);
		if (fd < 0)
			return -errno;
		struct stat opened;
		error = fstat(fd, &opened) == 0 ? 0 : -errno;
		if (!error && !same(&opened, e))
			error = -EAGAIN;
		if (!error)
			error = checksum_fd(fd, e->size, &e->crc);
		int close_error = close(fd);
		if (!error && close_error)
			error = -errno;
		if (error)
			return error;
		tree->file_bytes += e->size;
		tree->required_blocks +=
		    (e->size + 4095) / 4096 + (e->size > 12 * 4096 ? 1 : 0);
	}
	/* Conservative space for directory records and directory data blocks.
	 */
	tree->required_blocks += 2;
	tree->count++;
	return 0;
}
static int
skip_contents(const char *path)
{
	return !strcmp(path, "/dev") || !strcmp(path, "/proc") ||
	    !strcmp(path, "/init") || !strcmp(path, "/mnt") ||
	    !strcmp(path, "/var/run");
}
static int
entry_compare(const void *a, const void *b)
{
	return strcmp(((const struct install_entry *)a)->path,
	    ((const struct install_entry *)b)->path);
}
int
install_tree_scan(struct install_tree *tree, const char *source)
{
	if (!tree || !source || source[0] != '/' ||
	    strlen(source) >= INSTALL_PATH_MAX)
		return -EINVAL;
	memset(tree, 0, sizeof(*tree));
	char *canonical = realpath(source, NULL);
	if (!canonical)
		return -errno;
	if (strlen(canonical) >= INSTALL_PATH_MAX) {
		free(canonical);
		return -ENAMETOOLONG;
	}
	strcpy(tree->source, canonical);
	free(canonical);
	strcpy(tree->failed_path, "/");
	int error = append(tree, "/");
	if (!error && !S_ISDIR(tree->entries[0].mode))
		error = -ENOTDIR;
	for (size_t i = 0; !error && i < tree->count; i++) {
		char relative[INSTALL_PATH_MAX], path[INSTALL_PATH_MAX];
		strcpy(relative, tree->entries[i].path);
		if (!S_ISDIR(tree->entries[i].mode) || skip_contents(relative))
			continue;
		strcpy(tree->failed_path, relative);
		error = join(path, tree->source, relative);
		if (error)
			break;
		DIR *dir = opendir(path);
		if (!dir) {
			error = -errno;
			break;
		}
		for (;;) {
			errno = 0;
			struct dirent *entry = readdir(dir);
			if (!entry) {
				if (errno)
					error = -errno;
				break;
			}
			if (!strcmp(entry->d_name, ".") ||
			    !strcmp(entry->d_name, ".."))
				continue;
			if (!strcmp(relative, "/tmp") &&
			    strcmp(entry->d_name, "img1.jpg"))
				continue;
			if (strchr(entry->d_name, '/')) {
				error = -EINVAL;
				break;
			}
			char child[INSTALL_PATH_MAX];
			size_t a = strlen(relative), b = strlen(entry->d_name);
			if (a + (a > 1 ? 1 : 0) + b >= sizeof(child)) {
				error = -ENAMETOOLONG;
				break;
			}
			strcpy(child, relative);
			if (a > 1)
				child[a++] = '/';
			memcpy(child + a, entry->d_name, b + 1);
			strcpy(tree->failed_path, child);
			error = append(tree, child);
			if (error)
				break;
		}
		int result = closedir(dir);
		if (!error && result)
			error = -errno;
	}
	if (!error) {
		qsort(tree->entries, tree->count, sizeof(*tree->entries),
		    entry_compare);
		tree->failed_path[0] = 0;
	}
	return error;
}
int
install_tree_recheck(struct install_tree *tree)
{
	if (!tree || !tree->count)
		return -EINVAL;
	struct install_tree current;
	int error = install_tree_scan(&current, tree->source);
	if (!error && current.count != tree->count)
		error = -EAGAIN;
	for (size_t i = 0; !error && i < tree->count; i++) {
		const struct install_entry *a = &tree->entries[i],
					   *b = &current.entries[i];
		if (strcmp(a->path, b->path) || a->inode != b->inode ||
		    a->mode != b->mode || a->uid != b->uid ||
		    a->gid != b->gid ||
		    (S_ISREG(a->mode) &&
			(a->size != b->size || a->crc != b->crc))) {
			strcpy(tree->failed_path, a->path);
			error = -EAGAIN;
		}
	}
	if (error && !tree->failed_path[0])
		strcpy(tree->failed_path, "/");
	install_tree_free(&current);
	return error;
}
int
install_tree_target_check(const struct install_tree *tree, const char *target)
{
	if (!tree || !tree->count || !tree->entries || !target ||
	    target[0] != '/')
		return -EINVAL;
	char path[INSTALL_PATH_MAX];
	for (size_t i = 0; i < tree->count; i++) {
		int error = join(path, target, tree->entries[i].path);
		if (error)
			return error;
	}
	struct stat st;
	int error = metadata(target, &st);
	if (error)
		return error;
	if (!S_ISDIR(st.st_mode))
		return -ENOTDIR;
	char *canonical = realpath(target, NULL);
	if (!canonical)
		return -errno;
	if (strcmp(canonical, tree->source) == 0)
		error = -EINVAL;
	size_t length = strlen(tree->source);
	const char *relative = NULL;
	if (length == 1 && tree->source[0] == '/')
		relative = canonical;
	else if (!strncmp(canonical, tree->source, length) &&
	    canonical[length] == '/')
		relative = canonical + length;
	if (relative && strcmp(relative, "/mnt") &&
	    strncmp(relative, "/mnt/", 5))
		error = -EINVAL;
	free(canonical);
	return error;
}

static int
copy_file(const struct install_tree *tree, const struct install_entry *e,
    const char *destination)
{
	char source[INSTALL_PATH_MAX];
	int error = join(source, tree->source, e->path);
	if (error)
		return error;
	int input = open(source, O_RDONLY | O_NOFOLLOW);
	if (input < 0)
		return -errno;
	struct stat st;
	error = fstat(input, &st) == 0 ? 0 : -errno;
	if (!error && !same(&st, e))
		error = -EAGAIN;
	int output = -1;
	if (!error) {
		output = open(
		    destination, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (output < 0)
			error = -errno;
	}
	uint64_t total = 0;
	uint32_t crc = 0;
	uint8_t buffer[4096];
	while (!error) {
		ssize_t n = read(input, buffer, sizeof(buffer));
		if (n < 0) {
			error = -errno;
			break;
		}
		if (!n)
			break;
		total += (size_t)n;
		if (total > e->size) {
			error = -EAGAIN;
			break;
		}
		crc = storage_crc32_update(crc, buffer, (size_t)n);
		size_t done = 0;
		while (done < (size_t)n) {
			ssize_t w =
			    write(output, buffer + done, (size_t)n - done);
			if (w <= 0) {
				error = w < 0 ? -errno : -EIO;
				break;
			}
			done += (size_t)w;
		}
	}
	if (!error && (total != e->size || crc != e->crc))
		error = -EAGAIN;
	if (!error &&
	    (fchown(output, e->uid, e->gid) != 0 ||
		fchmod(output, e->mode & 07777) != 0 || fsync(output) != 0))
		error = -errno;
	if (!error && lseek(output, 0, SEEK_SET) != 0)
		error = -errno;
	uint32_t verify = 0;
	if (!error)
		error = checksum_fd(output, e->size, &verify);
	if (!error && verify != e->crc)
		error = -EIO;
	if (output >= 0 && close(output) != 0 && !error)
		error = -errno;
	if (close(input) != 0 && !error)
		error = -errno;
	return error;
}
int
install_tree_copy(struct install_tree *tree, const char *target,
    int (*progress)(void *, const char *, size_t, size_t), void *context)
{
	int error = install_tree_target_check(tree, target);
	if (error)
		return error;
	for (size_t i = 0; !error && i < tree->count; i++) {
		const struct install_entry *e = &tree->entries[i];
		char path[INSTALL_PATH_MAX];
		strcpy(tree->failed_path, e->path);
		error = join(path, target, e->path);
		if (error)
			break;
		if (S_ISDIR(e->mode)) {
			if (i && mkdir(path, 0700) != 0)
				error = -errno;
		} else
			error = copy_file(tree, e, path);
		if (!error && progress)
			error = progress(context, e->path, i + 1, tree->count);
	}
	for (size_t i = tree->count; !error && i > 0; i--) {
		const struct install_entry *e = &tree->entries[i - 1];
		if (!S_ISDIR(e->mode))
			continue;
		char path[INSTALL_PATH_MAX];
		strcpy(tree->failed_path, e->path);
		error = join(path, target, e->path);
		if (error)
			break;
		if (chown(path, e->uid, e->gid) != 0 ||
		    chmod(path, e->mode & 07777) != 0)
			error = -errno;
	}
	if (!error)
		tree->failed_path[0] = 0;
	return error;
}
void
install_tree_free(struct install_tree *tree)
{
	if (tree) {
		free(tree->entries);
		memset(tree, 0, sizeof(*tree));
	}
}
