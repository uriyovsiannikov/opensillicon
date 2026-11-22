#define main opensillicon_shell_main
#include "../../user/shell/shell.c"
#undef main

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
	/* Exercise real stdio output, keeping editor escape sequences out of the report. */
	FILE *editor = tmpfile();
	int report = dup(STDOUT_FILENO);
	if (editor == NULL || report < 0 || fflush(stdout) != 0 ||
	    dup2(fileno(editor), STDOUT_FILENO) < 0)
		return 6;
	complete_line(line, &length, &cursor);
	if (fflush(stdout) != 0 || dup2(report, STDOUT_FILENO) < 0 ||
	    close(report) != 0 || fclose(editor) != 0)
		return 7;
	printf("COMPLETE %s\n", line);
	const char *command[] = {"/bin/true", NULL};
	command_external(command, 0, 0);
	if (waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD)
		return 3;
	command_external(command, 1, 0);
	if (background_child_count != 1)
		return 4;
	for (int i = 0; i < 1000 && background_child_count != 0; i++) {
		reap_background_children();
		struct timespec delay = {.tv_nsec = 1000000};
		nanosleep(&delay, NULL);
	}
	if (background_child_count != 0 || waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD)
		return 5;
	return 0;
}
