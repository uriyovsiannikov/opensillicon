#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
remove_path(const char *path, int recursive, int force)
{
	struct stat status;
	if (lstat(path, &status) < 0) {
		if (force && errno == ENOENT)
			return 0;
		perror(path);
		return 1;
	}
	if (S_ISDIR(status.st_mode)) {
		if (!recursive) {
			fprintf(stderr, "rm: %s: is a directory\n", path);
			return 1;
		}
		const char *end = path + strlen(path);
		while (end > path && end[-1] == '/')
			end--;
		const char *base = end;
		while (base > path && base[-1] != '/')
			base--;
		size_t length = (size_t)(end - base);
		if (end == path || (length == 1 && base[0] == '.') ||
		    (length == 2 && base[0] == '.' && base[1] == '.')) {
			fprintf(stderr, "rm: refusing to remove root, . or ..: %s\n", path);
			return 1;
		}
		DIR *directory = opendir(path);
		if (directory == NULL) {
			perror(path);
			return 1;
		}
		int failed = 0;
		for (;;) {
			errno = 0;
			struct dirent *entry = readdir(directory);
			if (entry == NULL) {
				if (errno != 0) {
					perror(path);
					failed = 1;
				}
				break;
			}
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			size_t size = strlen(path) + strlen(entry->d_name) + 2;
			char *child = malloc(size);
			if (child == NULL) {
				perror("rm: allocate path");
				failed = 1;
				break;
			}
			snprintf(child, size, "%s/%s", path, entry->d_name);
			if (remove_path(child, recursive, force))
				failed = 1;
			free(child);
		}
		if (closedir(directory) < 0) {
			perror(path);
			failed = 1;
		}
		if (failed)
			return 1;
	}
	int result = S_ISDIR(status.st_mode) ? rmdir(path) : unlink(path);
	if (result < 0) {
		if (force && errno == ENOENT)
			return 0;
		perror(path);
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int i = 1, recursive = 0, force = 0, failed = 0;
	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (!strcmp(argv[i], "--")) { i++; break; }
		for (size_t j = 1; argv[i][j] != '\0'; j++) {
			if (argv[i][j] == 'r' || argv[i][j] == 'R') recursive = 1;
			else if (argv[i][j] == 'f') force = 1;
			else { fputs("usage: rm [-rf] [--] path ...\n", stderr); return 2; }
		}
		i++;
	}
	if (i == argc) {
		if (force) return 0;
		fputs("usage: rm [-rf] [--] path ...\n", stderr);
		return 2;
	}
	for (; i < argc; i++)
		if (remove_path(argv[i], recursive, force)) failed = 1;
	return failed;
}
