#include "format.h"
#include "http.h"
#include "io.h"
#include "sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#ifdef PKG_HOST
#include <sys/file.h>
#else
#include <sys/socket.h>
#endif

static const char *root = "";
static const char *repository = "https://packages.chipset.bond";
static char configured_repository[256];
static struct pkg_index_entry index_entries[PKG_MAX_PACKAGES];
static size_t index_count;
static char target_path[256];

static int rooted(char out[256], const char *path)
{
	int n = snprintf(out, 256, "%s/%s", root, path);
	if (n < 0 || n >= 256) {
		fprintf(stderr, "pkg: path too long: %s\n", path);
		return -1;
	}
	return 0;
}
static int fail(const char *message)
{
	fprintf(stderr, "pkg: %s\n", message);
	return 1;
}
static int ensure_directory(const char *relative)
{
	char path[256];
	if (rooted(path, relative) < 0 || pkg_mkdirs(path) < 0) {
		perror("pkg: create directory");
		return -1;
	}
	return 0;
}

/* Hold the lock for the process lifetime. A killed process releases it too. */
static int lock_database(void)
{
	char path[256];

	if (ensure_directory("var/db/pkg") < 0 ||
	    rooted(path, "var/db/pkg") < 0)
		return -1;
#ifdef PKG_HOST
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0)
		return fd;
#else
	const struct sockaddr_un address = { .sun_family = AF_UNIX,
	    .sun_path = "\0opensillicon:pkg" };
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd >= 0 && bind(fd, (const struct sockaddr *)&address,
	    offsetof(struct sockaddr_un, sun_path) +
	    sizeof("\0opensillicon:pkg") - 1) == 0)
		return fd;
#endif
	perror("pkg: lock database");
	if (fd >= 0)
		close(fd);
	return -1;
}
static int record_path(char out[256], const char *name)
{
	char relative[128];
	if (!pkg_identifier(name, PKG_NAME_SIZE))
		return -1;
	snprintf(relative, sizeof(relative), "var/db/pkg/%s.db", name);
	return rooted(out, relative);
}
static int save_file(const char *relative, const void *bytes, size_t length)
{
	char path[256];
	if (rooted(path, relative) < 0)
		return -1;
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0)
		return -1;
	int error = pkg_write_all(fd, bytes, length);
	if (close(fd) < 0)
		error = -1;
	return error;
}
static int read_index(void)
{
	char path[256];
	size_t length;
	if (rooted(path, "var/cache/pkg/index") < 0)
		return 1;
	uint8_t *bytes = pkg_read_file(path, PKG_INDEX_MAX, &length);
	if (!bytes)
		return fail("no repository index; run pkg update first");
	if (memchr(bytes, 0, length)) {
		free(bytes);
		return fail("invalid repository index");
	}
	const char *error = pkg_index_parse((char *)bytes, index_entries, &index_count);
	if (error)
		fprintf(stderr, "pkg: %s\n", error);
	free(bytes);
	return error != NULL;
}
static int update_index(void)
{
	printf("Fetching repository index from %s\n", repository);
	size_t size;
	uint8_t *bytes = pkg_http_get(repository, "/index", PKG_INDEX_MAX, &size);
	if (!bytes)
		return 1;
	uint8_t *copy = malloc(size + 1);
	if (!copy) {
		free(bytes);
		return fail("out of memory");
	}
	memcpy(copy, bytes, size + 1);
	const char *error =
	    memchr(bytes, 0, size)
		? "invalid repository index"
		: pkg_index_parse((char *)copy, index_entries, &index_count);
	free(copy);
	int result = 1;
	if (error)
		fprintf(stderr, "pkg: %s\n", error);
	else if (ensure_directory("var/cache/pkg") < 0 ||
		 save_file("var/cache/pkg/index", bytes, size) < 0)
		perror("pkg: save index");
	else {
		printf("Repository updated: %zu packages\n", index_count);
		result = 0;
	}
	free(bytes);
	return result;
}
static int installed_record(const char *name, struct pkg_archive *package,
			    uint8_t **bytes)
{
	char path[256];
	size_t size;
	if (record_path(path, name) < 0)
		return fail("invalid package name");
	*bytes = pkg_read_file(path, PKG_HEADER_SIZE + PKG_MAX_FILES * PKG_ENTRY_SIZE,
			       &size);
	if (!*bytes)
		return fail("package is not installed or its record is unreadable");
	const char *error = pkg_parse(*bytes, size, 1, package);
	if (!error && strcmp(package->name, name))
		error = "package database name mismatch";
	if (error) {
		free(*bytes);
		*bytes = NULL;
		return fail(error);
	}
	return 0;
}

