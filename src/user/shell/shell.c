#include <dirent.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

#include "include/os.h"

#define LINE_CAPACITY 256U
#define ARGUMENT_CAPACITY U_EXEC_ARG_MAX
#define PATH_CAPACITY 256U
#define HELP_COMMAND_LIMIT 128U
#define HELP_NAME_CAPACITY 256U
#define HELP_COLUMN_WIDTH 22U
#define HISTORY_CAPACITY 16U
#define COMPLETION_LIMIT 64U
#define BACKGROUND_CHILD_LIMIT 128U

struct completion_candidate {
	char name[HELP_NAME_CAPACITY];
	int is_directory;
};

static const char *const internal_commands[] = {
	"help", "cd", "exit", "history"
};

static char current_directory[PATH_CAPACITY] = "/";
static char command_history[HISTORY_CAPACITY][LINE_CAPACITY];
static size_t history_count;
static size_t history_next;
static size_t history_position;
static uint64_t background_children[BACKGROUND_CHILD_LIMIT];
static size_t background_child_count;
static int cursor_visible = 1;
static int xterm_cursor;

static size_t
history_slot(size_t position)
{
	return (history_next + HISTORY_CAPACITY - history_count + position) %
	    HISTORY_CAPACITY;
}

static void
remember_command(const char *line)
{
	size_t i;
	if (line[0] == '\0')
		return;
	if (history_count != 0 && strcmp(
	    command_history[history_slot(history_count - 1)], line) == 0)
		return;
	for (i = 0; i < LINE_CAPACITY - 1 && line[i] != '\0'; i++)
		command_history[history_next][i] = line[i];
	command_history[history_next][i] = '\0';
	history_next = (history_next + 1) % HISTORY_CAPACITY;
	if (history_count < HISTORY_CAPACITY)
		history_count++;
}

static void
print_history(void)
{
	size_t i;
	for (i = 0; i < history_count; i++) {
		u_put_u64(i + 1);
		u_puts("  ");
		u_puts(command_history[history_slot(i)]);
		u_puts("\n");
	}
}

static void
reap_background_children(void)
{
	size_t index = 0;

	while (index < background_child_count) {
		long result = u_waitpid_options(background_children[index], NULL,
		    U_WAIT_NOHANG);

		if (result == 0 || result == -4) {
			index++;
			continue;
		}
		background_children[index] =
		    background_children[--background_child_count];
		background_children[background_child_count] = 0;
	}
}

static void
print_error(const char *operation, const char *path)
{
	u_puts("sh: ");
	u_puts(operation);
	u_puts(": ");
	u_puts(path);
	u_puts(" failed\n");
}

static int
is_executable_entry(DIR *directory, const struct dirent *entry)
{
	struct stat status;
	return strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") &&
	    fstatat(dirfd(directory), entry->d_name, &status, 0) == 0 &&
	    S_ISREG(status.st_mode) && (status.st_mode & 0111U) != 0;
}

/* Visitors return zero to continue, or an error to stop the scan. */
static int
visit_path_commands(int (*visit)(const char *, void *), void *context)
{
	const char *path = getenv("PATH");
	char *copy = strdup(path != NULL ? path : _PATH_DEFPATH);
	if (copy == NULL)
		return -1;
	char *remaining = copy, *component;
	int result = 0;

	while ((component = strsep(&remaining, ":")) != NULL) {
		const char *name = *component != '\0' ? component : ".";
		DIR *directory = opendir(name);
		if (directory == NULL) {
			if (errno == ENOMEM) {
				result = -1;
				break;
			}
			continue;
		}
		for (;;) {
			errno = 0;
			struct dirent *entry = readdir(directory);
			if (entry == NULL) {
				if (errno != 0)
					result = -1;
				break;
			}
			if (!is_executable_entry(directory, entry))
				continue;
			char *candidate;
			if (asprintf(&candidate, "%s/%s", name, entry->d_name) < 0) {
				result = -1;
				break;
			}
			int executable = access(candidate, X_OK) == 0;
			free(candidate);
			if (!executable)
				continue;
			result = visit(entry->d_name, context);
			if (result != 0)
				break;
		}
		if (closedir(directory) < 0 && result == 0)
			result = -1;
		if (result != 0)
			break;
	}
	free(copy);
	return result;
}

