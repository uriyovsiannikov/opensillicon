#define main opensillicon_shell_main
#include "../../user/shell/shell.c"
#undef main

/* These editor writes are irrelevant to the completed input line. */
void u_puts(const char *text) { (void)text; }
void u_putn(const char *text, size_t length) { (void)text; (void)length; }
void u_put_u64(uint64_t value) { (void)value; }

int
main(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	char names[16][HELP_NAME_CAPACITY] = {"help"};
	struct command_list list = {names, 1, (size_t)atoi(argv[1])};
	if (list.capacity == 0 || list.capacity > 16)
		return 2;
	int result = visit_path_commands(collect_command_name, &list);
	printf("RESULT %d\n", result);
	for (size_t i = 0; i < list.count; i++)
		printf("NAME %s\n", names[i]);
	char line[LINE_CAPACITY];
	if (strlen(argv[2]) >= sizeof(line))
		return 2;
	strcpy(line, argv[2]);
	size_t length = strlen(line), cursor = length;
	complete_line(line, &length, &cursor);
	printf("COMPLETE %s\n", line);
	return 0;
}