/* An incomplete installation still reserves all its paths. */
static int check_reservations(const struct pkg_archive *package)
{
	char path[256];
	if (rooted(path, "var/db/pkg") < 0)
		return 1;
	DIR *dir = opendir(path);
	if (!dir) {
		perror("pkg: database");
		return 1;
	}
	int result = 0;
	for (;;) {
		errno = 0;
		const struct dirent *entry = readdir(dir);
		if (!entry) {
			if (errno != 0) {
				perror("pkg: read database");
				result = 1;
			}
			break;
		}
		size_t n = strlen(entry->d_name);
		if (n < 4 || strcmp(entry->d_name + n - 3, ".db"))
			continue;
		char name[PKG_NAME_SIZE];
		if (n - 3 >= sizeof(name)) {
			result = fail("invalid package database filename");
			break;
		}
		memcpy(name, entry->d_name, n - 3);
		name[n - 3] = 0;
		if (!strcmp(name, package->name))
			continue;
		struct pkg_archive other;
		uint8_t *bytes;
		if (installed_record(name, &other, &bytes)) {
			result = 1;
			break;
		}
		for (uint32_t i = 0; i < package->count && !result; i++) {
			for (uint32_t j = 0; j < other.count; j++) {
				const char *ours = package->files[i].path;
				const char *theirs = other.files[j].path;
				size_t a = strlen(ours), b = strlen(theirs);
				size_t shorter = a < b ? a : b;
				if (memcmp(ours, theirs, shorter) == 0 &&
				    (a == b || (a < b ? theirs[a] : ours[b]) == '/')) {
					fprintf(stderr, "pkg: /%s is reserved by %s\n",
					    package->files[i].path, name);
					result = 1;
					break;
				}
			}
		}
		free(bytes);
		if (result)
			break;
	}
	closedir(dir);
	return result;
}
static int check_parents(const char *path)
{
	char parent[256];
	struct stat st;

	strcpy(parent, path);
	for (char *s = parent + 1; *s; s++) {
		if (*s != '/')
			continue;
		*s = 0;
#ifdef PKG_HOST
		int exists = lstat(parent, &st);
#else
		int exists = stat(parent, &st);
#endif
		if (exists == 0 && !S_ISDIR(st.st_mode))
			return fail("destination parent is not a directory");
		if (exists < 0 && errno != ENOENT) {
			perror("pkg: parent lookup");
			return 1;
		}
		*s = '/';
	}
	return 0;
}

/* A missing payload was never written or was removed by a previous attempt. */
static int check_payload(const struct pkg_file *file, bool incomplete)
{
	struct stat st;
	if (rooted(target_path, file->path) < 0 || check_parents(target_path))
		return -1;
#ifdef PKG_HOST
	int exists = lstat(target_path, &st);
#else
	int exists = stat(target_path, &st);
#endif
	if (exists < 0 && errno == ENOENT)
		return 0;
	if (exists == 0 && S_ISREG(st.st_mode)) {
		size_t size;
		uint8_t *data = pkg_read_file(target_path, file->size, &size);
		if (data) {
			uint8_t hash[32];
			pkg_sha256(data, size, hash);
			int matches = size == file->size &&
			    (st.st_mode & 07777) == file->mode &&
			    memcmp(hash, file->hash, sizeof(hash)) == 0;
			/* Interrupted writes keep the installer's private mode. */
			if (incomplete && (st.st_mode & 07777) == 0600 &&
			    st.st_uid == geteuid())
				matches = 1;
			free(data);
			if (matches)
				return 1;
		}
	}
	fprintf(stderr, "pkg: unexpected or modified file: /%s\n", file->path);
	return -1;
}

static int sync_record(int fd)
{
	/* RAM filesystems need no flush. */
	return fsync(fd) == 0 || errno == EOPNOTSUPP ? 0 : -1;
}

static int set_record_state(int fd, uint8_t state)
{
	return lseek(fd, 8, SEEK_SET) == 8 &&
	    pkg_write_all(fd, &state, sizeof(state)) == 0 &&
	    sync_record(fd) == 0 ? 0 : -1;
}

static int remove_package(const char *name)
{
	struct pkg_archive package;
	uint8_t *bytes;
	char db[256];
	if (record_path(db, name) < 0)
		return fail("invalid package name");
	if (lock_database() < 0 || installed_record(name, &package, &bytes))
		return 1;
	int result = 1;
	if (check_reservations(&package))
		goto done;
	for (uint32_t i = 0; i < package.count; i++)
		if (check_payload(&package.files[i],
		    package.installed == PKG_INCOMPLETE) < 0)
			goto done;
	int fd = open(db, O_WRONLY | O_NOFOLLOW);
	if (fd < 0) {
		perror(db);
		goto done;
	}
	uint8_t state = package.installed == PKG_INCOMPLETE ?
	    PKG_INCOMPLETE : PKG_DELETING;
	if (set_record_state(fd, state) < 0) {
		perror(db);
		goto close_record;
	}
	for (uint32_t i = 0; i < package.count; i++) {
		int state = check_payload(&package.files[i],
		    package.installed == PKG_INCOMPLETE);
		if (state < 0)
			goto close_record;
		if (state == 1 && unlink(target_path) < 0) {
			perror(target_path);
			goto close_record;
		}
	}
	if (sync_record(fd) < 0 || unlink(db) < 0 || sync_record(fd) < 0) {
		perror(db);
		goto close_record;
	}
	printf("Removed %s\n", name);
	result = 0;
close_record:
	if (close(fd) < 0)
		result = 1;
done:
	free(bytes);
	return result;
}

