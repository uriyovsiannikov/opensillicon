/* Exercise the port's formatting and query failure paths with deterministic data. */
#define main fetch_program_main
#include "../pkg/repo/_src/fetch/fetch.c"
#undef main
#include <assert.h>
#include <stdio.h>

static char output[256];
static size_t used;
static int failed_query;

size_t u_strlen(const char *text) { return strlen(text); }

long u_write(uint64_t fd, const void *data, size_t length)
{
	assert(fd == 1 && used + length < sizeof(output));
	memcpy(output + used, data, length);
	used += length;
	output[used] = 0;
	return length;
}
void u_put_u64(uint64_t value)
{
	char buffer[32];
	int count = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
	assert(count > 0);
	(void)u_write(1, buffer, (size_t)count);
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
int main(void)
{
	assert(write_memory() && !strcmp(output, "12.0 / 16.0 MiB"));
	for (failed_query = 1; failed_query <= 6; failed_query++) {
		used = 0;
		assert(write_memory() && !strcmp(output, "unavailable"));
	}
	failed_query = 0; used = 0;
	assert(write_uptime() && !strcmp(output, "1h 1m 1s"));
	failed_query = 7; used = 0;
	assert(write_uptime() && !strcmp(output, "unavailable"));
	puts("fetch typed-query formatting and failure checks passed");
	return 0;
}
