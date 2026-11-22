#define main sleep_program_main
#include "../../user/coreutils/sleep.c"
#undef main
#include <assert.h>

static unsigned calls;
static int error_mode;

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
	assert(request != NULL && remaining != NULL);
	assert(request->tv_sec == (calls == 0 ? 3 : 1));
	assert(request->tv_nsec == (calls == 0 ? 0 : 250000000));
	calls++;
	if (error_mode) {
		errno = EINVAL;
		return -1;
	}
	if (calls == 1) {
		*remaining = (struct timespec){.tv_sec = 1, .tv_nsec = 250000000};
		errno = EINTR;
		return -1;
	}
	return 0;
}

int main(void)
{
	char *arguments[] = {"sleep", "3", NULL};
	assert(sleep_program_main(2, arguments) == 0 && calls == 2);
	calls = 0; error_mode = 1;
	assert(sleep_program_main(2, arguments) == 1 && calls == 1);
	static const char *invalid[] = {"", "-1", "+1", "1.5", "1s", "9223372036854775808", "184467440737095516160"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		calls = 0;
		arguments[1] = (char *)invalid[i];
		assert(sleep_program_main(2, arguments) == 2 && calls == 0);
	}
	assert(sleep_program_main(1, arguments) == 2 && calls == 0);
	puts("sleep interruption, failure and input-bound checks passed");
	return 0;
}
