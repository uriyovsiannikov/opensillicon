#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static const char *const default_environment[]={"TERM=xterm",NULL};
static const char *const *shell_environment=default_environment;
static char current_directory[PATH_CAPACITY] = "/";
static char command_history[HISTORY_CAPACITY][LINE_CAPACITY];
static size_t history_count;
static size_t history_next;
static size_t history_position;
static uint64_t background_children[BACKGROUND_CHILD_LIMIT];
static size_t background_child_count;
static int cursor_visible = 1;
static int xterm_cursor;

static int
resolve(const char *path, char *absolute)
{
	return u_normalize_path(current_directory, path, absolute, PATH_CAPACITY);
}

static size_t
history_slot(size_t position)
{
	return (history_next + HISTORY_CAPACITY - history_count + position) %
	    HISTORY_CAPACITY;
}

static int
same_text(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static void
remember_command(const char *line)
{
	size_t i;
	if (line[0] == '\0')
		return;
	if (history_count != 0 && same_text(
	    command_history[history_slot(history_count - 1)], line))
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

static long
read_bin_commands(char names[][HELP_NAME_CAPACITY], size_t capacity)
{
	DIR *directory = opendir("/bin");
	if (directory == NULL)
		return -1;
	long count = 0;
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0)
				count = -1;
			break;
		}
		if (!is_executable_entry(directory, entry))
			continue;
		if ((size_t)count == capacity) {
			count = -2;
			break;
		}
		size_t length = strlen(entry->d_name);
		if (length >= HELP_NAME_CAPACITY) {
			count = -1;
			break;
		}
		memcpy(names[count++], entry->d_name, length + 1);
	}
	if (closedir(directory) < 0)
		return -1;
	return count;
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
	long external_count = read_bin_commands(names + internal_count, HELP_COMMAND_LIMIT);
	long listing_error = external_count < 0 ? external_count : 0;
	uint64_t total = internal_count + (external_count < 0 ? 0 : (uint64_t)external_count);
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
		    "sh: /bin has more than 128 executable entries\n" :
		    "sh: cannot read /bin; listed internal commands only\n");
}

static void
command_external(const char *const argv[], int background,
	int interactive)
{
	char path[PATH_CAPACITY];
	const char *command = argv[0];
	long child;
	long wait_result;
	int status = 0;

	if (command[0] == '/') {
		if (u_strlen(command) >= sizeof(path)) {
			print_error("exec", command);
			return;
		}
		for (size_t i = 0; command[i] != '\0'; i++)
			path[i] = command[i];
		path[u_strlen(command)] = '\0';
	} else {
		int has_slash = 0;
		for (size_t i = 0; command[i] != '\0'; i++)
			if (command[i] == '/')
				has_slash = 1;
		if (has_slash) {
			if (!resolve(command, path)) {
				print_error("exec", command);
				return;
			}
		} else if (!u_find_command(command, path, sizeof(path))) {
			u_puts("sh: command not found: ");
			u_puts(command);
			u_puts("\n");
			return;
		}
	}
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
		(void)execve(path, (char *const *)argv, (char *const *)shell_environment);
		print_error("exec", path);
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

static int
has_prefix(const char *text, const char *prefix)
{
	size_t index = 0;
	while (prefix[index] != '\0' && text[index] == prefix[index])
		index++;
	return prefix[index] == '\0';
}

static void
add_completion(struct completion_candidate *candidates, size_t *count,
	const char *name, int is_directory, const char *prefix)
{
	size_t length = u_strlen(name);
	if (!has_prefix(name, prefix) || length >= HELP_NAME_CAPACITY ||
	    *count == COMPLETION_LIMIT)
		return;
	for (size_t i = 0; i < *count; i++)
		if (u_streq(candidates[i].name, name))
			return;
	for (size_t i = 0; i <= length; i++)
		candidates[*count].name[i] = name[i];
	candidates[*count].is_directory = is_directory;
	(*count)++;
}

static size_t
completion_common_prefix(const struct completion_candidate *candidates,
	size_t count)
{
	size_t length = u_strlen(candidates[0].name);
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
	char directory_path[PATH_CAPACITY];
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
		static const char *const directories[] = { "/bin", "/usr/bin" };
		for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
			directory = opendir(directories[i]);
			if (directory != NULL) {
				for (;;) {
					entry = readdir(directory);
					if (entry == NULL)
						break;
					if (is_executable_entry(directory, entry))
						add_completion(candidates, &count, entry->d_name, 0, word);
				}
				(void)closedir(directory);
			}
		}
	} else {
		size_t prefix_length = slash == (size_t)-1 ? 0 : slash + 1;
		if (slash == (size_t)-1) {
			if (!resolve(".", directory_path))
				return;
		} else {
			if (prefix_length >= sizeof(directory_name))
				return;
			for (size_t i = 0; i < prefix_length; i++)
				directory_name[i] = word[i];
			directory_name[prefix_length] = '\0';
			if (prefix_length == 1 && directory_name[0] == '/') {
				directory_name[0] = '/';
				directory_name[1] = '\0';
			} else {
				directory_name[prefix_length - 1] = '\0';
				if (directory_name[0] == '\0')
					directory_name[0] = '.', directory_name[1] = '\0';
			}
			if (!resolve(directory_name, directory_path))
				return;
		}
		directory = opendir(directory_path);
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
			if (has_prefix(entry->d_name, word + prefix_length)) {
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
	if (count == 1 && u_streq(arguments[0], "!(crash)")) {
		if (u_kernel_crash() < 0)
			u_puts("sh: !(crash): permission denied\n");
		return 1;
	}
	if (u_streq(arguments[count - 1], "&")) {
		arguments[--count] = NULL;
		background = 1;
	}
	if (count != 0 && u_streq(arguments[0], "exec")) {
		for (size_t i = 1; i < count; i++)
			arguments[i - 1] = arguments[i];
		count--;
	}
	if (count == 0)
		return 1;
	if (u_streq(arguments[0], "help")) {
		print_help_commands();
	} else if (u_streq(arguments[0], "history")) {
		if (count != 1)
			u_puts("usage: history\n");
		else
			print_history();
	} else if (u_streq(arguments[0], "cd")) {
		char path[PATH_CAPACITY];
		const char *destination = count > 1 ? arguments[1] : "/";

		if (!resolve(destination, path) || u_chdir(path) < 0 ||
		    u_getcwd(path, sizeof(path)) < 0) {
			print_error("cd", destination);
		} else {
			for (size_t i = 0; path[i] != '\0'; i++)
				current_directory[i] = path[i];
			current_directory[u_strlen(path)] = '\0';
		}
	} else if (u_streq(arguments[0], "exit")) {
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
	extern char **environ;

	if (environ != NULL && environ[0] != NULL)
		shell_environment = (const char *const *)environ;
	if (u_getcwd(current_directory, sizeof(current_directory)) < 0) {
		u_puts("OpenSillicon shell: getcwd failed\n");
		u_exit(1);
	}
	if (argc == 3 && u_streq(argv[1], "-c")) {
		/* A command shell has no line editor and does not own the tty. */
		(void)run_command(argv[2], 0);
		u_exit(0);
	}
	if (argc == 2 && u_streq(argv[1], "--xterm")) {
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
				(void)u_sleep_ticks(100);
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

