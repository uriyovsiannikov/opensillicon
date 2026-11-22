#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/uuid.h"
#include "../storage/storage.h"
#include "../storage/install_copy.h"
#include "ui.h"

#define SATA_DISK_LIMIT 7
#define TARGET "/mnt"
static FILE *journal;
static const char *stage = "preflight";
static void
note(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	if (ui_active()) {
		char text[512];
		vsnprintf(text, sizeof(text), format, args);
		ui_note(text);
	} else {
		vprintf(format, args);
		fflush(stdout);
	}
	va_end(args);
	if (journal) {
		va_start(args, format);
		vfprintf(journal, format, args);
		va_end(args);
		fflush(journal);
	}
}
static int
line(char *out, size_t size)
{
	if (!fgets(out, (int)size, stdin))
		return -1;
	size_t n = strlen(out);
	if (!n || out[n - 1] != '\n') {
		int c;
		while ((c = fgetc(stdin)) != '\n' && c != EOF) {
		}
		return -1;
	}
	while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		out[--n] = 0;
	return 0;
}
static int
disk_path(const char *text, char out[32])
{
	const char *p = text;
	if (!strncmp(p, "/dev/", 5))
		p += 5;
	if (strncmp(p, "ada", 3))
		return -EINVAL;
	const char *number = p + 3;
	if (!*number)
		return -EINVAL;
	for (const char *q = number; *q; q++)
		if (*q < '0' || *q > '9')
			return -EINVAL;
	if (strlen(p) + 5 >= 32)
		return -ENAMETOOLONG;
	strcpy(out, "/dev/");
	strcat(out, p);
	return 0;
}
static int
live_root(void)
{
	int fd = open("/dev/ram0", O_RDONLY);
	if (fd < 0)
		return 0;
	struct os_disk_partinfo info;
	int live = ioctl(fd, DIOCGPARTINFO, &info) == 0 &&
	    (info.flags & OS_DISK_MOUNTED);
	close(fd);
	return live;
}
static int
directory(const char *path, unsigned mode)
{
	if (mkdir(path, mode) == 0)
		return 0;
	if (errno != EEXIST)
		return -errno;
	struct stat st;
	if (stat(path, &st) != 0)
		return -errno;
	return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}
static size_t
read_disks(struct disk_choice disks[SATA_DISK_LIMIT])
{
	size_t count = 0;
	for (unsigned i = 0; i < SATA_DISK_LIMIT; i++) {
		struct disk_choice *disk = &disks[count];
		snprintf(disk->path, sizeof(disk->path), "/dev/ada%u", i);
		struct storage_device device;
		if (storage_open(&device, disk->path, 0))
			continue;
		struct os_disk_partinfo info;
		if (ioctl(device.fd, DIOCGPARTINFO, &info) == 0) {
			(void)ioctl(device.fd, DIOCGMODEL, disk->model);
			(void)ioctl(device.fd, DIOCGIDENT, disk->serial);
			disk->mib = device.io.bytes / (1024 * 1024);
			disk->writable = (info.flags & OS_DISK_RAW_WRITE_ALLOWED) &&
			    !(info.flags & OS_DISK_PARTITION);
			count++;
		}
		storage_close(&device);
	}
	return count;
}

static int
choose_disk(char path[32])
{
	struct disk_choice disks[SATA_DISK_LIMIT] = {0};
	size_t count = read_disks(disks);
	if (ui_active()) {
		int selected = ui_choose_disk(disks, count);
		if (selected == UI_DISK_CANCELLED)
			return 1;
		if (selected < 0)
			return selected;
		strcpy(path, disks[selected].path);
		return 0;
	}
	puts("Available SATA disks:");
	for (size_t i = 0; i < count; i++)
		printf("  %s  %llu MiB  %s  serial=%s%s\n", disks[i].path,
		    disks[i].mib, disks[i].model, disks[i].serial,
		    disks[i].writable ? "" : "  [IN USE]");
	fputs("Target disk: ", stdout);
	fflush(stdout);
	char answer[128];
	if (line(answer, sizeof(answer)) || !strcmp(answer, "q"))
		return 1;
	return disk_path(answer, path);
}

