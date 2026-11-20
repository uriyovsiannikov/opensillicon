#include <assert.h>
#include <string.h>

int audit_errno;
int audit_sscanf(const char *, const char *, ...);

int main(void)
{
	char buffer[32], tail = 0;
	assert(audit_sscanf("aaaZ!", "%31[a]Z%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "aaa") == 0 && tail == '!');
	assert(audit_sscanf("abc]", "%31[^]]%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "abc") == 0 && tail == ']');
	assert(audit_sscanf("]]a!", "%31[]a]%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "]]a") == 0 && tail == '!');
	assert(audit_sscanf("abc1", "%31[a-z]%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "abc") == 0 && tail == '1');
	assert(audit_sscanf("ab-c", "%31[a-z]%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "ab") == 0 && tail == '-');
	assert(audit_sscanf("-a!", "%31[-a]%c", buffer, &tail) == 2);
	assert(strcmp(buffer, "-a") == 0 && tail == '!');
	assert(audit_sscanf("aaZ", "%*2[a]%c", &tail) == 1 && tail == 'Z');
	assert(audit_sscanf("aaa", "%31[a", buffer) == 0);
	return 0;
}
