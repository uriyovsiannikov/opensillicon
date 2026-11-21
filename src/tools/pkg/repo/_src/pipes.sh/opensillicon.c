#include <curses.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static uint32_t seed = 0x5a17c9e3U;
static uint32_t next_random(void) { seed = seed * 1664525U + 1013904223U; return seed; }

int
main(void)
{
	int rows, columns, x, y, direction;
	int key;
	seed ^= (uint32_t)time(NULL);
	if (initscr() == NULL) return 1;
	noecho();
	cbreak();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	curs_set(0);
	getmaxyx(stdscr, rows, columns);
	if (rows < 3 || columns < 3) { endwin(); return 1; }
	x = 1 + (int)(next_random() % (uint32_t)(columns - 2));
	y = 1 + (int)(next_random() % (uint32_t)(rows - 2));
	direction = (int)(next_random() & 3U);
	while ((key = getch()) != 'q' && key != 'Q' && key != 27) {
		static const char glyphs[] = "|/-\\";
		mvaddch(y, x, glyphs[direction]);
		if ((next_random() & 15U) == 0) direction = (direction + 1 + (int)(next_random() & 1U)) & 3;
		switch (direction) {
		case 0: y--; break;
		case 1: x++; break;
		case 2: y++; break;
		default: x--; break;
		}
		if (x < 1 || x >= columns - 1 || y < 1 || y >= rows - 1) {
			direction = (int)(next_random() & 3U);
			x = 1 + (int)(next_random() % (uint32_t)(columns - 2));
			y = 1 + (int)(next_random() % (uint32_t)(rows - 2));
		}
		refresh();
		usleep(35000);
	}
	endwin();
	return 0;
}