struct command_list {
	char (*names)[HELP_NAME_CAPACITY];
	size_t count;
	size_t capacity;
};

static int
collect_command_name(const char *name, void *context)
{
	struct command_list *list = context;

	for (size_t i = 0; i < list->count; i++)
		if (strcmp(list->names[i], name) == 0)
			return 0;
	if (list->count == list->capacity)
		return -2;
	size_t length = strlen(name);
	if (length >= HELP_NAME_CAPACITY)
		return -1;
	memcpy(list->names[list->count++], name, length + 1);
	return 0;
}

static void
print_help_commands(void)
{
	size_t internal_count = sizeof(internal_commands) / sizeof(internal_commands[0]);
	char (*names)[HELP_NAME_CAPACITY] = calloc(internal_count + HELP_COMMAND_LIMIT,
	    sizeof(*names));
	if (names == NULL) {
		u_puts("sh: cannot allocate command list\n");
		return;
	}
	for (size_t i = 0; i < internal_count; i++)
		strcpy(names[i], internal_commands[i]);
	struct command_list list = {names, internal_count, internal_count + HELP_COMMAND_LIMIT};
	int listing_error = visit_path_commands(collect_command_name, &list);
	uint64_t total = listing_error == 0 ? list.count : internal_count;
	uint64_t columns = 1, max_columns = 4;
	struct u_winsize terminal_size;

	while (columns * columns < total)
		columns++;
	if (u_ioctl(1, 0x5413U, &terminal_size) == 0) {
		max_columns = terminal_size.columns / HELP_COLUMN_WIDTH;
		if (max_columns == 0)
			max_columns = 1;
	}
	if (columns > max_columns)
		columns = max_columns;
	uint64_t rows = (total + columns - 1) / columns;
	u_puts("Available commands:\n");
	for (uint64_t row = 0; row < rows; row++) {
		for (uint64_t column = 0; column < columns; column++) {
			uint64_t index = row * columns + column;
			if (index >= total)
				continue;
			size_t length = strlen(names[index]);
			u_puts(names[index]);
			if (column + 1 < columns && index + 1 < total) {
				uint64_t spaces = length < HELP_COLUMN_WIDTH ?
				    HELP_COLUMN_WIDTH - length : 2;
				while (spaces-- != 0)
					u_puts(" ");
			}
		}
		u_puts("\n");
	}
	free(names);
	if (listing_error != 0)
		u_puts(listing_error == -2 ?
		    "sh: PATH has more than 128 external commands\n" :
		    "sh: cannot read PATH commands; listed internal commands only\n");
}

static void
command_external(const char *const argv[], int background,
	int interactive)
{
	const char *command = argv[0];
	long child;
	long wait_result;
	int status = 0;

	if (interactive && u_ioctl(0, U_TIOCCANON, 0) < 0) {
		print_error("tty mode", "canonical");
		return;
	}
	if (background) {
		reap_background_children();
		if (background_child_count == BACKGROUND_CHILD_LIMIT) {
			u_puts("sh: too many unreaped background commands\n");
			goto resume_editor;
		}
	}
	child = u_fork();
	if (child < 0) {
		print_error("fork", command);
		goto resume_editor;
	}
	if (child == 0) {
		(void)execvp(command, (char *const *)argv);
		if (errno == ENOENT || errno == ENOTDIR) {
			u_puts("sh: command not found: ");
			u_puts(command);
			u_puts("\n");
		} else {
			print_error("exec", command);
		}
		u_exit(127);
	}
	if (!background) {
		do {
			wait_result = u_waitpid((uint64_t)child, &status);
		} while (wait_result == -4);
		if (wait_result < 0)
			print_error("waitpid", command);
		else if (status != 0) {
		u_puts("sh: command exited status ");
		u_put_u64((status & 0x7f) != 0 ? 128U + (status & 0x7f) :
		    (unsigned int)status >> 8);
		u_puts("\n");
		}
	} else {
		background_children[background_child_count++] = (uint64_t)child;
	}
resume_editor:
	if (interactive && u_ioctl(0, U_TIOCSRAW, 0) < 0) {
		u_puts("sh: cannot restore interactive input mode\n");
		u_exit(1);
	}
}

