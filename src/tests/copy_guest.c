#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../user/storage/install_copy.h"
static void
dir(const char *p, unsigned mode)
{
	assert(mkdir(p, mode) == 0 && chmod(p, mode) == 0);
}
static void
file(const char *p, unsigned size, unsigned mode, unsigned uid)
{
	int fd = open(p, O_RDWR | O_CREAT | O_EXCL, 0600);
	assert(fd >= 0);
	unsigned char data[4096];
	for (unsigned i = 0; i < sizeof(data); i++)
		data[i] = (unsigned char)i;
	for (unsigned done = 0; done < size;) {
		unsigned n =
		    size - done > sizeof(data) ? sizeof(data) : size - done;
		assert(write(fd, data, n) == (int)n);
		done += n;
	}
	assert(fchown(fd, uid, uid) == 0 && fchmod(fd, mode) == 0 &&
	    close(fd) == 0);
}
int
main(void)
{
	dir("/tmp/source", 0755);
	dir("/tmp/source/bin", 0755);
	dir("/tmp/source/etc", 0755);
	dir("/tmp/source/tmp", 01777);
	dir("/tmp/source/dev", 0755);
	dir("/tmp/source/var", 0755);
	dir("/tmp/source/var/run", 0755);
	file("/tmp/source/bin/program", 13 * 4096 + 11, 04755, 0);
	file("/tmp/source/etc/config", 12000, 0640, 1000);
	file("/tmp/source/etc/empty", 0, 0600, 1000);
	file("/tmp/source/tmp/img1.jpg", 100, 0644, 0);
	file("/tmp/source/tmp/ignored", 100, 0600, 0);
	file("/tmp/source/dev/ignored", 100, 0600, 0);
	file("/tmp/source/var/run/ignored", 100, 0600, 0);
	struct install_tree tree;
	int scan = install_tree_scan(&tree, "/tmp/source");
	if (scan)
		printf("copy scan error %d at %s\n", scan, tree.failed_path);
	assert(scan == 0);
	int recheck = install_tree_recheck(&tree);
	if (recheck)
		printf(
		    "copy recheck error %d at %s\n", recheck, tree.failed_path);
	assert(recheck == 0);
	dir("/mnt", 0700);
	struct silliconfs_args args = {"/dev/ada0"};
	assert(mount("silliconfs", "/mnt", 0, &args) == 0);
	assert(install_tree_copy(&tree, "/mnt", NULL, NULL) == 0);
	install_tree_free(&tree);
	assert(unmount("/mnt", 0) == 0 &&
	    mount("silliconfs", "/mnt", 0, &args) == 0);
	int fd = open("/mnt/etc/config", O_RDONLY);
	assert(fd >= 0);
	struct stat st;
	assert(fstat(fd, &st) == 0 && st.st_size == 12000 &&
	    st.st_uid == 1000 && st.st_gid == 1000 &&
	    (st.st_mode & 07777) == 0640);
	unsigned char data[4096];
	assert(read(fd, data, sizeof(data)) == sizeof(data));
	for (unsigned i = 0; i < sizeof(data); i++)
		assert(data[i] == (unsigned char)i);
	close(fd);
	assert(stat("/mnt/bin/program", &st) == 0 &&
	    st.st_size == 13 * 4096 + 11 && (st.st_mode & 07777) == 04755);
	assert(stat("/mnt/etc/empty", &st) == 0 && st.st_size == 0);
	assert(stat("/mnt/tmp", &st) == 0 && (st.st_mode & 07777) == 01777);
	assert(access("/mnt/tmp/img1.jpg", F_OK) == 0 &&
	    access("/mnt/tmp/ignored", F_OK) != 0 &&
	    access("/mnt/dev/ignored", F_OK) != 0 &&
	    access("/mnt/var/run/ignored", F_OK) != 0);
	assert(unmount("/mnt", 0) == 0);
	puts(
	    "COPY GUEST PASS: contents, indirect blocks, UID/GID, setuid/sticky modes, runtime exclusions");
	fflush(stdout);
	for (;;)
		sleep(1);
}