static int
boot_sources(void)
{
	static const char *paths[] = {
	    "/boot/BOOTX64.EFI", "/boot/kernel.elf", "/boot/init.cpi"};
	for (unsigned i = 0; i < 3; i++) {
		int fd = open(paths[i], O_RDONLY);
		if (fd < 0)
			return -errno;
		unsigned char bytes[8];
		ssize_t n = read(fd, bytes, sizeof(bytes));
		close(fd);
		if (n != sizeof(bytes) || (i == 0 && memcmp(bytes, "MZ", 2)) ||
		    (i == 1 &&
			memcmp(bytes,
			    "\x7f"
			    "ELF",
			    4)) ||
		    (i == 2 && memcmp(bytes, "070701", 6) &&
			memcmp(bytes, "070702", 6)))
			return -EINVAL;
	}
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -errno;
	unsigned char random[48];
	ssize_t n = read(fd, random, sizeof(random));
	close(fd);
	return n == sizeof(random) ? 0 : -EAGAIN;
}
static int
run(char **argv)
{
	if (journal)
		fflush(journal);
	pid_t pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		if (journal &&
		    (dup2(fileno(journal), 1) < 0 ||
			dup2(fileno(journal), 2) < 0))
			_exit(126);
		execv(argv[0], argv);
		_exit(127);
	}
	int status = 0;
	if (waitpid(pid, &status, 0) != pid)
		return -errno;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		note("%s failed: child status %d\n", stage, status);
		return -EIO;
	}
	return 0;
}
static int
progress(void *context, const char *path, size_t done, size_t total)
{
	const char *action = context;
	if (journal &&
	    (fprintf(journal, "%s %s\n", action, path) < 0 ||
		fflush(journal) != 0))
		return -EIO;
	if (ui_active()) {
		ui_progress(action, done, total);
	} else if (done % 20 == 0 || done == total) {
		printf("%s: %u/%u entries\n", action, (unsigned)done,
		    (unsigned)total);
		fflush(stdout);
	}
	return 0;
}
static int
save_journal(void)
{
	int error = directory(TARGET "/var", 0755);
	if (!error)
		error = directory(TARGET "/var/log", 0755);
	if (error)
		return error;
	if (fflush(journal) != 0)
		return -errno;
	int source = open("/tmp/scinstall.log", O_RDONLY);
	if (source < 0)
		return -errno;
	int output = open(
	    TARGET "/var/log/install.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (output < 0) {
		error = -errno;
		close(source);
		return error;
	}
	unsigned char buffer[4096];
	for (;;) {
		ssize_t n = read(source, buffer, sizeof(buffer));
		if (n < 0) {
			error = -errno;
			break;
		}
		if (!n)
			break;
		for (ssize_t done = 0; done < n;) {
			ssize_t w =
			    write(output, buffer + done, (size_t)(n - done));
			if (w <= 0) {
				error = w < 0 ? -errno : -EIO;
				break;
			}
			done += w;
		}
		if (error)
			break;
	}
	if (!error && fsync(output) != 0)
		error = -errno;
	if (close(output) != 0 && !error)
		error = -errno;
	if (close(source) != 0 && !error)
		error = -errno;
	return error;
}
int
main(int argc, char **argv)
{
	int preview = argc == 3 && !strcmp(argv[1], "-N");
	bool text = argc == 2 && !strcmp(argv[1], "--text");
	if (argc != 1 && !preview && !text) {
		fputs("usage: scinstall [--text | -N /dev/ada0]\n", stderr);
		return 2;
	}
	if (geteuid() != 0) {
		fputs(
		    "scinstall: select OpenSillicon Installer in the Live boot menu (root required).\n",
		    stderr);
		return 1;
	}
	if (!live_root()) {
		fputs(
		    "scinstall: installation is allowed only from the Live RAM root.\n",
		    stderr);
		return 1;
	}
	journal = fopen("/tmp/scinstall.log", "w");
	if (!journal) {
		perror("scinstall log");
		return 1;
	}
	if (!preview && !text)
		(void)ui_open();
	note("OpenSillicon Installer - UEFI/GPT, whole SATA disk\n");
	note("All data on the selected disk will be replaced.\n");
	char answer[128], path[32];
	int selection = preview ? disk_path(argv[2], path) : choose_disk(path);
	if (selection) {
		bool terminal_error = ui_active() && selection < 0;
		ui_close();
		if (terminal_error)
			note("Cannot read installer keyboard: %s (errno %d). No disk writes.\n",
			    strerror(-selection), -selection);
		else if (selection < 0)
			note("Invalid disk name; no disk writes.\n");
		else
			note("Installation cancelled; no disk writes.\n");
		fclose(journal);
		return terminal_error ? 1 : selection < 0 ? 2 : 0;
	}
	ui_reset();
	struct storage_device d = {.fd = -1};
	struct os_disk_partinfo original;
	char model[41] = {0}, ident[256] = {0};
	struct gpt_layout plan;
	struct sfs_layout fsplan;
	struct install_tree tree = {0};
	int mounted = 0, error = storage_open(&d, path, 0);
	if (!error && ioctl(d.fd, DIOCGPARTINFO, &original) != 0)
		error = -errno;
	if (!error &&
	    ((original.flags & OS_DISK_PARTITION) ||
		!(original.flags & OS_DISK_RAW_WRITE_ALLOWED)))
		error = -EBUSY;
	if (!error &&
	    (ioctl(d.fd, DIOCGMODEL, model) != 0 ||
		ioctl(d.fd, DIOCGIDENT, ident) != 0))
		error = -errno;
	if (!error)
		error = gpt_layout_plan(&d.io, 512, &plan);
	unsigned char probe[4096];
	if (!error)
		error = d.io.read(d.io.context, 0, probe, d.io.sector);
	if (!error)
		error = d.io.read(
		    d.io.context, d.io.bytes - d.io.sector, probe, d.io.sector);
	if (!error && ioctl(d.fd, DIOCGFLUSH) != 0)
		error = -errno;
	if (!error)
		error = boot_sources();
	if (!error)
		error = directory(TARGET, 0755);
	if (!error) {
		note("Scanning and verifying Live files...\n");
		error = install_tree_scan(&tree, "/");
	}
	if (!error)
		error = install_tree_target_check(&tree, TARGET);
	uint32_t inodes = 4096;
	while (inodes < tree.count + 16)
		inodes *= 2;
	if (!error) {
		struct storage_io target = d.io;
		target.bytes = plan.root_sectors * d.io.sector;
		error = sfs_plan(&target, inodes, &fsplan);
		if (!error &&
		    tree.required_blocks > fsplan.blocks - fsplan.data_start)
			error = -ENOSPC;
	}
	if (error)
		goto failed;
	note("Target: %s | %s | serial=%s | %llu bytes\n", path, model, ident,
	    (unsigned long long)d.io.bytes);
	note("Plan: GPT; p1 EFI 512 MiB; p2 SilliconFS %llu MiB, %u inodes\n",
	    (unsigned long long)(plan.root_sectors * d.io.sector /
		(1024 * 1024)),
	    inodes);
	note(
	    "Source: %u entries, %llu file bytes\n",
	    (unsigned)tree.count, (unsigned long long)tree.file_bytes);
	if (preview) {
		note("Preview complete; no partition or filesystem writes.\n");
		storage_close(&d);
		install_tree_free(&tree);
		fclose(journal);
		return 0;
	}
	char confirmation[64];
	snprintf(confirmation, sizeof(confirmation), "ERASE %s", path);
	int confirmed;
	if (ui_active()) {
		confirmed = ui_confirm(confirmation);
	} else {
		note("To erase this disk, type: %s\nConfirmation: ", confirmation);
		confirmed = !line(answer, sizeof(answer)) && !strcmp(answer, confirmation);
	}
	if (confirmed <= 0) {
		ui_close();
		if (confirmed < 0)
			note("Cannot read installer keyboard: %s (errno %d). No disk writes.\n",
			    strerror(-confirmed), -confirmed);
		else
			note("Installation cancelled; no disk writes.\n");
		storage_close(&d);
		install_tree_free(&tree);
		fclose(journal);
		return confirmed < 0 ? 1 : 0;
	}
	note("Rechecking source and target...\n");
	error = install_tree_recheck(&tree);
	if (error)
		goto failed;
	/* Revalidate identity and in-use state immediately before launching
	 * writers. */
	char current_ident[256];
	struct os_disk_partinfo current;
	if (ioctl(d.fd, DIOCGPARTINFO, &current) != 0 ||
	    ioctl(d.fd, DIOCGIDENT, current_ident) != 0)
		error = -errno;
	if (!error &&
	    (memcmp(&current, &original, sizeof(current)) ||
		strcmp(current_ident, ident)))
		error = -EAGAIN;
	if (error)
		goto failed;
	storage_close(&d);
	stage = "GPT creation";
	note("[1/7] %s\n", stage);
	char *gpart[] = {"/bin/gpart", "bootlayout", "-F", path, NULL};
	error = run(gpart);
	if (error)
		goto failed;
	char root_path[40], esp_path[40], inode_text[16], uuid[37];
	snprintf(root_path, sizeof(root_path), "%sp2", path);
	snprintf(esp_path, sizeof(esp_path), "%sp1", path);
	int fd = open(root_path, O_RDONLY);
	if (fd < 0) {
		error = -errno;
		goto failed;
	}
	struct os_disk_partinfo rootinfo;
	error = ioctl(fd, DIOCGPARTINFO, &rootinfo) == 0 ? 0 : -errno;
	close(fd);
	if (error)
		goto failed;
	uuid_format(rootinfo.uuid, uuid);
	note("Root PARTUUID=%s\n", uuid);
	snprintf(inode_text, sizeof(inode_text), "%u", inodes);
	stage = "root filesystem creation";
	note("[2/7] %s\n", stage);
	char *newfs[] = {
	    "/bin/newfs_silliconfs", "-F", "-i", inode_text, root_path, NULL};
	error = run(newfs);
	if (error)
		goto failed;

	if (mount(root_path, TARGET, "silliconfs", 0, NULL) != 0) {
		error = -errno;
		goto failed;
	}
	mounted = 1;
	stage = "copying system files";
	note("[3/7] %s\n", stage);
	error = install_tree_copy(&tree, TARGET, progress, "Copied");
	if (error)
		goto failed;
	stage = "verifying written files";
	ui_clear_progress();
	note("[4/7] %s\n", stage);
	error = install_tree_verify(&tree, TARGET, progress, "Verified");
	if (error)
		goto failed;
	stage = "UEFI boot installation";
	note("[5/7] %s\n", stage);
	char *boot[] = {"/bin/installboot", "-F", "-r", uuid, esp_path, NULL};
	error = run(boot);
	if (error)
		goto failed;
	stage = "installation journal and filesystem flush";
	note("[6/7] %s\n", stage);
	error = save_journal();
	if (error)
		goto failed;
	if (umount2(TARGET, 0) != 0) {
		error = -errno;
		goto failed;
	}
	mounted = 0;
	stage = "final disk flush";
	note("[7/7] %s\n", stage);
	error = storage_open(&d, path, 0);
	if (!error && ioctl(d.fd, DIOCGFLUSH) != 0)
		error = -errno;
	if (error)
		goto failed;
	storage_close(&d);
	install_tree_free(&tree);
	note(
	    "INSTALLATION COMPLETE: %s\nRemove the Live USB before booting the installed system.\n",
	    path);
	bool reboot_now;
	if (ui_active()) {
		reboot_now = ui_reboot();
		ui_close();
		note("Installation complete on %s.\n", path);
	} else {
		fputs("Reboot now? [y/N]: ", stdout);
		fflush(stdout);
		reboot_now = !line(answer, sizeof(answer)) &&
		    (!strcmp(answer, "y") || !strcmp(answer, "Y"));
	}
	if (reboot_now) {
		stage = "reboot";
		char *reboot[] = {"/bin/reboot", NULL};
		(void)run(reboot);
	}
	fclose(journal);
	return 0;
failed:
	note(
	    "INSTALLATION FAILED at %s (errno %d), path=%s. See /tmp/scinstall.log\n",
	    stage, -error, tree.failed_path);
	if (mounted && umount2(TARGET, 0) != 0)
		note(
		    "Target remains mounted at %s; close files and retry umount (errno %d).\n",
		    TARGET, errno);
	if (ui_active()) {
		ui_wait();
		ui_close();
	}
	if (d.fd >= 0)
		storage_close(&d);
	install_tree_free(&tree);
	fclose(journal);
	return 1;
}