static void
redraw_line(const char *line, size_t length, size_t cursor)
{
	u_puts("\r");
	u_puts(current_directory);
	u_puts("% ");
	if (cursor_visible && !xterm_cursor) {
		u_putn(line, cursor);
		u_puts("\033[7m");
		if (cursor < length)
			u_putn(line + cursor, 1);
		else
			u_puts(" ");
		u_puts("\033[0m");
		if (cursor < length)
			u_putn(line + cursor + 1, length - cursor - 1);
	} else {
		u_putn(line, length);
	}
	u_puts("\033[K");
	if (cursor_visible && !xterm_cursor && cursor == length) {
		u_puts("\033[1D");
	} else if (cursor < length) {
		u_puts("\033[");
		u_put_u64(length - cursor);
		u_puts("D");
	}
}

static void
load_history_line(char *line, size_t *length, size_t *cursor)
{
	const char *source;
	size_t i;
	if (history_position == history_count) {
		line[0] = '\0';
		*length = 0;
		*cursor = 0;
		return;
	}
	source = command_history[history_slot(history_position)];
	for (i = 0; i < LINE_CAPACITY - 1 && source[i] != '\0'; i++)
		line[i] = source[i];
	line[i] = '\0';
	*length = i;
	*cursor = i;
}

static void
add_completion(struct completion_candidate *candidates, size_t *count,
	const char *name, int is_directory, const char *prefix)
{
	size_t length = strlen(name);
	if (strncmp(name, prefix, strlen(prefix)) != 0 || length >= HELP_NAME_CAPACITY ||
	    *count == COMPLETION_LIMIT)
		return;
	for (size_t i = 0; i < *count; i++)
		if (strcmp(candidates[i].name, name) == 0)
			return;
	for (size_t i = 0; i <= length; i++)
		candidates[*count].name[i] = name[i];
	candidates[*count].is_directory = is_directory;
	(*count)++;
}

struct command_completion {
	struct completion_candidate *candidates;
	size_t *count;
	const char *prefix;
};

static int
collect_command_completion(const char *name, void *context)
{
	struct command_completion *completion = context;

	add_completion(completion->candidates, completion->count, name, 0,
	    completion->prefix);
	return 0;
}

static size_t
completion_common_prefix(const struct completion_candidate *candidates,
	size_t count)
{
	size_t length = strlen(candidates[0].name);
	for (size_t i = 1; i < count; i++) {
		size_t j = 0;
		while (j < length && candidates[i].name[j] != '\0' &&
		    candidates[0].name[j] == candidates[i].name[j])
			j++;
		length = j;
	}
	return length;
}

static void
insert_completion(char *line, size_t *length, size_t *cursor,
	const char *text, size_t text_length)
{
	if (text_length > LINE_CAPACITY - 1 - *length)
		text_length = LINE_CAPACITY - 1 - *length;
	if (text_length == 0)
		return;
	for (size_t i = *length; i > *cursor; i--)
		line[i + text_length - 1] = line[i - 1];
	for (size_t i = 0; i < text_length; i++)
		line[*cursor + i] = text[i];
	*length += text_length;
	*cursor += text_length;
	line[*length] = '\0';
}

