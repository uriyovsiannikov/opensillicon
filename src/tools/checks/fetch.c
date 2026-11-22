#define _GNU_SOURCE
/* Exercise the port's formatting and query failure paths with deterministic data. */
#define main fetch_program_main
#include "../pkg/repo/_src/fetch/fetch.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static char *output;
static size_t used;
static int failed_query;

static void
reset_output(void)
{
	assert(fclose(stdout) == 0);
	free(output);
	output = NULL;
	stdout = open_memstream(&output, &used);
	assert(stdout);
}

static int
matches(const char *expected)
{
	assert(fflush(stdout) == 0);
	return !strcmp(output, expected);
}
int sysctlbyname(const char *name, void *data, size_t *length,
	const void *replacement, size_t replacement_length)
{
	assert(replacement == NULL && replacement_length == 0);
	int query = !strcmp(name, "hw.physmem") ? 1 :
	    !strcmp(name, "vm.freepages") ? 2 : 3;
	if (failed_query == query)
		return -1;
	if (query == 3) {
		assert(!strcmp(name, "hw.pagesize") && *length == sizeof(int));
		*(int *)data = failed_query == 4 ? 0 : 4096;
	} else {
		assert(*length == sizeof(uint64_t));
		*(uint64_t *)data = query == 1 ? 16U * 1024U * 1024U :
		    failed_query == 5 ? UINT64_MAX : 1024;
	}
	if (failed_query == 6)
		*length = 1;
	return 0;
}
int clock_gettime(clockid_t clock, struct timespec *value)
{
	assert(clock == CLOCK_MONOTONIC);
	if (failed_query == 7)
		return -1;
	*value = (struct timespec){.tv_sec = 3661, .tv_nsec = 500000000};
	return 0;
}
int uname(struct utsname *value)
{
	if (failed_query == 8)
		return -1;
	*value = (struct utsname){0};
	strcpy(value->sysname, "TestSystem");
	strcpy(value->machine, "TestMachine");
	return 0;
}
int main(void)
{
	FILE *original_stdout = stdout;
	stdout = open_memstream(&output, &used);
	assert(stdout);
	assert(write_memory() && matches("12.0 / 16.0 MiB"));
	for (failed_query = 1; failed_query <= 6; failed_query++) {
		reset_output();
		assert(write_memory() && matches("unavailable"));
	}
	failed_query = 0; reset_output();
	assert(write_uptime() && matches("1h 1m 1s"));
	failed_query = 7; reset_output();
	assert(write_uptime() && matches("unavailable"));
	failed_query = 0; reset_output();
	assert(write_identity(0) && matches("TestSystem"));
	reset_output();
	assert(write_identity(1) && matches("TestMachine"));
	failed_query = 8; reset_output();
	assert(write_identity(0) && matches("unavailable"));
	assert(fclose(stdout) == 0);
	free(output);
	stdout = original_stdout;
	puts("fetch typed-query formatting and failure checks passed");
	return 0;
}
