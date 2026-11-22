#include "../include/os.h"

#define PATH_SIZE 256U

static const char *
type_name(uint32_t mode)
{
	if (OS_S_ISREG(mode))
		return "regular file";
	if (OS_S_ISDIR(mode))
		return "directory";
	return "unknown";
}

static void
print_octal(uint32_t value)
{
	char digits[11];
	size_t count = 0;
	do {
		digits[count++] = (char)('0' + (value & 7U));
		value >>= 3;
	} while (value != 0 && count < sizeof(digits));
	while (count != 0)
		u_putn(&digits[--count], 1);
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	int failed = 0;
	if (argc == 1) {
		u_puts("usage: stat file ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		struct os_stat status;
		if (u_stat_path(argv[i], &status) < 0) {
			u_puts("stat: cannot stat: ");
			u_puts(argv[i]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		u_puts("  File: ");
		u_puts(argv[i]);
		u_puts("\n  Type: ");
		u_puts(type_name(status.st_mode));
		u_puts("\n  Size: ");
		u_put_u64(status.st_size);
		u_puts(" bytes\n  Mode: ");
		print_octal(status.st_mode & 07777U);
		u_puts("\n");
	}
	return failed;
}