static void
complete_line(char *line, size_t *length, size_t *cursor)
{
	/* Keep the bounded completion table off the process stack. */
	static struct completion_candidate candidates[COMPLETION_LIMIT];
	char word[LINE_CAPACITY];
	char directory_name[PATH_CAPACITY];
	size_t start = *cursor;
	size_t word_length;
	size_t slash = (size_t)-1;
	size_t count = 0;
	int command_position = 1;
	DIR *directory;
	struct dirent *entry;

	while (start != 0 && line[start - 1] != ' ' && line[start - 1] != '\t')
		start--;
	for (size_t i = 0; i < start; i++)
		if (line[i] != ' ' && line[i] != '\t')
			command_position = 0;
	word_length = *cursor - start;
	if (word_length >= sizeof(word))
		return;
	for (size_t i = 0; i < word_length; i++) {
		word[i] = line[start + i];
		if (word[i] == '/')
			slash = i;
	}
	word[word_length] = '\0';
	if (command_position && slash == (size_t)-1) {
		for (size_t i = 0; i < sizeof(internal_commands) /
		    sizeof(internal_commands[0]); i++)
			add_completion(candidates, &count, internal_commands[i], 0, word);
		struct command_completion completion = {candidates, &count, word};
		(void)visit_path_commands(collect_command_completion, &completion);
	} else {
		size_t prefix_length = slash == (size_t)-1 ? 0 : slash + 1;
		if (prefix_length == 0) {
			strcpy(directory_name, ".");
		} else {
			if (prefix_length >= sizeof(directory_name))
				return;
			memcpy(directory_name, word, prefix_length);
			directory_name[prefix_length] = '\0';
		}
		directory = opendir(directory_name);
		if (directory == NULL)
			return;
		int read_error = 0;
		for (;;) {
			errno = 0;
			entry = readdir(directory);
			if (entry == NULL) {
				read_error = errno;
				break;
			}
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			if (strncmp(entry->d_name, word + prefix_length,
			    word_length - prefix_length) == 0) {
				struct stat status;
				int is_directory = fstatat(dirfd(directory), entry->d_name,
				    &status, 0) == 0 && S_ISDIR(status.st_mode);
				add_completion(candidates, &count, entry->d_name,
				    is_directory, word + prefix_length);
			}
		}
		if (closedir(directory) < 0 || read_error != 0)
			count = 0;
		if (count != 0) {
			char replacement[PATH_CAPACITY];
			size_t replacement_length = 0;
			size_t common_length =
			    completion_common_prefix(candidates, count);
			size_t extra = count == 1 && candidates[0].is_directory ? 1U :
			    count == 1 && *cursor == *length ? 1U : 0U;
			if (prefix_length + common_length + extra >=
			    sizeof(replacement)) {
				redraw_line(line, *length, *cursor);
				return;
			}
			for (size_t i = 0; i < prefix_length; i++)
				replacement[replacement_length++] = word[i];
			for (size_t i = 0; i < common_length; i++)
				replacement[replacement_length++] = candidates[0].name[i];
			if (count == 1 && candidates[0].is_directory)
				replacement[replacement_length++] = '/';
			else if (count == 1 && *cursor == *length)
				replacement[replacement_length++] = ' ';
			if (replacement_length > word_length)
				insert_completion(line, length, cursor,
				    replacement + word_length,
				    replacement_length - word_length);
		}
		if (count > 1) {
			int restore_cursor = cursor_visible;
			cursor_visible = 0;
			redraw_line(line, *length, *cursor);
			cursor_visible = restore_cursor;
			u_puts("\r\n");
			for (size_t i = 0; i < count; i++) {
				u_puts(candidates[i].name);
				if (candidates[i].is_directory)
					u_puts("/");
				u_puts("  ");
			}
			u_puts("\r\n");
		}
		redraw_line(line, *length, *cursor);
		return;
	}
	if (count == 0)
		return;
	size_t common_length = completion_common_prefix(candidates, count);
	if (common_length > word_length)
		insert_completion(line, length, cursor,
		    candidates[0].name + word_length, common_length - word_length);
	if (count == 1 && *cursor == *length)
		insert_completion(line, length, cursor, " ", 1);
	if (count > 1) {
		int restore_cursor = cursor_visible;
		cursor_visible = 0;
		redraw_line(line, *length, *cursor);
		cursor_visible = restore_cursor;
		u_puts("\r\n");
		for (size_t i = 0; i < count; i++) {
			u_puts(candidates[i].name);
			u_puts("  ");
		}
		u_puts("\r\n");
	}
	redraw_line(line, *length, *cursor);
}

