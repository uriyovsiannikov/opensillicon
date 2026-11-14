#include <sys/disk.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "include/os.h"
#include "service.h"

#define SERVICE_DIR "/etc/service.d"
#define MAX_SERVICES 32
#define CONFIG_SIZE 2048
#define LOOP_TICKS 5
#define RESTART_DELAY_MS 1000
#define STOP_TIMEOUT_MS 3000
#define REQUEST_TIMEOUT_MS 500

enum restart_policy { RESTART_NEVER, RESTART_FAILURE, RESTART_ALWAYS };

struct service {
	char name[SERVICE_NAME_SIZE];
	char command[1024];
	size_t argc;
	uint32_t uid, gid, priority;
	enum restart_policy policy;
	bool enabled, console;
	bool pending, stopping, has_status;
	pid_t pid;
	int status;
	uint64_t deadline;
};

static struct service services[MAX_SERVICES];
static size_t service_count;
static int listener = -1;
static int client = -1;
static struct service saved_console;
static struct service *installer_console;

static void configure_installer_console(void)
{
 char command[1024];int fd=open("/proc/cmdline",O_RDONLY);
 if(fd<0)return;
 ssize_t n=read(fd,command,sizeof(command)-1);close(fd);
 if(n<=0)return;
 command[n]=0;
 int requested=0;
 for(char *p=command;*p;) {
  while(*p==' '||*p=='\t'||*p=='\n')p++;
  char *end=p;while(*end && *end!=' ' && *end!='\t' && *end!='\n')end++;
  if(end-p==9 && memcmp(p,"installer",9)==0)requested=1;
  p=end;
 }
 if(!requested)return;
 fd=open("/dev/ram0",O_RDONLY);if(fd<0)return;
 struct os_disk_partinfo info;
 int live=ioctl(fd,DIOCGPARTINFO,&info)==0 && (info.flags&OS_DISK_MOUNTED);
 close(fd);if(!live)return;
 size_t count=0;
 for(size_t i=0;i<service_count;i++)if(services[i].enabled && services[i].console){installer_console=&services[i];count++;}
 if(count!=1){installer_console=NULL;return;}
 saved_console=*installer_console;
 memset(installer_console->command,0,sizeof(installer_console->command));
 strcpy(installer_console->command,"/bin/bsdinstall");installer_console->argc=1;
 installer_console->uid=installer_console->gid=0;
 installer_console->policy=RESTART_NEVER;
}

static void
report(const char *name, const char *message)
{
	u_puts("init: ");
	u_puts(name);
	u_puts(": ");
	u_puts(message);
	u_puts("\n");
}

static bool
name_valid(const char *name)
{
	if (*name == '\0')
		return false;
	for (; *name != '\0'; name++) {
		char c = *name;
		if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
		    !(c >= '0' && c <= '9') && c != '_' && c != '-')
			return false;
	}
	return true;
}

static bool
parse_number(const char *value, uint32_t *number)
{
	uint32_t result = 0;

	if (*value == '\0')
		return false;
	for (; *value != '\0'; value++) {
		if (*value < '0' || *value > '9')
			return false;
		uint32_t digit = (uint32_t)(*value - '0');
		if (result > (UINT32_MAX - 1U - digit) / 10U)
			return false;
		result = result * 10U + digit;
	}
	*number = result;
	return true;
}

static bool
parse_command(struct service *service, const char *value)
{
	char *p = service->command;

	if (*value != '/' || strlen(value) >= sizeof(service->command))
		return false;
	while (*value != '\0') {
		if (++service->argc > U_EXEC_ARG_MAX)
			return false;
		while (*value != '\0' && *value != ' ' && *value != '\t')
			*p++ = *value++;
		*p++ = '\0';
		while (*value == ' ' || *value == '\t')
			value++;
	}
	return true;
}