static int preflight(const struct pkg_archive *p)
{
	struct stat st;
	char db[256];
	if (record_path(db, p->name) < 0)
		return fail("invalid database path");
	if (stat(db, &st) == 0)
		return fail(
		    "package already has an installation record; use pkg remove first");
	if (errno != ENOENT) {
		perror("pkg: database lookup");
		return 1;
	}
	for (uint32_t i = 0; i < p->count; i++) {
		if (rooted(target_path, p->files[i].path) < 0 ||
		    check_parents(target_path))
			return 1;
#ifdef PKG_HOST
		int exists = lstat(target_path, &st);
#else
		int exists = stat(target_path, &st);
#endif
		if (exists == 0) {
			fprintf(stderr, "pkg: file already exists: /%s\n",
				p->files[i].path);
			return 1;
		}
		if (errno != ENOENT) {
			perror("pkg: destination lookup");
			return 1;
		}
	}
	return 0;
}

static int install_archive(const uint8_t *data, size_t size,
			   const struct pkg_index_entry *expected)
{
	struct pkg_archive package;
	const char *error = pkg_parse(data, size, 0, &package);
	if (error)
		return fail(error);
	if (expected && (strcmp(package.name, expected->name) ||
			 strcmp(package.version, expected->version)))
		return fail("downloaded package does not match repository metadata");
	if (lock_database() < 0)
		return 1;
	char db[256];
	if (record_path(db, package.name) < 0)
		return 1;
	uint8_t header[PKG_HEADER_SIZE];
	memcpy(header, data, sizeof(header));
	memcpy(header, PKG_RECORD_MAGIC, 8);
	pkg_put_u32(header + 8, PKG_INCOMPLETE);
	if (preflight(&package))
		return 1;
	if (check_reservations(&package))
		return 1;
	/* O_EXCL reserves ownership before any payload is written. Keep pending
	 * records on error so partial installations are visible after a reboot. */
	int record = open(db, O_WRONLY | O_NOFOLLOW | O_CREAT | O_EXCL, 0644);
	if (record < 0) {
		perror("pkg: reserve installation record");
		return 1;
	}
	int result = 1;
	if (pkg_write_all(record, header, sizeof(header)) < 0)
		goto bad_manifest;
	for (uint32_t i = 0; i < package.count; i++)
		if (pkg_write_all(record, package.files[i].header, PKG_ENTRY_SIZE) < 0)
			goto bad_manifest;
	if (sync_record(record) < 0)
		goto failed;
	for (uint32_t i = 0; i < package.count; i++) {
		struct pkg_file *f = &package.files[i];
		char parent[256];
		if (rooted(target_path, f->path) < 0)
			goto failed;
		strcpy(parent, target_path);
		char *slash = strrchr(parent, '/');
		*slash = 0;
		if (pkg_mkdirs(parent) < 0)
			goto failed;
		int fd = open(target_path, O_WRONLY | O_NOFOLLOW | O_CREAT | O_EXCL, 0600);
		if (fd < 0)
			goto failed;
		int written = pkg_write_all(fd, f->data, f->size);
		if (written == 0)
			written = fchmod(fd, f->mode);
		if (close(fd) < 0)
			written = -1;
		if (written < 0)
			goto failed;
	}
	/* Only the low byte changes. Keep the manifest usable after interruption. */
	if (sync_record(record) < 0 ||
	    set_record_state(record, PKG_INSTALLED) < 0)
		goto failed;
	printf("Installed %s %s (%u files)\n", package.name, package.version,
	       package.count);
	result = 0;
	goto done;
bad_manifest:
	perror("pkg: write installation manifest");
	if (unlink(db) < 0)
		perror(db);
	goto done;
failed:
	fprintf(stderr, "pkg: installation incomplete (errno %d); record kept at %s\n",
		errno, db);
done:
	if (close(record) < 0)
		result = 1;
	return result;
}
static int install_remote(const char *name)
{
	if (!pkg_identifier(name, PKG_NAME_SIZE))
		return fail("invalid package name");
	if (read_index())
		return 1;
	struct pkg_index_entry *entry = NULL;
	for (size_t i = 0; i < index_count; i++)
		if (!strcmp(name, index_entries[i].name)) {
			entry = &index_entries[i];
			break;
		}
	if (!entry)
		return fail("package not found in repository index");
	char path[160];
	snprintf(path, sizeof(path), "/packages/%s", entry->filename);
	printf("Downloading %s %s (%u bytes)\n", entry->name, entry->version,
	       entry->size);
	size_t size;
	uint8_t *bytes = pkg_http_get(repository, path, PKG_MAX_SIZE, &size);
	if (!bytes)
		return 1;
	uint8_t hash[32];
	char hex[65];
	pkg_sha256(bytes, size, hash);
	pkg_hex(hash, hex);
	int result;
	if (size != entry->size || strcmp(hex, entry->hash))
		result = fail(
		    "repository package SHA-256 or length mismatch; run pkg update");
	else
		result = install_archive(bytes, size, entry);
	free(bytes);
	return result;
}
static int
load_repository_file(const char *path)
{
	size_t size;
	uint8_t *bytes = pkg_read_file(path, sizeof(configured_repository) - 1, &size);
	if (!bytes) {
		if (errno == ENOENT)
			return 0;
		perror(path);
		return -1;
	}
	while (size && (bytes[size - 1] == '\n' || bytes[size - 1] == '\r'))
		size--;
	if (size < 8 ||
	    memchr(bytes, 0, size) || memchr(bytes, '\r', size) ||
	    memchr(bytes, '\n', size)) {
		fprintf(stderr, "pkg: %s must contain one repository URL\n", path);
		free(bytes);
		return -1;
	}
	bytes[size] = 0;
	if (!pkg_http_repository_valid((const char *)bytes)) {
		fprintf(stderr, "pkg: %s contains an invalid repository URL\n", path);
		free(bytes);
		return -1;
	}
	memcpy(configured_repository, bytes, size);
	configured_repository[size] = 0;
	free(bytes);
	repository = configured_repository;
	return 1;
}