static int
read_command_line(char *line)
{
	size_t length = 0;
	size_t cursor = 0;
	line[0] = '\0';
	history_position = history_count;
	for (;;) {
		char character;
		reap_background_children();
		long amount = u_read(0, &character, 1);
		if (amount == -11) {
			cursor_visible = !cursor_visible;
			redraw_line(line, length, cursor);
			continue;
		}
		if (amount == 0)
			return 0;
		if (amount < 0)
			return (int)amount;
		cursor_visible = 1;
		if (character == '\n' || character == '\r') {
			line[length] = '\0';
			/* The cursor is a highlighted character. Remove the highlight
			 * from the screen before leaving the editor for command output. */
			cursor_visible = 0;
			redraw_line(line, length, length);
			u_puts("\n");
			remember_command(line);
			return 1;
		}
		if ((uint8_t)character == 0x1bU) {
			char sequence[2];
			if (u_read(0, sequence, sizeof(sequence)) !=
			    (long)sizeof(sequence) || sequence[0] != '[')
				continue;
			if (sequence[1] >= '0' && sequence[1] <= '9') {
				/* Ignore parameterized keys the line editor does not use. */
				char final = sequence[1];
				for (unsigned int i = 0; i < 8 && final < '@'; i++) {
					if (u_read(0, &final, 1) != 1)
						break;
				}
				continue;
			}
			switch (sequence[1]) {
			case 'A':
				if (history_position != 0) {
					history_position--;
					load_history_line(line, &length, &cursor);
					redraw_line(line, length, cursor);
				}
				break;
			case 'B':
				if (history_position < history_count) {
					history_position++;
					load_history_line(line, &length, &cursor);
					redraw_line(line, length, cursor);
				}
				break;
			case 'C':
				if (cursor < length) {
					cursor++;
					redraw_line(line, length, cursor);
				}
				break;
			case 'D':
				if (cursor != 0) {
					cursor--;
					redraw_line(line, length, cursor);
				}
				break;
			case 'H':
				cursor = 0;
				redraw_line(line, length, cursor);
				break;
			case 'F':
				cursor = length;
				redraw_line(line, length, cursor);
				break;
			default:
				break;
			}
			continue;
		}
		if (character == '\t') {
			complete_line(line, &length, &cursor);
			continue;
		}
		if (character == '\b' || (uint8_t)character == 0x7fU) {
			if (cursor != 0) {
				size_t i;
				for (i = cursor; i < length; i++)
					line[i - 1] = line[i];
				length--;
				cursor--;
				line[length] = '\0';
				redraw_line(line, length, cursor);
			}
			continue;
		}
		if ((uint8_t)character == 4U) {
			if (length == 0) {
				u_puts("\n");
				return 0;
			}
			if (cursor < length) {
				size_t i;
				for (i = cursor; i + 1 < length; i++)
					line[i] = line[i + 1];
				length--;
				line[length] = '\0';
				redraw_line(line, length, cursor);
			}
			continue;
		}
		if ((uint8_t)character == 3U) {
			u_puts("^C\n");
			return -2;
		}
		if ((uint8_t)character < 0x20U || (uint8_t)character > 0x7eU ||
		    length + 1 >= LINE_CAPACITY)
			continue;
		{
			size_t i;
			for (i = length; i > cursor; i--)
				line[i] = line[i - 1];
			line[cursor++] = character;
			length++;
			line[length] = '\0';
		}
		cursor_visible = 1;
		redraw_line(line, length, cursor);
	}
}

