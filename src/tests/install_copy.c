#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../user/storage/install_copy.h"
static int
progress(void *context, const char *path, size_t done, size_t total)
{
	size_t *count = context;
	assert(path[0] == '/' && done <= total);
	(*count)++;
	return 0;
}
int
main(int argc, char **argv)
{
	assert(argc == 4);
	struct install_tree tree;
	if (!strcmp(argv[3], "reject")) {
		assert(install_tree_scan(&tree, argv[1]) == -EINVAL);
		install_tree_free(&tree);
		return 0;
	}
	assert(install_tree_scan(&tree, argv[1]) == 0 && tree.count > 5 &&
	    tree.file_bytes > 4096);
	assert(install_tree_target_check(&tree, argv[1]) == -EINVAL);
	assert(install_tree_target_check(&tree, argv[2]) == 0);
	assert(install_tree_recheck(&tree) == 0);
	size_t count = 0;
	assert(install_tree_copy(&tree, argv[2], progress, &count) == 0 &&
	    count == tree.count);
	if (!strcmp(argv[3], "change")) {
		char path[512];
		snprintf(path, sizeof(path), "%s/bin/program", argv[1]);
		int fd = open(path, O_WRONLY);
		assert(
		    fd >= 0 && write(fd, "CHANGED", 7) == 7 && close(fd) == 0);
		assert(install_tree_recheck(&tree) == -EAGAIN);
	}
	install_tree_free(&tree);
}