static bool
parse_config(struct service *service, char *line)
{
	static const char *const keys[] = {
		"command", "uid", "gid", "priority", "restart", "enabled", "console"
	};
	unsigned int seen = 0;

	service->priority = 100;
	service->enabled = true;
	while (*line != '\0') {
		char *next = strchr(line, '\n');
		if (next != NULL)
			*next++ = '\0';
		else
			next = line + strlen(line);
		if (*line != '\0' && *line != '#') {
			char *value = strchr(line, '=');
			size_t key = 0;

			if (value == NULL)
				return false;
			*value++ = '\0';
			while (key < sizeof(keys) / sizeof(keys[0]) &&
			    strcmp(line, keys[key]) != 0)
				key++;
			if (key == sizeof(keys) / sizeof(keys[0]) ||
			    (seen & (1U << key)) != 0)
				return false;
			seen |= 1U << key;
			switch (key) {
			case 0:
				if (!parse_command(service, value))
					return false;
				break;
			case 1:
				if (!parse_number(value, &service->uid))
					return false;
				break;
			case 2:
				if (!parse_number(value, &service->gid))
					return false;
				break;
			case 3:
				if (!parse_number(value, &service->priority))
					return false;
				break;
			case 4:
				if (strcmp(value, "always") == 0)
					service->policy = RESTART_ALWAYS;
				else if (strcmp(value, "on-failure") == 0)
					service->policy = RESTART_FAILURE;
				else if (strcmp(value, "never") != 0)
					return false;
				break;
			case 5:
			case 6:
				if (strcmp(value, "yes") != 0 && strcmp(value, "no") != 0)
					return false;
				if (key == 5)
					service->enabled = strcmp(value, "yes") == 0;
				else
					service->console = strcmp(value, "yes") == 0;
				break;
			}
		}
		line = next;
	}
	return (seen & 7U) == 7U;
}

static bool
read_config(const char *filename, struct service *service)
{
	char path[sizeof(SERVICE_DIR) + SERVICE_NAME_SIZE + sizeof(".conf")];
	char data[CONFIG_SIZE + 1];
	struct stat info;
	ssize_t amount = 0;
	size_t used = 0;

	snprintf(path, sizeof(path), "%s/%s", SERVICE_DIR, filename);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;
	if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) ||
	    info.st_uid != 0 || (info.st_mode & 022) != 0) {
		close(fd);
		return false;
	}
	while (used < sizeof(data) &&
	    (amount = read(fd, data + used, sizeof(data) - used)) > 0)
		used += (size_t)amount;
	close(fd);
	if (used == sizeof(data) || amount < 0 || memchr(data, 0, used) != NULL)
		return false;
	data[used] = '\0';
	return parse_config(service, data);
}

static int
compare_services(const void *left, const void *right)
{
	const struct service *a = left;
	const struct service *b = right;

	if (a->priority != b->priority)
		return a->priority < b->priority ? -1 : 1;
	return strcmp(a->name, b->name);
}

static void
load_services(void)
{
	struct stat info;
	struct dirent *entry;

	if (stat(SERVICE_DIR, &info) < 0) {
		if (errno == ENOENT) {
			/* Images built before service configurations were introduced. */
			struct service *console = &services[service_count++];
			strcpy(console->name, "console");
			parse_command(console, "/bin/sh");
			console->uid = console->gid = 1000;
			console->console = console->enabled = true;
			console->policy = RESTART_ALWAYS;
		} else
			report(SERVICE_DIR, "cannot check directory");
		return;
	}
	if (!S_ISDIR(info.st_mode) || info.st_uid != 0 || (info.st_mode & 022) != 0) {
		report(SERVICE_DIR, "expected a root owned directory");
		return;
	}
	DIR *dir = opendir(SERVICE_DIR);
	if (dir == NULL) {
		report(SERVICE_DIR, "cannot open directory");
		return;
	}
	while ((entry = readdir(dir)) != NULL) {
		size_t length = strlen(entry->d_name);
		struct service service = {0};

		if (length <= 5 || length - 5 >= sizeof(service.name) ||
		    strcmp(entry->d_name + length - 5, ".conf") != 0)
			continue;
		memcpy(service.name, entry->d_name, length - 5);
		if (!name_valid(service.name) || !read_config(entry->d_name, &service)) {
			report(entry->d_name, "invalid service configuration");
			continue;
		}
		if (service_count == MAX_SERVICES) {
			report(SERVICE_DIR, "too many services");
			break;
		}
		services[service_count++] = service;
	}
	closedir(dir);
	qsort(services, service_count, sizeof(services[0]), compare_services);
}