static int
run_command(char *line, int interactive)
{
	char *arguments[ARGUMENT_CAPACITY + 1];
	char *cursor = line;
	size_t count = 0;
	int background = 0;

	while (*cursor != '\0') {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		    *cursor == '\n') cursor++;
		if (*cursor == '\0') break;
		if (count == ARGUMENT_CAPACITY) {
			u_puts("sh: too many arguments\n");
			return 1;
		}
		arguments[count++] = cursor;
		while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
		    *cursor != '\r' && *cursor != '\n') cursor++;
		if (*cursor != '\0') *cursor++ = '\0';
	}
	if (count == 0)
		return 1;
	if (count == 1 && strcmp(arguments[0], "!(crash)") == 0) {
		if (u_kernel_crash() < 0)
			u_puts("sh: !(crash): permission denied\n");
		return 1;
	}
	if (strcmp(arguments[count - 1], "&") == 0) {
		arguments[--count] = NULL;
		background = 1;
	}
	if (count != 0 && strcmp(arguments[0], "exec") == 0) {
		for (size_t i = 1; i < count; i++)
			arguments[i - 1] = arguments[i];
		count--;
	}
	if (count == 0)
		return 1;
	if (strcmp(arguments[0], "help") == 0) {
		print_help_commands();
	} else if (strcmp(arguments[0], "history") == 0) {
		if (count != 1)
			u_puts("usage: history\n");
		else
			print_history();
	} else if (strcmp(arguments[0], "cd") == 0) {
		const char *destination = count > 1 ? arguments[1] : "/";

		if (chdir(destination) < 0 ||
		    getcwd(current_directory, sizeof(current_directory)) == NULL)
			print_error("cd", destination);
	} else if (strcmp(arguments[0], "exit") == 0) {
		return 0;
	} else {
		arguments[count] = 0;
		command_external((const char *const *)arguments, background,
		    interactive);
	}
	return 1;
}

int
main(int argc, char **argv)
{
	char line[LINE_CAPACITY];
	if (getenv("TERM") == NULL && setenv("TERM", "xterm", 0) != 0) {
		u_puts("OpenSillicon shell: cannot initialize TERM\n");
		return 1;
	}
	if (u_getcwd(current_directory, sizeof(current_directory)) < 0) {
		u_puts("OpenSillicon shell: getcwd failed\n");
		u_exit(1);
	}
	if (argc == 3 && strcmp(argv[1], "-c") == 0) {
		/* A command shell has no line editor and does not own the tty. */
		(void)run_command(argv[2], 0);
		u_exit(0);
	}
	if (argc == 2 && strcmp(argv[1], "--xterm") == 0) {
		xterm_cursor = 1;
	} else if (argc > 1) {
		u_puts("usage: sh [-c command] [--xterm]\n");
		u_exit(2);
	}
	long input_mode = u_ioctl(0, U_TIOCSRAW, 0);
	if (input_mode < 0) {
		u_puts("OpenSillicon shell: cannot enable interactive input mode, errno ");
		u_put_u64((uint64_t)-input_mode);
		u_puts("\n");
		u_exit(1);
	}
	bool waiting_for_keyboard = false;
	for (;;) {
		int result;
		if (!waiting_for_keyboard) {
			u_puts(current_directory);
			u_puts("# ");
			cursor_visible = 1;
			redraw_line(line, 0, 0);
		}
		result = read_command_line(line);
		if (result == 0) {
			(void)u_ioctl(0, U_TIOCCANON, 0);
			u_exit(0);
		}
		if (result < 0) {
			if (result == -2)
				continue;
			if (result == -19) {
				if (!waiting_for_keyboard) {
					u_puts("\nOpenSillicon shell: no keyboard controller available\n");
					waiting_for_keyboard = true;
				}
				const struct timespec delay = {.tv_sec = 1};
				(void)nanosleep(&delay, NULL);
				continue;
			}
			(void)u_puts("\n");
			print_error("read", "stdin");
			continue;
		}
		waiting_for_keyboard = false;
		if (!run_command(line, 1)) {
			(void)u_ioctl(0, U_TIOCCANON, 0);
			u_exit(0);
		}
	}
}