static int
load_repository(void)
{
	char path[256];
	if (rooted(path, "etc/pkg.conf") < 0)
		return 1;
	int result = load_repository_file(path);
#ifndef PKG_HOST
	if (result == 0 && *root)
		result = load_repository_file("/etc/pkg.conf");
#endif
	return result < 0;
}

static int
save_repository(const char *url)
{
	char line[256], path[256];
	int n = snprintf(line, sizeof(line), "%s\n", url);
	if (n < 9 || (size_t)n >= sizeof(line) || !pkg_http_repository_valid(url) ||
	    strcspn(url, " \t\r\n") != strlen(url))
		return fail("repository must use https://host[:port]");
	if (ensure_directory("etc") < 0 || rooted(path, "etc/pkg.conf") < 0)
		return 1;
	if (save_file("etc/pkg.conf", line, (size_t)n) < 0) {
		perror("pkg: save repository");
		return 1;
	}
	printf("Repository saved in %s\nRun pkg update to fetch its index.\n", path);
	return 0;
}

static int usage(void)
{
	puts("usage: pkg repo URL | pkg add name|file.spkg | pkg update | pkg remove name");
	return 2;
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
#ifdef PKG_HOST
	/* Host integration tests use an isolated filesystem tree. */
	if (argc >= 3 && strcmp(argv[1], "-r") == 0) {
		if (argv[2][0] != '/')
			return fail("installation root must be absolute");
		root = argv[2];
		argv += 2;
		argc -= 2;
	}
#endif
	if (argc < 2)
		return usage();
	const char *command = argv[1];
	int valid = (argc == 2 && strcmp(command, "update") == 0) ||
	    (argc == 3 && (strcmp(command, "repo") == 0 ||
	    strcmp(command, "add") == 0 || strcmp(command, "remove") == 0));
	if (!valid)
		return usage();
	if (!*root && geteuid() != 0)
		return fail("root privileges required");
	if (strcmp(command, "repo") == 0)
		return save_repository(argv[2]);
	if (strcmp(command, "update") == 0)
		return load_repository() || update_index();
	if (strcmp(command, "remove") == 0)
		return remove_package(argv[2]);

	const char *operand = argv[2];
	size_t length = strlen(operand);
	if (!strchr(operand, '/') &&
	    (length < 5 || strcmp(operand + length - 5, ".spkg")))
		return load_repository() || install_remote(operand);
	size_t size;
	uint8_t *bytes = pkg_read_file(operand, PKG_MAX_SIZE, &size);
	if (!bytes) {
		perror("pkg: read archive");
		return 1;
	}
	int result = install_archive(bytes, size, NULL);
	free(bytes);
	return result;
}
