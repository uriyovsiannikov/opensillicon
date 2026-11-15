#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

size_t os_strftime(char *, size_t, const char *, const struct tm *);
char *os_ctime(const time_t *);

int
main(void)
{
	static const char format[] = "%a|%A|%b|%B|%c|%C|%d|%D|%e|%F|%g|%G|%H|%I|%j|%m|%M|%p|%r|%R|%s|%S|%T|%u|%U|%V|%w|%W|%x|%X|%y|%Y|%z|%Z|%%";
	for (long day = -3653; day < 55000; day++) {
		time_t timestamp = day * 86400 + 17 * 3600 + 35 * 60 + 19;
		struct tm date = *localtime(&timestamp);
		char reference[512], result[512];
		size_t expected = strftime(reference, sizeof(reference), format, &date);
		size_t actual = os_strftime(result, sizeof(result), format, &date);
		if (expected != actual || strcmp(reference, result) != 0) {
			fprintf(stderr, "day %ld\n%s\n%s\n", day, reference, result);
			return 1;
		}
		assert(strcmp(os_ctime(&timestamp), ctime(&timestamp)) == 0);
	}
	time_t timestamp = 0;
	struct tm date = *localtime(&timestamp);
	char small[6] = {1, 2, 3, 4, 5, 6};
	assert(os_strftime(small, 5, "%Y-%m", &date) == 0);
	assert(small[5] == 6);
	assert(os_strftime(NULL, 0, "%Y", &date) == 0);
	assert(os_strftime(small, sizeof(small), "", &date) == 0 && small[0] == 0);
	puts("C-locale UTC strftime/ctime: calendar comparison and bounds passed");
}
