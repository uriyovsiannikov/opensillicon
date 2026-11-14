#include <curses.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ui.h"

#define MESSAGE_ROWS 8
#define MESSAGE_WIDTH 256

static SCREEN *screen;
static char messages[MESSAGE_ROWS][MESSAGE_WIDTH];
static size_t message_count;
static const char *progress_label;
static size_t progress_done, progress_total;

bool
ui_open(void)
{
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return false;
	const char *term = getenv("TERM");
	screen = newterm(term && *term ? term : "xterm", stdout, stdin);
	if (!screen)
		return false;
	if (cbreak() == ERR || noecho() == ERR ||
	    keypad(stdscr, TRUE) == ERR || LINES < 16 || COLS < 44) {
		ui_close();
		return false;
	}
	/* The kernel console sends CSI arrows even in xterm keypad mode. */
	define_key("\033[A", KEY_UP);
	define_key("\033[B", KEY_DOWN);
	define_key("\033[C", KEY_RIGHT);
	define_key("\033[D", KEY_LEFT);
	curs_set(0);
	return true;
}

bool
ui_active(void)
{
	return screen != NULL;
}

void
ui_close(void)
{
	if (screen) {
		endwin();
		delscreen(screen);
		screen = NULL;
	}
}

static void
row(int y, const char *text)
{
	mvaddnstr(y, 2, text, COLS - 4);
}

static void
frame(const char *title)
{
	erase();
	attron(A_REVERSE);
	mvaddnstr(0, 0, " OpenSillicon Installer ", COLS - 1);
	attroff(A_REVERSE);
	attron(A_BOLD);
	row(2, title);
	attroff(A_BOLD);
}

static int
read_key(void)
{
	for (;;) {
		errno = 0;
		int key = getch();
		if (key != ERR)
			return key;
		/* The raw console times out with EAGAIN. Ncurses can also
		 * return ERR again from its input FIFO without setting errno. */
		if (errno && errno != EAGAIN && errno != EINTR)
			return ERR;
		const struct timespec pause = {.tv_nsec = 10000000};
		nanosleep(&pause, NULL);
	}
}

static bool
enter(int key)
{
	return key == '\n' || key == '\r' || key == KEY_ENTER;
}

int
ui_choose_disk(const struct disk_choice *disks, size_t count)
{
	size_t selected = 0;
	for (;;) {
		frame("Select a target SATA disk");
		if (!count)
			row(4, "No SATA disks available.");
		for (size_t i = 0; i < count; i++) {
			char label[128];
			snprintf(label, sizeof(label), "%s  %llu MiB  %s%s",
			    disks[i].path, disks[i].mib,
			    disks[i].writable ? "" : "[IN USE]  ", disks[i].model);
			if (i == selected)
				attron(A_REVERSE);
			row(4 + (int)i, label);
			attroff(A_REVERSE);
		}
		if (count) {
			char serial[MESSAGE_WIDTH];
			snprintf(serial, sizeof(serial), "Serial: %s",
			    disks[selected].serial[0] ? disks[selected].serial :
			    "unavailable");
			row(LINES - 4, serial);
			if (!disks[selected].writable)
				row(LINES - 3, "Disk or partition is mounted.");
		}
		row(LINES - 2, "Up/Down: select   Enter: review   Esc/Q: exit");
		refresh();

		int key = read_key();
		if (key == ERR)
			return -errno;
		if (key == 27 || key == 'q' || key == 'Q')
			return UI_DISK_CANCELLED;
		if (key == KEY_UP && selected > 0)
			selected--;
		if (key == KEY_DOWN && selected + 1 < count)
			selected++;
		if (enter(key) && count && disks[selected].writable)
			return (int)selected;
	}
}

void
ui_reset(void)
{
	message_count = 0;
	ui_clear_progress();
}

static void
status(const char *title)
{
	frame(title);
	for (size_t i = 0; i < message_count; i++)
		row(4 + (int)i, messages[i]);
	if (progress_total) {
		char text[64];
		snprintf(text, sizeof(text), "%s: %u/%u (%u%%)", progress_label,
		    (unsigned)progress_done, (unsigned)progress_total,
		    (unsigned)(progress_done * 100 / progress_total));
		row(LINES - 4, text);
	}
}

void
ui_note(const char *text)
{
	while (*text) {
		size_t length = strcspn(text, "\n");
		if (message_count == MESSAGE_ROWS) {
			memmove(messages, messages + 1,
			    sizeof(messages) - sizeof(messages[0]));
			message_count--;
		}
		snprintf(messages[message_count++], MESSAGE_WIDTH, "%.*s",
		    (int)length, text);
		text += length;
		if (*text)
			text++;
	}
	/* The boot console does not implement terminal scrolling regions. */
	clearok(stdscr, TRUE);
	status("Installation status");
	refresh();
}

void
ui_progress(const char *label, size_t done, size_t total)
{
	progress_label = label;
	progress_done = done;
	progress_total = total;
	status("Installing system");
	refresh();
}

void
ui_clear_progress(void)
{
	progress_total = 0;
}

int
ui_confirm(const char *phrase)
{
	char answer[64] = {0}, prompt[96];
	size_t length = 0, overflow = 0;
	snprintf(prompt, sizeof(prompt), "Type %s:", phrase);
	curs_set(1);
	int confirmed = 0;
	for (;;) {
		status("Confirm disk erasure");
		row(3, "All partitions on this disk will be replaced.");
		row(LINES - 4, prompt);
		row(LINES - 2, "Enter: confirm   Esc: cancel");
		row(LINES - 3, answer);
		move(LINES - 3, length < (size_t)(COLS - 4) ?
		    2 + (int)length : COLS - 2);
		refresh();
		int key = read_key();
		if (enter(key)) {
			confirmed = !overflow && strcmp(answer, phrase) == 0;
			break;
		}
		if (key == ERR) {
			confirmed = -errno;
			break;
		}
		if (key == 27)
			break;
		if (key == KEY_BACKSPACE || key == 127 || key == '\b') {
			if (overflow)
				overflow--;
			else if (length)
				answer[--length] = 0;
		} else if (key >= 32 && key <= 126) {
			if (length < sizeof(answer) - 1) {
				answer[length++] = (char)key;
				answer[length] = 0;
			} else
				overflow++;
		}
	}
	curs_set(0);
	return confirmed;
}

bool
ui_reboot(void)
{
	bool reboot = false;
	flushinp();
	for (;;) {
		status("Installation complete");
		row(LINES - 3, reboot ? "  Return to console    [ Reboot ]" :
		    "[ Return to console ]   Reboot");
		row(LINES - 2, "Left/Right: select   Enter: accept   Esc: exit");
		refresh();
		int key = read_key();
		if (key == ERR || key == 27 || key == 'n' || key == 'N')
			return false;
		if (key == 'y' || key == 'Y')
			return true;
		if (enter(key))
			return reboot;
		if (key == KEY_LEFT)
			reboot = false;
		if (key == KEY_RIGHT)
			reboot = true;
	}
}

void
ui_wait(void)
{
	status("Installation failed");
	row(LINES - 2, "Any key: return to console");
	refresh();
	(void)read_key();
}
