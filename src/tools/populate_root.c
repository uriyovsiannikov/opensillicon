#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/silliconfs.h"
#include "kernel/vfs.h"

struct image_context {
	FILE *file;
};

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *pointer) { free(pointer); return true; }
void klog(const char *message) { fputs(message, stderr); }
void klog_u64(uint64_t value) { fprintf(stderr, "%llu", (unsigned long long)value); }
_Noreturn void kernel_halt(void) { abort(); }
bool serial_write(const char *buffer, size_t length)
{ return fwrite(buffer, 1, length, stderr) == length; }
int tty_read(void *buffer, size_t length, size_t *read_length)
{ (void)buffer; (void)length; *read_length = 0; return -5; }
bool tty_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; return true; }
void process_wake_channel(void *channel) { (void)channel; }
bool terminal_input_available(void) { return false; }

static int
image_read(void *argument, uint32_t block, void *buffer)
{
	struct image_context *context = argument;
	if (fseek(context->file, (long)block * SILLICONFS_BLOCK_SIZE, SEEK_SET) != 0 ||
	    fread(buffer, 1, SILLICONFS_BLOCK_SIZE, context->file) !=
	    SILLICONFS_BLOCK_SIZE)
		return -EIO;
	return 0;
}

static int
image_write(void *argument, uint32_t block, const void *buffer)
{
	struct image_context *context = argument;
	if (fseek(context->file, (long)block * SILLICONFS_BLOCK_SIZE, SEEK_SET) != 0 ||
	    fwrite(buffer, 1, SILLICONFS_BLOCK_SIZE, context->file) !=
	    SILLICONFS_BLOCK_SIZE || fflush(context->file) != 0)
		return -EIO;
	return 0;
}

static int
add_directory(const char *path, uint32_t mode)
{
	struct vnode *node = NULL;
	int error = vfs_create_path(path, VNODE_DIRECTORY, mode, &node);
	if (error == 0)
		vnode_put(node);
	return error;
}

static int
add_file(const char *path, const char *host_path, uint32_t mode)
{
	FILE *source = fopen(host_path, "rb");
	struct vnode *node = NULL;
	unsigned char buffer[2048];
	uint64_t offset = 0;
	int error;

	if (source == NULL) {
		int open_error = errno;
		fprintf(stderr, "could not open %s for %s: %s\n",
		    host_path, path, strerror(open_error));
		return -open_error;
	}
	error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error != 0) {
		fprintf(stderr, "could not create %s: error %d\n", path, error);
		fclose(source);
		return error;
	}
	for (;;) {
		size_t amount = fread(buffer, 1, sizeof(buffer), source);
		size_t written = 0;
		if (amount != 0) {
			error = node->operations->write(node, offset, buffer, amount,
			    &written);
			if (error != 0 || written != amount) {
				error = error != 0 ? error : -EIO;
				fprintf(stderr, "could not populate %s from %s at offset "
				    "%llu: error %d (wrote %zu of %zu bytes)\n",
				    path, host_path, (unsigned long long)offset,
				    error, written, amount);
				break;
			}
			offset += written;
		}
		if (amount < sizeof(buffer)) {
			if (ferror(source)) error = -EIO;
			break;
		}
	}
	vnode_put(node);
	fclose(source);
	return error;
}


static int
add_data(const char *path, const void *data, size_t length, uint32_t mode)
{
	struct vnode *node = NULL;
	size_t written = 0;
	int error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error == 0) {
		error = node->operations->write(node, 0, data, length, &written);
		if (error == 0 && written != length)
			error = -EIO;
		vnode_put(node);
	}
	return error;
}

