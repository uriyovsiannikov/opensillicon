/* SPDX-License-Identifier: BSD-2-Clause
 * Small read-only console pager. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define TEXT_LIMIT (16U * 1024U * 1024U)
#define LINE_LIMIT (1024U * 1024U)
static struct termios saved_terminal;
static int terminal_changed;
static volatile sig_atomic_t interrupted;
static void on_signal(int number) { interrupted = number; }
static int write_all(const char *text, size_t size)
{
	while (size && !interrupted) {
		ssize_t amount = write(STDOUT_FILENO, text, size);
		if (amount < 0 && errno == EINTR) continue;
		if (amount <= 0) return 0;
		text += amount; size -= (size_t)amount;
	}
	return size == 0;
}
static void restore_terminal(void)
{
	if (terminal_changed) {
		(void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
		terminal_changed = 0;
		static const char reset[] = "\033[0m\033[?25h\033[2J\033[H";
		(void)write(STDOUT_FILENO, reset, sizeof(reset) - 1);
	}
}
static int load_file(const char *path, char **text, size_t **lines, size_t *length, size_t *count)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror(path); return 0; }
	size_t used = 0, capacity = 8192;
	char *data = malloc(capacity);
	if (!data) { perror("txtview: allocation"); close(fd); return 0; }
	for (;;) {
		if (used == capacity) {
			if (capacity == TEXT_LIMIT) {
				char extra; ssize_t amount;
				do { amount = read(fd, &extra, 1); } while (amount < 0 && errno == EINTR && !interrupted);
				if (amount == 0) break;
				fprintf(stderr, "txtview: file too large or unreadable (limit 16 MiB)\n");
				goto fail;
			}
			capacity *= 2;
			char *grown = realloc(data, capacity);
			if (!grown) { perror("txtview: allocation"); goto fail; }
			data = grown;
		}
		ssize_t amount = read(fd, data + used, capacity - used);
		if (amount < 0 && errno == EINTR && !interrupted) continue;
		if (amount < 0) { perror("txtview: read"); goto fail; }
		if (!amount) break;
		used += (size_t)amount;
	}
	if (close(fd) < 0) { fd = -1; perror("txtview: close"); goto fail; }
	fd = -1;
	size_t n = 1;
	for (size_t i = 0; i < used; i++) if (data[i] == '\n' && i + 1 < used) n++;
	if (n > LINE_LIMIT) { fprintf(stderr, "txtview: too many lines\n"); goto fail; }
	size_t *offsets = malloc(n * sizeof(*offsets));
	if (!offsets) { perror("txtview: allocation"); goto fail; }
	offsets[0] = 0;
	for (size_t i = 0, next = 1; i < used; i++)
		if (data[i] == '\n' && i + 1 < used) offsets[next++] = i + 1;
	*text = data; *lines = offsets; *length = used; *count = n;
	return 1;
fail:
	if (fd >= 0) close(fd);
	free(data); return 0;
}
/* Escape sequences in log contents must be displayed as text, not executed.
 * Tabs are expanded at 8-column stops; CR is ignored for CRLF logs. */
