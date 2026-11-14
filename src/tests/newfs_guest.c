#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
static int
run(char **argv)
{
	pid_t pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	int status = 0;
	assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
	return WEXITSTATUS(status);
}
int
main(void)
{
	uint8_t bytes[4096];
	uint32_t sector = 0;
	int64_t size = 0;
	int fd = open("/dev/ada0", O_RDONLY);
	assert(fd >= 0);
	assert(ioctl(fd, DIOCGSECTORSIZE, &sector) == 0 && sector == 512);
	assert(
	    ioctl(fd, DIOCGMEDIASIZE, &size) == 0 && size == 16 * 1024 * 1024);
	assert(read(fd, bytes, 512) == 512 && bytes[0] == 0xcc);
	char *preview[] = {
	    "/bin/newfs_silliconfs", "-N", "-i", "128", "/dev/ada0", NULL};
	assert(run(preview) == 0);
	assert(lseek(fd, 0, SEEK_SET) == 0 && read(fd, bytes, 512) == 512 &&
	    bytes[0] == 0xcc);
	char *format[] = {
	    "/bin/newfs_silliconfs", "-F", "-i", "128", "/dev/ada0", NULL};
	assert(run(format) == 0);
	assert(lseek(fd, 0, SEEK_SET) == 0 && read(fd, bytes, 512) == 512 &&
	    memcmp(bytes, "SILFS1\0\0", 8) == 0);
	assert(ioctl(fd, DIOCGFLUSH) == 0);
	close(fd);
	char *mounted[] = {"/bin/newfs_silliconfs", "-F", "/dev/ram0", NULL};
	assert(run(mounted) != 0);
	fd = open("/dev/ram0", O_RDONLY);
	assert(fd >= 0);
	assert(read(fd, bytes, 4096) == 4096 &&
	    memcmp(bytes, "SILFS1\0\0", 8) == 0);
	close(fd);
	puts(
	    "NEWFS GUEST PASS: preview, geometry ioctls, formatting, flush, mounted root protection");
	fflush(stdout);
	for (;;)
		sleep(1);
}