static uint64_t
now_ms(void)
{
	struct timespec now = {0};

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void
schedule_after_exit(struct service *service, int status, uint64_t now)
{
	service->pid = 0;
	service->has_status = true;
	service->status = status;
	if (!service->stopping) {
		service->pending = service->policy == RESTART_ALWAYS ||
		    (service->policy == RESTART_FAILURE && status != 0);
		service->deadline = now + RESTART_DELAY_MS;
	} else {
		service->deadline = now;
	}
	service->stopping = false;
}

static void
start_service(struct service *service, uint64_t now)
{
	service->pending = false;
	pid_t pid = fork();

	if (pid < 0) {
		report(service->name, "fork failed");
		schedule_after_exit(service, 125, now);
		return;
	}
	if (pid == 0) {
		const char *argv[U_EXEC_ARG_MAX + 1];
		const char *argument = service->command;

		if (listener >= 0)
			close(listener);
		if (client >= 0)
			close(client);
		if (!service->console) {
			int input = open("/dev/null", O_RDONLY);
			if (input < 0 || dup2(input, 0) < 0)
				_exit(126);
			if (input != 0)
				close(input);
		}
		if (setgid(service->gid) < 0 || setuid(service->uid) < 0) {
			report(service->name, "cannot set service credentials");
			_exit(126);
		}
		for (size_t i = 0; i < service->argc; i++) {
			argv[i] = argument;
			argument += strlen(argument) + 1;
		}
		argv[service->argc] = NULL;
		u_exec_path_argv(argv[0], service->argc, argv);
		report(service->name, "exec failed");
		_exit(127);
	}
	service->pid = pid;
}

static void
reap_services(uint64_t now)
{
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		for (size_t i = 0; i < service_count; i++) {
			struct service *service = &services[i];
			if (service->pid == pid) {
				if (service == installer_console) {
					*service = saved_console; installer_console = NULL;
					service->pending = service->enabled; service->deadline = now;
					report("installer", "returned; restoring user console"); break;
				}
				schedule_after_exit(service, WEXITSTATUS(status), now);
				break;
			}
		}
	}
}

static void
advance_services(uint64_t now)
{
	for (size_t i = 0; i < service_count; i++) {
		struct service *service = &services[i];

		if (now < service->deadline)
			continue;
		if (service->stopping) {
			(void)kill(service->pid, SIGKILL);
			service->deadline = now + STOP_TIMEOUT_MS;
		} else if (service->pending && service->pid == 0) {
			start_service(service, now);
		}
	}
}