int
main(int argc, char **argv)
{
	struct image_context context;
	struct silliconfs_device device;
	struct vnode *root = NULL;
	FILE *image;
	int error;
	int coreutils_start = 0;
	int coreutils_count = 0;
	const char *libos_path = NULL;
	const char *nano_path = NULL;
	const char *htop_path = NULL;
	const char *pkg_ca_path = NULL;
	const char *svctl_path = NULL;
	const char *scinstall_path = NULL;
	const char *boot_files[3] = {NULL, NULL, NULL};
	const char *service_paths[32];
	size_t service_count = 0;
	const char *module_paths[32];
	size_t module_count = 0;
	int arg;

	if (argc < 4) {
		fprintf(stderr,
		    "usage: %s image init-elf shell-elf [--nano elf] [--htop elf] [--pkg-ca file.pem] [--svctl elf] [--scinstall elf] [--service file.conf] [--boot-files loader kernel initramfs] [--module file.ko] [--coreutils coreutils...]\n",
		    argv[0]);
		return 2;
	}
	for (arg = 4; arg < argc; arg++) {
		if (strcmp(argv[arg], "--libos") == 0 && arg + 1 < argc) {
			if (libos_path != NULL) return 2;
			libos_path = argv[++arg];
		} else if (strcmp(argv[arg], "--nano") == 0 && arg + 1 < argc) {
			if (nano_path != NULL) return 2;
			nano_path = argv[++arg];
		} else if (strcmp(argv[arg], "--htop") == 0 && arg + 1 < argc) {
			if (htop_path != NULL) return 2;
			htop_path = argv[++arg];
		} else if (strcmp(argv[arg], "--pkg-ca") == 0 && arg + 1 < argc) {
			if (pkg_ca_path != NULL) return 2;
			pkg_ca_path = argv[++arg];
		} else if (strcmp(argv[arg], "--boot-files") == 0 &&
		    arg + 3 < argc) {
			if (boot_files[0] != NULL)
				return 2;
			for (unsigned i = 0; i < 3; i++)
				boot_files[i] = argv[++arg];
		} else if (strcmp(argv[arg], "--module") == 0 &&
		    arg + 1 < argc) {
			if (module_count == 32) return 2;
			module_paths[module_count++] = argv[++arg];
		} else if (strcmp(argv[arg], "--svctl") == 0 &&
		    arg + 1 < argc) {
			if (svctl_path != NULL) return 2;
			svctl_path = argv[++arg];
		} else if (strcmp(argv[arg], "--service") == 0 &&
		    arg + 1 < argc) {
			if (service_count == sizeof(service_paths) / sizeof(service_paths[0]))
				return 2;
			service_paths[service_count++] = argv[++arg];
		} else if (strcmp(argv[arg], "--scinstall") == 0 && arg + 1 < argc) {
			if (scinstall_path != NULL) return 2;
			scinstall_path = argv[++arg];
		} else if (strcmp(argv[arg], "--coreutils") == 0) {
			if (coreutils_start != 0) return 2;
			coreutils_start = arg + 1;
			break;
		} else {
			fprintf(stderr, "unexpected argument: %s\n", argv[arg]);
			return 2;
		}
	}
	if (coreutils_start != 0)
		coreutils_count = argc - coreutils_start;
	image = fopen(argv[1], "r+b");
	if (image == NULL) {
		perror(argv[1]);
		return 1;
	}
	context.file = image;
	device = (struct silliconfs_device){
		.context = &context,
		.block_count = 0,
		.read_block = image_read,
		.write_block = image_write
	};
	if (fseek(image, 0, SEEK_END) != 0) return 1;
	device.block_count = (uint32_t)(ftell(image) / SILLICONFS_BLOCK_SIZE);
	if (!vfs_init() || silliconfs_mount(&device, &root) != 0 ||
	    vfs_mount("/", root) != 0) {
		fprintf(stderr, "could not mount formatted SilliconFS image\n");
		fclose(image);
		return 1;
	}
	vnode_put(root);
	if ((error = add_directory("/bin", 0755)) != 0 ||
	    (error = add_directory("/usr", 0755)) != 0 ||
	    (error = add_directory("/usr/bin", 0755)) != 0 ||
	    (error = add_directory("/sbin", 0755)) != 0 ||
	    (error = add_directory("/etc", 0755)) != 0 ||
	    (error = add_directory("/var", 0755)) != 0 ||
	    (error = add_directory("/var/db", 0755)) != 0 ||
	    (error = add_directory("/var/db/pkg", 0755)) != 0 ||
	    (error = add_directory("/tmp", 01777)) != 0 ||
	    (error = add_file("/sbin/init", argv[2], 0755)) != 0 ||
	    (error = add_file("/bin/sh", argv[3], 0755)) != 0) {
		fprintf(stderr, "SilliconFS population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (svctl_path != NULL &&
	    (error = add_file("/bin/svctl", svctl_path, 0755)) != 0) {
		fprintf(stderr, "svctl installation failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (scinstall_path != NULL &&
	    (error = add_file("/bin/scinstall", scinstall_path, 0755)) != 0) {
		fprintf(stderr, "scinstall installation failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (svctl_path != NULL || service_count != 0) {
		error = add_directory("/etc/service.d", 0755);
		for (size_t i = 0; error == 0 && i < service_count; i++) {
			const char *filename = strrchr(service_paths[i], '/');
			char destination[sizeof("/etc/service.d/") + 36];

			filename = filename == NULL ? service_paths[i] : filename + 1;
			int length = snprintf(destination, sizeof(destination),
			    "/etc/service.d/%s", filename);
			if (length < 0 || (size_t)length >= sizeof(destination))
				error = -EINVAL;
			else
				error = add_file(destination, service_paths[i], 0644);
		}
		if (error != 0) {
			fprintf(stderr, "service configuration installation failed: %d\n", error);
			fclose(image);
			return 1;
		}
	}
	if (module_count != 0 || boot_files[0] != NULL) {
		if ((error = add_directory("/boot", 0755)) != 0) {
			fprintf(stderr,
			    "boot directory population failed: %d\n", error);
			fclose(image);
			return 1;
		}
	}
	if (module_count != 0) {
		if ((error = add_directory("/boot/modules", 0755)) != 0) {
			fprintf(stderr, "module directory population failed: %d\n", error);
			fclose(image);
			return 1;
		}
		for (size_t i = 0; i < module_count; i++) {
			char destination[256];
			const char *name = strrchr(module_paths[i], '/');
			name = name == NULL ? module_paths[i] : name + 1;
			int length = snprintf(destination, sizeof(destination),
			    "/boot/modules/%s", name);
			if (*name == '\0' || strcmp(name, ".") == 0 ||
			    strcmp(name, "..") == 0 || length < 0 ||
			    (size_t)length >= sizeof(destination) ||
			    (error = add_file(destination, module_paths[i], 0644)) != 0) {
				fprintf(stderr, "module population failed: %s\n", module_paths[i]);
				fclose(image);
				return 1;
			}
		}
	}
	if (boot_files[0] != NULL) {
		static const char *destinations[] = {
		    "/boot/BOOTX64.EFI", "/boot/kernel.elf", "/boot/init.cpi"};
		for (unsigned i = 0; i < 3; i++) {
			if ((error = add_file(
				 destinations[i], boot_files[i], 0644)) != 0) {
				fprintf(stderr,
				    "boot payload population failed: %d\n",
				    error);
				fclose(image);
				return 1;
			}
		}
	}
	if (libos_path != NULL &&
	    (error = add_file("/bin/libos-test", libos_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS libos fixture population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if ((error = add_directory("/etc/ssl", 0755)) != 0 ||
	    (error = add_file("/etc/ssl/cert.pem",
	    "src/user/thirdparty/mbedtls/opensillicon/ca-certificates.crt", 0644)) != 0 ||
	    (pkg_ca_path != NULL && (error = add_file("/etc/ssl/pkg-ca.pem",
	    pkg_ca_path, 0644)) != 0)) {
		fprintf(stderr, "SilliconFS TLS trust population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (nano_path != NULL &&
	    (error = add_file("/bin/nano", nano_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS nano population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (htop_path != NULL &&
	    (error = add_file("/bin/htop", htop_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS htop population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (coreutils_start != 0) {
		static const char *names[] = {"ls", "cat", "echo", "pwd",
		    "true", "false", "clear", "uname", "fetch", "head", "tail",
		    "wc", "seq", "mkdir", "rm", "touch", "stat", "basename",
		    "dirname", "kill", "sleep", "which", "ps",
		    "ping", "poweroff", "reboot", "host", "ifconfig", "kldstat",
		    "kldload", "kldunload", "pkg", "sysupgrade", "pciconf", "netconf",
		    "usbconf", "cpuconf", "blkconf", "newfs_silliconfs", "gpart",
		    "installboot", "mount", "umount", "sync", "doas"};
		int index;
		if (coreutils_count !=
		    (int)(sizeof(names) / sizeof(names[0]))) {
			fprintf(stderr, "expected %zu coreutils ELF files, got %d\n",
			    sizeof(names) / sizeof(names[0]), coreutils_count);
			fclose(image);
			return 1;
		}
		for (index = 0; index < coreutils_count; index++) {
			const char *name = names[index];
			char destination[32];
			bool doas = strcmp(name, "doas") == 0;
			snprintf(destination, sizeof(destination), "%s/%s",
			    doas ? "/usr/bin" : "/bin", name);
			if ((error = add_file(destination,
			    argv[coreutils_start + index], doas ? 04755 : 0755)) != 0) {
				fprintf(stderr, "SilliconFS coreutils population failed: %d\n", error);
				fclose(image);
				return 1;
			}
		}
	}
	{
		static const char passwd[] =
		    "root:*:0:0:OpenSillicon root:/:/bin/sh\n"
		    "user:*:1000:1000:OpenSillicon user:/tmp:/bin/sh\n";
		static const char group[] = "root:*:0:\nuser:*:1000:\n";
		static const char pkg_conf[] = "https://packages.chipset.bond\n";
		static const char doas_conf[] =
		    "permit nopass 1000 as root cmd /bin/pkg\n"
		    "permit nopass 1000 as root cmd /bin/sysupgrade\n";
		static const char resolv_conf[] = "nameserver 10.15.3.3\n";
		if (add_data("/etc/passwd", passwd, sizeof(passwd) - 1, 0644) != 0 ||
		    add_data("/etc/group", group, sizeof(group) - 1, 0644) != 0 ||
		    add_data("/etc/pkg.conf", pkg_conf, sizeof(pkg_conf) - 1, 0644) != 0 ||
		    add_data("/etc/doas.conf", doas_conf, sizeof(doas_conf) - 1, 0644) != 0 ||
		    add_data("/etc/resolv.conf", resolv_conf,
		    sizeof(resolv_conf) - 1, 0644) != 0) {
			fclose(image);
			return 1;
		}
	}
	if (fclose(image) != 0) return 1;
	return 0;
}
