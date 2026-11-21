#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX 120
#define ROW_MAX 32

static char rows[ROW_MAX][LINE_MAX + 1];
static const char *eyes = "oo";
static const char *tongue = "  ";
static int think;

static int
wrap_text(const char *text, int width)
{
	int row = 0, column = 0;
	while (*text && row < ROW_MAX) {
		while (*text == ' ' || *text == '\t' || *text == '\n') {
			if (*text == '\n' && column) { row++; column = 0; }
			text++;
	}
	if (!*text) break;
	const char *word = text;
	int length = 0;
	while (text[length] && text[length] != ' ' && text[length] != '\t' && text[length] != '\n') length++;
	if (length > width) length = width;
	if (column && column + 1 + length > width) { row++; column = 0; }
	if (row >= ROW_MAX) break;
	if (column) rows[row][column++] = ' ';
	for (int i = 0; i < length && column < width; i++) rows[row][column++] = word[i];
	rows[row][column] = '\0';
	text += length;
	if (*text && *text != ' ' && *text != '\t' && *text != '\n') {
		while (*text && *text != ' ' && *text != '\t' && *text != '\n') text++;
	}
	if (*text == '\n') { row++; column = 0; }
	else if (*text) text++;
	}
	return row + (column || row == 0);
}

int
main(int argc, char **argv)
{
	char input[2048] = {0};
	int width = 40;
	const char *program = argv[0];
	for (const char *p = program; *p; p++)
		if (*p == '/') program = p + 1;
	if (!strcmp(program, "cowthink")) think = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t")) think = 1;
		else if (!strcmp(argv[i], "-e") && i + 1 < argc) eyes = argv[++i];
		else if (!strcmp(argv[i], "-T") && i + 1 < argc) tongue = argv[++i];
		else if (!strcmp(argv[i], "-W") && i + 1 < argc) width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f") && i + 1 < argc) { i++; }
		else if (argv[i][0] == '-') { fprintf(stderr, "cowsay: unsupported option %s\n", argv[i]); return 2; }
		else {
			if (*input) strncat(input, " ", sizeof(input) - strlen(input) - 1);
			strncat(input, argv[i], sizeof(input) - strlen(input) - 1);
		}
	}
	if (!*input && !fgets(input, sizeof(input), stdin)) input[0] = '\0';
	if (width < 1 || width > LINE_MAX) width = 40;
	int count = wrap_text(input, width), max = 0;
	for (int i = 0; i < count; i++) { int n = (int)strlen(rows[i]); if (n > max) max = n; }
	putchar(' '); for (int i = 0; i < max + 2; i++) putchar('_'); putchar('\n');
	for (int i = 0; i < count; i++) {
		char left = count == 1 ? (think ? '(' : '<') : (i == 0 ? '/' : (i == count - 1 ? '\\' : '|'));
		char right = count == 1 ? (think ? ')' : '>') : (i == 0 ? '\\' : (i == count - 1 ? '/' : '|'));
		printf("%c %-*s %c\n", left, max, rows[i], right);
	}
	putchar(' '); for (int i = 0; i < max + 2; i++) putchar('-'); putchar('\n');
	if (think) puts("        o   ^__^"); else puts("         \\   ^__^");
	printf(think ? "         o  (%s)\\_______\n" : "          \\ (%s)\\_______\n", eyes);
	puts("            (__)\\       )\\/\\");
	printf("             %s ||----w |\n", tongue);
	puts("                ||     ||");
	return 0;
}
