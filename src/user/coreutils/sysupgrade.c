#include <errno.h>
#include <fcntl.h>
#include <mbedtls/pk.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "common/uuid.h"
#include "pkg/format.h"
#include "pkg/http.h"
#include "pkg/sha256.h"
#include "storage/storage.h"

#define MANIFEST_MAX 1024U
#define SIGNATURE_BYTES 256U
static const char public_key[] =
"-----BEGIN PUBLIC KEY-----\n"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsMuVWYrMKffuX890GYxn\n"
"yobVIbNakHlMq79Iic/Lx/oPBaO3Hd6uTZXN7OLYDVInveMd6wC7AgQmMheJLdii\n"
"GGMeQXLWT7ztgyc6J+40d4htnx7xWOVATpg4OcAJc2nO307gkvF8gubs89aAxjFM\n"
"TaUQRGykUg18EY45ldvEH5gGXulHuIoADLgzm9SHyqyDQxWIeHFKVWVfDbQB7JFi\n"
"juabVArewyCCwI/66yLdogvYmapgbJmfx4h3pG0CChNcLCkW/lhrJkM0u9P8v1Hy\n"
"EAWgjLbaLxTMTAt6PdAz94twQrnoWDGwzTkL8n3OD8WjWR4O1yaJ46wlX2tRVmd5\n"
"wQIDAQAB\n-----END PUBLIC KEY-----\n";
struct release {
	const char *version;
	const uint8_t *kernel, *initramfs;
	size_t kernel_size, init_size;
};
static int memory_read(void *context, uint64_t offset, void *buffer, size_t size)
{
	const struct pkg_file *file = context;
	if (offset > file->size || size > file->size - offset) return -EINVAL;
	memcpy(buffer, file->data + offset, size);
	return 0;
}
static int read_all(const char *path, char *buffer, size_t cap, size_t *length)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -errno;
	ssize_t n = read(fd, buffer, cap - 1);
	int saved = errno;
	close(fd);
	if (n < 0) return -saved;
	buffer[n] = 0;
	*length = (size_t)n;
	return 0;
}
static int repository(char out[256])
{
	size_t n;
	if (read_all("/etc/pkg.conf", out, 256, &n) || !n) return -1;
	while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
		out[--n] = 0;
	return pkg_http_repository_valid(out) ? 0 : -1;
}
static int fetch_release(struct pkg_archive *archive, uint8_t **package_bytes)
{
	char repo[256], index_path[] = "/index", package_path[160];
	size_t index_size = 0, package_size = 0, count = 0;
	uint8_t *index = NULL, *bytes = NULL;
	struct pkg_index_entry entries[PKG_MAX_PACKAGES], *selected = NULL;
	int result = -1;
	if (repository(repo)) { fputs("sysupgrade: invalid /etc/pkg.conf\n", stderr); goto done; }
	index = pkg_http_get(repo, index_path, PKG_INDEX_MAX, &index_size);
	if (!index || memchr(index, 0, index_size)) goto done;
	char *index_text = malloc(index_size + 1);
	if (!index_text) goto done;
	memcpy(index_text, index, index_size); index_text[index_size] = 0;
	const char *parse_error = pkg_index_parse(index_text, entries, &count);
	free(index_text);
	if (parse_error) { fprintf(stderr, "sysupgrade: repository index: %s\n", parse_error); goto done; }
	for (size_t i = 0; i < count; i++)
		if (!strcmp(entries[i].name, "os-kernel") &&
		    (!selected || strcmp(entries[i].version, selected->version) > 0))
			selected = &entries[i];
	if (!selected) { fputs("sysupgrade: no os-kernel release in repository\n", stderr); goto done; }
	snprintf(package_path, sizeof(package_path), "/packages/%s", selected->filename);
	bytes = pkg_http_get(repo, package_path, PKG_MAX_SIZE, &package_size);
	if (!bytes || package_size != selected->size) goto done;
	uint8_t digest[32]; char hex[65];
	pkg_sha256(bytes, package_size, digest); pkg_hex(digest, hex);
	if (strcmp(hex, selected->hash) || pkg_parse(bytes, package_size, 0, archive)) {
		fputs("sysupgrade: package hash or format is invalid\n", stderr); goto done;
	}
	if (strcmp(archive->name, "os-kernel") || strcmp(archive->version, selected->version)) {
		fputs("sysupgrade: package metadata does not match the index\n", stderr); goto done;
	}
	*package_bytes = bytes; bytes = NULL;
	result = 0;
done:
	free(index); free(bytes);
	if (result) fputs("sysupgrade: unable to fetch a verified release\n", stderr);
	return result;
}
static const struct pkg_file *find_file(const struct pkg_archive *a, const char *path)
{
	for (uint32_t i = 0; i < a->count; i++)
		if (!strcmp(a->files[i].path, path)) return &a->files[i];
	return NULL;
}
static int field_value(const char *manifest, const char *key, char *out, size_t cap)
{
	char prefix[64];
	int n = snprintf(prefix, sizeof(prefix), "%s=", key);
	if (n < 0 || (size_t)n >= sizeof(prefix)) return -1;
	const char *line = manifest;
	while (*line) {
		const char *end = strchr(line, '\n');
		size_t length = end ? (size_t)(end - line) : strlen(line);
		if (length > (size_t)n && !memcmp(line, prefix, (size_t)n)) {
			size_t value = length - (size_t)n;
			if (value >= cap) return -1;
			memcpy(out, line + n, value); out[value] = 0; return 0;
		}
		if (!end) break;
		line = end + 1;
	}
	return -1;
}
static int running_driver_api(unsigned *major, unsigned *minor);
static int verify_manifest(const struct pkg_archive *archive, struct release *out)
{
	const struct pkg_file *manifest = find_file(archive, "opt/os-kernel/manifest");
	const struct pkg_file *signature = find_file(archive, "opt/os-kernel/manifest.sig");
	const struct pkg_file *kernel = find_file(archive, "opt/os-kernel/kernel.elf");
	const struct pkg_file *init = find_file(archive, "opt/os-kernel/init.cpi");
	char manifest_text[MANIFEST_MAX + 1];
	char version[32], api[32], ksize[32], isize[32], khash[65], ihash[65];
	uint8_t digest[32];
	if (!manifest || !signature || !kernel || !init ||
	    manifest->size > MANIFEST_MAX || signature->size != SIGNATURE_BYTES ||
	    memchr(manifest->data, 0, manifest->size)) {
		fputs("sysupgrade: incomplete release manifest\n", stderr); return -1;
	}
	memcpy(manifest_text, manifest->data, manifest->size);
	manifest_text[manifest->size] = 0;
	if (strncmp(manifest_text, "OSUPG1\n", 7) ||
	    field_value(manifest_text, "version", version, sizeof(version)) ||
	    field_value(manifest_text, "driver-api", api, sizeof(api)) ||
	    field_value(manifest_text, "kernel-size", ksize, sizeof(ksize)) ||
	    field_value(manifest_text, "init-size", isize, sizeof(isize)) ||
	    field_value(manifest_text, "kernel-sha256", khash, sizeof(khash)) ||
	    field_value(manifest_text, "init-sha256", ihash, sizeof(ihash))) {
		fputs("sysupgrade: invalid release manifest fields\n", stderr); return -1;
	}
	if (strcmp(version, archive->version)) {
		fputs("sysupgrade: manifest version does not match package version\n", stderr); return -1;
	}
	unsigned major = 0, minor = 0, running_major = 0, running_minor = 0;
	char api_extra;
	if (sscanf(api, "%u.%u%c", &major, &minor, &api_extra) != 2 ||
	    running_driver_api(&running_major, &running_minor) ||
	    major != running_major || minor != running_minor) {
		fputs("sysupgrade: release driver ABI differs from the running kernel; update full system instead\n", stderr); return -1;
	}
	char *number_end;
	unsigned long kernel_size = strtoul(ksize, &number_end, 10);
	if (!*ksize || *number_end || kernel_size != kernel->size) {
		fputs("sysupgrade: release size does not match manifest\n", stderr); return -1;
	}
	unsigned long init_size = strtoul(isize, &number_end, 10);
	if (!*isize || *number_end || init_size != init->size) {
		fputs("sysupgrade: release size does not match manifest\n", stderr); return -1;
	}
	pkg_sha256(kernel->data, kernel->size, digest); char actual[65]; pkg_hex(digest, actual);
	if (strcmp(actual, khash)) return -1;
	pkg_sha256(init->data, init->size, digest); pkg_hex(digest, actual);
	if (strcmp(actual, ihash)) return -1;
	mbedtls_pk_context pk; mbedtls_pk_init(&pk);
	int error = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)public_key,
	    sizeof(public_key));
	if (!error) {
		pkg_sha256(manifest->data, manifest->size, digest);
		error = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest),
		    signature->data, signature->size);
	}
	mbedtls_pk_free(&pk);
	if (error) { fputs("sysupgrade: release signature is invalid\n", stderr); return -1; }
	*out = (struct release){.version = archive->version, .kernel = kernel->data,
	    .initramfs = init->data, .kernel_size = kernel->size, .init_size = init->size};
	return 0;
}
static int proc_value(const char *key, char *out, size_t cap)
{
	char cmdline[2048]; size_t n;
	if (read_all("/proc/cmdline", cmdline, sizeof(cmdline), &n)) return -1;
	char wanted[64]; snprintf(wanted, sizeof(wanted), "%s=", key);
	char *token = strtok(cmdline, " \n");
	while (token) {
		if (!strncmp(token, wanted, strlen(wanted))) {
			size_t len = strlen(token + strlen(wanted));
			if (!len || len >= cap) return -1;
			memcpy(out, token + strlen(wanted), len + 1); return 0;
		}
		token = strtok(NULL, " \n");
	}
	return -1;
}
static int running_driver_api(unsigned *major, unsigned *minor)
{
	char text[32]; size_t n;
	int error = read_all("/proc/dapi", text, sizeof(text), &n);
	if (error || sscanf(text, "%u.%u", major, minor) != 2)
		return -1;
	return 0;
}
static int locate_esp(char path[64], uint8_t root_uuid[16])
{
	char uuid[40];
	if (proc_value("root", uuid, sizeof(uuid)) || strncmp(uuid, "PARTUUID=", 9) ||
	    !uuid_parse(uuid + 9, strlen(uuid + 9), root_uuid)) return -1;
	const uint8_t esp_type[16] = {0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};
	char root_parent[32] = {0};
	for (unsigned d = 0; d < 8; d++) for (unsigned p = 1; p < 17; p++) {
		snprintf(path, 64, "/dev/ada%up%u", d, p);
		int fd = open(path, O_RDONLY); if (fd < 0) continue;
		struct os_disk_partinfo info;
		int ok = ioctl(fd, DIOCGPARTINFO, &info) == 0; close(fd);
		if (ok && !memcmp(info.uuid, root_uuid, 16)) { memcpy(root_parent, info.parent, sizeof(root_parent)); goto sibling; }
	}
	return -1;
sibling:
	for (unsigned d = 0; d < 8; d++) for (unsigned p = 1; p < 17; p++) {
		snprintf(path, 64, "/dev/ada%up%u", d, p);
		int fd = open(path, O_RDONLY); if (fd < 0) continue;
		struct os_disk_partinfo info;
		int ok = ioctl(fd, DIOCGPARTINFO, &info) == 0; close(fd);
		if (ok && !memcmp(info.parent, root_parent, sizeof(root_parent)) &&
		    !memcmp(info.type_uuid, esp_type, 16)) return 0;
	}
	return -1;
}
static int usage(void)
{
	fputs("usage: sysupgrade check | apply\n", stderr); return 2;
}
int main(int argc, char **argv)
{
	if (argc != 2 || (strcmp(argv[1], "check") && strcmp(argv[1], "apply"))) return usage();
	struct pkg_archive archive = {0}; struct release release = {0}; uint8_t *package = NULL;
	if (fetch_release(&archive, &package)) return 1;
	int result = verify_manifest(&archive, &release);
	if (result) { free(package); return 1; }
	unsigned running_major = 0, running_minor = 0;
	(void)running_driver_api(&running_major, &running_minor);
	printf("Verified OpenSillicon kernel release %s (driver ABI %u.%u)\n",
	    release.version, running_major, running_minor);
	if (!strcmp(argv[1], "check")) { free(package); return 0; }
	char slot[8] = "A", active_slot[8] = {0}, esp_path[64], selected = 0; uint8_t root_uuid[16];
	(void)proc_value("kernel-slot", active_slot, sizeof(active_slot));
	if ((active_slot[0] == 'A' || active_slot[0] == 'B') && !active_slot[1]) slot[0] = active_slot[0];
	if (locate_esp(esp_path, root_uuid)) {
		fputs("sysupgrade: could not find this system's EFI partition\n", stderr); free(package); return 1;
	}
	struct storage_device device;
	int error = storage_open(&device, esp_path, 1);
	if (!error) {
		const struct pkg_file *kernel_file = find_file(&archive, "opt/os-kernel/kernel.elf");
		const struct pkg_file *init_file = find_file(&archive, "opt/os-kernel/init.cpi");
		struct esp_file kernel = {.context = (void *)kernel_file, .bytes = kernel_file->size, .read = memory_read};
		struct esp_file init = {.context = (void *)init_file, .bytes = init_file->size, .read = memory_read};
		error = esp_update_slots(&device.io, &kernel, &init, slot[0], root_uuid, &selected);
		storage_close(&device);
	}
	if (error) { fprintf(stderr, "sysupgrade: install failed (%d)\n", error); free(package); return 1; }
	printf("Installed to EFI slot %c; reboot to start it. The previous slot remains available in Limine.\n", selected);
	free(package); return 0;
}