static int
open_listener(void)
{
	const struct sockaddr_un address = { .sun_family = AF_UNIX,
	    .sun_path = SERVICE_SOCKET };
	struct stat info;

	if (mkdir("/var/run", 0755) < 0 && errno != EEXIST)
		return -1;
	if (stat("/var/run", &info) < 0 || !S_ISDIR(info.st_mode) ||
	    info.st_uid != 0 || (info.st_mode & 022) != 0)
		return -1;
	if (stat(SERVICE_SOCKET, &info) == 0) {
		if (!S_ISSOCK(info.st_mode) || info.st_uid != 0 ||
		    unlink(SERVICE_SOCKET) < 0)
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	mode_t mask = umask(0111);
	int result = bind(fd, (const struct sockaddr *)&address, sizeof(address));
	umask(mask);
	if (result < 0 || listen(fd, 4) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
format_status(char *out, size_t size, const struct service *service)
{
	const char *state = service->stopping ? "stopping" :
	    service->pid != 0 ? "running" :
	    service->pending ? "pending" : "stopped";

	return snprintf(out, size, "%s: %s pid=%d last-exit=%d\n",
	    service->name, state, service->pid,
	    service->has_status ? service->status : -1);
}

static const char *
run_request(const struct service_request *request, uint64_t now,
    char *response, size_t capacity)
{
	struct service *service = NULL;
	struct ucred peer;
	socklen_t length = sizeof(peer);

	if (memchr(request->name, 0, sizeof(request->name)) == NULL)
		return "invalid name\n";
	if (request->operation == SERVICE_LIST) {
		size_t used = 0;
		for (size_t i = 0; i < service_count; i++)
			used += (size_t)format_status(response + used,
			    capacity - used, &services[i]);
		return NULL;
	}
	for (size_t i = 0; i < service_count; i++) {
		if (strcmp(request->name, services[i].name) == 0) {
			service = &services[i];
			break;
		}
	}
	if (service == NULL)
		return "unknown service\n";
	if (request->operation == SERVICE_STATUS) {
		format_status(response, capacity, service);
		return NULL;
	}
	if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) < 0 ||
	    length != sizeof(peer) || peer.uid != 0)
		return "root required\n";
	switch (request->operation) {
	case SERVICE_START:
		if (service->pid == 0 || service->stopping) {
			service->pending = true;
			if (!service->stopping)
				service->deadline = now;
		}
		break;
	case SERVICE_STOP:
	case SERVICE_RESTART:
		if (service->pid != 0 && !service->stopping) {
			if (kill(service->pid, SIGTERM) < 0 && errno != ESRCH)
				return "cannot signal service\n";
			service->stopping = true;
			service->deadline = now + STOP_TIMEOUT_MS;
		}
		service->pending = request->operation == SERVICE_RESTART;
		if (!service->stopping)
			service->deadline = now;
		break;
	default:
		return "unknown operation\n";
	}
	strcpy(response, "accepted\n");
	return NULL;
}

static void
serve_control(uint64_t now)
{
	static struct service_request request;
	static size_t used;
	static uint64_t deadline;

	if (client < 0) {
		client = accept(listener, NULL, NULL);
		if (client < 0)
			return;
		if (fcntl(client, F_SETFL, O_NONBLOCK) < 0)
			goto done;
		used = 0;
		deadline = now + REQUEST_TIMEOUT_MS;
	}
	if (now >= deadline)
		goto done;
	ssize_t amount = read(client, (char *)&request + used, sizeof(request) - used);
	if (amount < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	if (amount <= 0)
		goto done;
	used += (size_t)amount;
	if (used < sizeof(request))
		return;

	/* One status byte followed by text. A fresh Unix socket has space
	 * for the whole reply (at most 96 bytes per service). */
	char response[1 + MAX_SERVICES * 96] = {0};
	const char *error = run_request(&request, now, response + 1,
	    sizeof(response) - 1);
	if (error != NULL) {
		response[0] = 1;
		strcpy(response + 1, error);
	}
	(void)write(client, response, 1 + strlen(response + 1));
done:
	close(client);
	client = -1;
}

_Noreturn void
service_manager(void)
{
	load_services();
	configure_installer_console();
	listener = open_listener();
	if (listener < 0)
		report("svctl", "cannot open control socket");
	for (size_t i = 0; i < service_count; i++)
		services[i].pending = services[i].enabled;
	advance_services(now_ms());

	for (;;) {
		uint64_t now = now_ms();
		reap_services(now);
		if (listener >= 0)
			serve_control(now);
		advance_services(now);
		(void)u_sleep_ticks(LOOP_TICKS);
	}
}