static size_t line_text(char *out, size_t width, const char *text, size_t start,
	size_t end, size_t left)
{
	size_t column = 0, used = 0;
	for (size_t i = start; i < end && used < width; i++) {
		unsigned char ch = (unsigned char)text[i];
		if (ch == '\n') break;
		if (ch == '\r') continue;
		unsigned repeat = ch == '\t' ? 8 - column % 8 : 1;
		char value = ch == '\t' ? ' ' : (ch < 32 || ch == 127 || ch == 155) ? '?' : (char)ch;
		while (repeat--) {
			if (column++ >= left) out[used++] = value;
			if (used == width) break;
		}
	}
	return used;
}
static int draw(const char *path, const char *text, const size_t *lines,
	size_t length, size_t count, size_t *top, size_t left, size_t *page)
{
	struct winsize size = {0};
	unsigned rows = 25, columns = 80;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
		if (size.ws_row >= 2) rows = size.ws_row;
		if (size.ws_col >= 2) columns = size.ws_col;
	}
	if (rows > 256) rows = 256;
	if (columns > 1024) columns = 1024;
	*page = rows - 1;
	size_t last = count > *page ? count - *page : 0;
	if (*top > last) *top = last;
	char output[1100], status[2048];
	for (unsigned row = 0; row < rows - 1; row++) {
		int prefix = snprintf(output, sizeof(output), "\033[%u;1H\033[0m\033[K", row + 1);
		size_t n = (size_t)prefix, line = *top + row;
		if (line < count) n += line_text(output + n, columns - 1, text, lines[line],
		    line + 1 < count ? lines[line + 1] : length, left);
		if (!write_all(output, n)) return 0;
	}
	snprintf(status, sizeof(status), "%zu-%zu/%zu col %zu | q quit | arrows PgUp/PgDn Home/End | %s",
	    *top + 1, *top + *page < count ? *top + *page : count, count, left + 1, path);
	int prefix = snprintf(output, sizeof(output), "\033[%u;1H\033[7m\033[K", rows);
	size_t n = (size_t)prefix;
	/* Sanitize the filename too; it may contain terminal control bytes. */
	n += line_text(output + n, columns - 1, status, 0, strlen(status), 0);
	memcpy(output + n, "\033[0m", 4); n += 4;
	return write_all(output, n);
}
/* Read one byte; timeout is used only to distinguish Esc from an arrow. */
static int byte(int timeout)
{
	if (timeout) {
		fd_set ready; FD_ZERO(&ready); FD_SET(STDIN_FILENO, &ready);
		struct timeval delay = {0, 150000};
		int rc = select(1, &ready, NULL, NULL, &delay);
		if (rc <= 0) return -1;
	}
	unsigned char ch;
	ssize_t n;
	do {
		n = read(STDIN_FILENO, &ch, 1);
		/* Native console raw reads may time out with EAGAIN even with
		 * VMIN=1. An idle reader must keep waiting, not exit the pager. */
	} while (n < 0 && (errno == EINTR || (!timeout && errno == EAGAIN)) && !interrupted);
	return n == 1 ? ch : -1;
}
enum { UP = 256, DOWN, LEFT, RIGHT, PAGE_UP, PAGE_DOWN, HOME, END };
static int key(void)
{
	int ch = byte(0);
	if (ch != 27) return ch;
	int next = byte(1);
	if (next < 0) return 27;
	if (next != '[' && next != 'O') return 0;
	ch = byte(1);
	switch (ch) {
	case 'A': return UP; case 'B': return DOWN;
	case 'C': return RIGHT; case 'D': return LEFT;
	case 'H': return HOME; case 'F': return END;
	case '1': case '4': case '5': case '6': case '7': case '8':
		if (byte(1) != '~') return 0;
		return ch == '5' ? PAGE_UP : ch == '6' ? PAGE_DOWN :
		    (ch == '1' || ch == '7') ? HOME : END;
	default: return 0;
	}
}
int main(int argc, char **argv)
{
	if (argc != 2) { fprintf(stderr, "usage: txtview <file>\n"); return 1; }
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "txtview: input and output must be terminals\n"); return 1;
	}
	char *text; size_t *lines, length, count;
	if (!load_file(argv[1], &text, &lines, &length, &count)) return 1;
	if (tcgetattr(STDIN_FILENO, &saved_terminal) < 0) {
		perror("txtview: terminal"); free(lines); free(text); return 1;
	}
	struct termios raw = saved_terminal;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(IXON | ICRNL);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	struct sigaction action = { .sa_handler = on_signal };
	(void)sigemptyset(&action.sa_mask);
	(void)sigaction(SIGINT, &action, NULL); (void)sigaction(SIGTERM, &action, NULL);
	(void)sigaction(SIGHUP, &action, NULL); (void)sigaction(SIGPIPE, &action, NULL);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
		perror("txtview: raw mode"); free(lines); free(text); return 1;
	}
	terminal_changed = 1;
	int result = 0;
	size_t top = 0, left = 0, page = 24;
	if (!write_all("\033[?25l\033[2J\033[H", 13)) result = 1;
	while (!result && !interrupted) {
		if (!draw(argv[1], text, lines, length, count, &top, left, &page)) { result = 1; break; }
		int ch = key();
		if (ch < 0 || ch == 'q' || ch == 'Q' || ch == 27 || ch == 3) break;
		size_t last = count > page ? count - page : 0;
		if (ch == UP || ch == 'k') { if (top) top--; }
		if (ch == DOWN || ch == 'j' || ch == '\n') { if (top < last) top++; }
		if (ch == PAGE_UP || ch == 'b') top = top > page ? top - page : 0;
		if (ch == PAGE_DOWN || ch == ' ') top = last - top < page ? last : top + page;
		if (ch == HOME || ch == 'g') { top = 0; left = 0; }
		if (ch == END || ch == 'G') top = last;
		if (ch == LEFT || ch == 'h') left = left > 8 ? left - 8 : 0;
		if ((ch == RIGHT || ch == 'l') && left < TEXT_LIMIT) left += 8;
	}
	restore_terminal(); free(lines); free(text);
	return interrupted ? 128 + interrupted : result;
}
