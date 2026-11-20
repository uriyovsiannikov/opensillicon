#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int audit_errno;
int audit_sscanf(const char *, const char *, ...);

void *audit_fdopen(int, const char *);
int audit_fclose(void *);
size_t audit_fread(void *, size_t, size_t, void *);
size_t audit_fwrite(const void *, size_t, size_t, void *);
int audit_feof(void *);
int audit_ferror(void *);
int audit_fseek(void *, long, int);
long audit_ftell(void *);
void audit_rewind(void *);
int audit_ungetc(int, void *);
int audit_fgetc(void *);
int audit_fputc(int, void *);

static int interrupt_read, interrupt_write;
ssize_t audit_read(int fd, void *buffer, size_t size)
{
	if (interrupt_read) { interrupt_read = 0; audit_errno = EINTR; return -1; }
	ssize_t result = read(fd, buffer, size);
	if (result < 0) audit_errno = errno;
	return result;
}
ssize_t audit_write(int fd, const void *buffer, size_t size)
{
	if (interrupt_write) { interrupt_write = 0; audit_errno = EINTR; return -1; }
	ssize_t result = write(fd, buffer, size);
	if (result < 0) audit_errno = errno;
	return result;
}

static void stream_states(void)
{
	int fd = open("/dev/null", O_RDWR);
	assert(fd >= 0);
	void *stream = audit_fdopen(fd, "r+");
	assert(stream);
	char buffer[4];
	assert(audit_fread(buffer, 1, sizeof(buffer), stream) == 0);
	assert(audit_feof(stream) && !audit_ferror(stream));
	assert(audit_ungetc('x', stream) == 'x' && !audit_feof(stream));
	assert(audit_fread(buffer, 1, 1, stream) == 1 && buffer[0] == 'x');
	assert(audit_fread(buffer, 1, 1, stream) == 0 && audit_feof(stream));
	assert(audit_fseek(stream, 0, SEEK_SET) == 0 && !audit_feof(stream));
	assert(close(fd) == 0);
	assert(audit_fread(buffer, 1, 1, stream) == 0 && audit_ferror(stream));
	audit_rewind(stream);
	assert(!audit_ferror(stream));
	assert(audit_fputc('x', stream) == -1 && audit_ferror(stream));
	assert(audit_fclose(stream) == -1);

	fd = open("/dev/null", O_RDWR);
	stream = audit_fdopen(fd, "r+");
	assert(stream);
	interrupt_write = 1;
	assert(audit_fwrite("x", 1, 1, stream) == 1 && !audit_ferror(stream));
	interrupt_write = 1;
	assert(audit_fputc('x', stream) == 'x' && !audit_ferror(stream));
	interrupt_read = 1;
	assert(audit_fgetc(stream) == -1 && audit_feof(stream) && !audit_ferror(stream));
	assert(audit_fclose(stream) == 0);

	int pipes[2];
	assert(pipe(pipes) == 0);
	assert(write(pipes[1], "abc", 3) == 3);
	assert(close(pipes[1]) == 0);
	stream = audit_fdopen(pipes[0], "r");
	assert(stream);
	interrupt_read = 1;
	assert(audit_fread(buffer, 1, 4, stream) == 3);
	assert(!memcmp(buffer, "abc", 3) && audit_feof(stream) && !audit_ferror(stream));
	assert(audit_fclose(stream) == 0);
}

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
	stream_states();
	return 0;
}
