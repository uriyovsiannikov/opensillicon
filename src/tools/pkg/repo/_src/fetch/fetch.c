#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#define LOGO_WIDTH 24U

static const char *const logo[] = {
	"          __",
	"    .-'\" (M /--.",
	"  .'   .'|o|  @ `..",
	" /  .-/       _....'",
	"|  '-/      .' |",
	"|   /      |_\\ |",
	" \\  |    /\\|  /",
	"  '.|   /   .'",
	"    |  /_.-'",
	"    | /",
	"    | |",
	"   (,-,)"
};

#define INFO_LINE_COUNT 8U

static const char *const information_labels[INFO_LINE_COUNT] = {
	"OS: ", "Uptime: ", "Architecture: ", "Shell: ",
	"Memory: ", "/bin entries: ", "Terminal: ", "PID: "
};

static const char *const logo_colors[] = {
	"\033[94m", "\033[96m", "\033[95m", "\033[94m",
	"\033[96m", "\033[95m", "\033[94m", "\033[96m",
	"\033[95m", "\033[94m", "\033[96m", "\033[95m"
};

static const char *const normal_palette[] = {
	"\033[40m  \033[0m", "\033[41m  \033[0m",
	"\033[42m  \033[0m", "\033[43m  \033[0m",
	"\033[44m  \033[0m", "\033[45m  \033[0m",
	"\033[46m  \033[0m", "\033[47m  \033[0m"
};

static const char *const bright_palette[] = {
	"\033[100m  \033[0m", "\033[101m  \033[0m",
	"\033[102m  \033[0m", "\033[103m  \033[0m",
	"\033[104m  \033[0m", "\033[105m  \033[0m",
	"\033[106m  \033[0m", "\033[107m  \033[0m"
};

static int
write_string(const char *text)
{
	return fputs(text, stdout) != EOF;
}

static int
write_palette(const char *const colors[8])
{
	size_t index;

	for (index = 0; index < 8; index++) {
		if (!write_string(colors[index]))
			return 0;
	}
	return 1;
}

static int
write_uptime(void)
{
	struct timespec uptime;
	if (clock_gettime(CLOCK_MONOTONIC, &uptime) < 0 || uptime.tv_sec < 0)
		return write_string("unavailable");
	uint64_t seconds = (uint64_t)uptime.tv_sec;
	return printf("%" PRIu64 "h %" PRIu64 "m %" PRIu64 "s",
	    seconds / 3600U, seconds / 60U % 60U, seconds % 60U) >= 0;
}

static int
write_mib(uint64_t kib)
{
	return printf("%" PRIu64 ".%" PRIu64, kib / 1024U,
	    kib % 1024U * 10U / 1024U) >= 0;
}

static int
write_memory(void)
{
	uint64_t total, free_pages;
	int page_size;
	size_t length = sizeof(total);
	if (sysctlbyname("hw.physmem", &total, &length, NULL, 0) < 0 ||
	    length != sizeof(total))
		return write_string("unavailable");
	length = sizeof(free_pages);
	if (sysctlbyname("vm.freepages", &free_pages, &length, NULL, 0) < 0 ||
	    length != sizeof(free_pages))
		return write_string("unavailable");
	length = sizeof(page_size);
	if (sysctlbyname("hw.pagesize", &page_size, &length, NULL, 0) < 0 ||
	    length != sizeof(page_size) || page_size <= 0 ||
	    free_pages > total / (uint64_t)page_size)
		return write_string("unavailable");
	uint64_t free = free_pages * (uint64_t)page_size;
	return write_mib((total - free) / 1024U) && write_string(" / ") &&
	    write_mib(total / 1024U) && write_string(" MiB");
}

static int
write_identity(int machine)
{
	struct utsname identity;
	if (uname(&identity) < 0)
		return write_string("unavailable");
	return write_string(machine ? identity.machine : identity.sysname);
}

static int
write_dynamic_information(size_t row)
{
	if (row == 1)
		return write_uptime();
	if (row == 0 || row == 2)
		return write_identity(row == 2);
	if (row == 3)
		return write_string("/bin/sh");
	if (row == 4)
		return write_memory();
	if (row == 5) {
		DIR *directory = opendir("/bin");
		uint64_t count = 0;
		int read_error = 0;
		if (directory == NULL)
			return write_string("unavailable");
		for (;;) {
			errno = 0;
			struct dirent *entry = readdir(directory);
			if (entry == NULL) {
				read_error = errno;
				break;
			}
			if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
				count++;
		}
		if (closedir(directory) < 0 || read_error != 0)
			return write_string("unavailable");
		return printf("%" PRIu64, count) >= 0;
	}
	if (row == 6) {
		struct winsize size;
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0)
			return write_string("unavailable");
		return printf("%ux%u", (unsigned)size.ws_row, (unsigned)size.ws_col) >= 0;
	}
	return printf("%jd", (intmax_t)getpid()) >= 0;
}

int
main(int argc, char **argv)
{
	size_t row;
	(void)argv;
	if (argc != 1) {
		fputs("fetch: arguments are unsupported\n", stderr);
		return 1;
	}

	for (row = 0; row < sizeof(logo) / sizeof(logo[0]); row++) {
		size_t length = strlen(logo[row]);
		size_t column;

		if (!write_string(logo_colors[row]) ||
		    fwrite(logo[row], 1, length, stdout) != length || !write_string("\033[0m"))
			return 1;
		for (column = length; column < LOGO_WIDTH; column++) {
			if (!write_string(" "))
				return 1;
		}
		if (row < INFO_LINE_COUNT) {
			if (!write_string("\033[96m") ||
			    !write_string(information_labels[row]) ||
			    !write_string("\033[0m"))
				return 1;
			if (!write_dynamic_information(row) || !write_string("\033[0m"))
				return 1;
		} else if (row == INFO_LINE_COUNT + 1) {
			if (!write_palette(normal_palette))
				return 1;
		} else if (row == INFO_LINE_COUNT + 2) {
			if (!write_palette(bright_palette))
				return 1;
		}
		if (!write_string("\n"))
			return 1;
	}
	return fflush(stdout) == EOF;
}
