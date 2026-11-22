#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int
list_path(const char *path)
{
	struct stat status;
	if (stat(path, &status) < 0) {
		perror(path);
		return 1;
	}
	if (!S_ISDIR(status.st_mode))
		return puts(path) == EOF;
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
		if (puts(entry->d_name) == EOF) {
			failed = 1;
			break;
		}
	}
	if (closedir(directory) < 0) {
		perror(path);
		failed = 1;
	}
	return failed;
}

int
main(int argc, char **argv)
{
	if (argc == 1)
		return list_path(".");
	int failed = 0;
	for (int i = 1; i < argc; i++)
		if (list_path(argv[i]))
			failed = 1;
	return failed;
}
