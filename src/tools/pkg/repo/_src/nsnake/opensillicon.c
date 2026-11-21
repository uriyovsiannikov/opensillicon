#include <curses.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define SNAKE_CAP 2048
static int sx[SNAKE_CAP], sy[SNAKE_CAP];
static uint32_t seed = 0x9e3779b9U;
static uint32_t random_next(void) { seed = seed * 1103515245U + 12345U; return seed; }

int
main(void)
{
	int rows, columns, length = 4, dx = 1, dy = 0, fx, fy, score = 0, key;
	seed ^= (uint32_t)time(NULL);
	if (initscr() == NULL) return 1;
	noecho(); cbreak(); keypad(stdscr, TRUE); timeout(100); curs_set(0);
	getmaxyx(stdscr, rows, columns);
	if (rows < 8 || columns < 12) { endwin(); return 1; }
	for (int i = 0; i < length; i++) { sx[i] = columns / 2 - i; sy[i] = rows / 2; }
	fx = 2 + (int)(random_next() % (uint32_t)(columns - 4));
	fy = 2 + (int)(random_next() % (uint32_t)(rows - 4));
	for (;;) {
		clear();
		box(stdscr, 0, 0);
		mvprintw(0, 2, " nSnake  score %d  (q quits) ", score);
		mvaddch(fy, fx, '*');
		for (int i = 0; i < length; i++) mvaddch(sy[i], sx[i], i == 0 ? '@' : 'o');
		refresh();
		key = getch();
		if (key == 'q' || key == 'Q' || key == 27) break;
		if ((key == KEY_UP || key == 'w') && dy != 1) { dx = 0; dy = -1; }
		else if ((key == KEY_DOWN || key == 's') && dy != -1) { dx = 0; dy = 1; }
		else if ((key == KEY_LEFT || key == 'a') && dx != 1) { dx = -1; dy = 0; }
		else if ((key == KEY_RIGHT || key == 'd') && dx != -1) { dx = 1; dy = 0; }
		int nx = sx[0] + dx, ny = sy[0] + dy;
		if (nx <= 0 || nx >= columns - 1 || ny <= 0 || ny >= rows - 1) break;
		int eating = nx == fx && ny == fy;
		for (int i = 0; i < length - (eating ? 0 : 1); i++) if (sx[i] == nx && sy[i] == ny) goto done;
		if (eating && length < SNAKE_CAP) { length++; score += 10; fx = 1 + (int)(random_next() % (uint32_t)(columns - 2)); fy = 1 + (int)(random_next() % (uint32_t)(rows - 2)); }
		for (int i = length - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
		sx[0] = nx; sy[0] = ny;
	}
done:
	endwin();
	printf("nSnake score: %d\n", score);
	return 0;
}
