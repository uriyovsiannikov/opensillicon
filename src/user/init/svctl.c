#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "service.h"

int
main(int argc, char **argv)
{
	static const struct {
		const char *name;
		enum service_operation operation;
	} commands[] = {
		{ "list", SERVICE_LIST }, { "status", SERVICE_STATUS },
		{ "start", SERVICE_START }, { "stop", SERVICE_STOP },
		{ "restart", SERVICE_RESTART }
	};
	const struct sockaddr_un address = { .sun_family = AF_UNIX,
	    .sun_path = SERVICE_SOCKET };
	struct service_request request = {0};
	char buffer[512];
	size_t sent = 0;
	int result = 1;

	if (argc == 2 || argc == 3) {
		for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
			if (strcmp(argv[1], commands[i].name) == 0 &&
			    argc == (commands[i].operation == SERVICE_LIST ? 2 : 3)) {
				request.operation = commands[i].operation;
				break;
			}
		}
	}
	if (request.operation == 0 ||
	    (argc == 3 && strlen(argv[2]) >= sizeof(request.name))) {
		fprintf(stderr, "usage: svctl list | svctl {status|start|stop|restart} service\n");
		return 2;
	}
	if (argc == 3)
		strcpy(request.name, argv[2]);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("svctl: socket");
		return 1;
	}
	if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("svctl: connect");
		goto done;
	}
	while (sent < sizeof(request)) {
		ssize_t amount = write(fd, (const char *)&request + sent,
		    sizeof(request) - sent);
		if (amount < 0 && errno == EINTR)
			continue;
		if (amount <= 0) {
			perror("svctl: write");
			goto done;
		}
		sent += (size_t)amount;
	}

	unsigned char code;
	ssize_t amount;
	do {
		amount = read(fd, &code, 1);
	} while (amount < 0 && errno == EINTR);
	if (amount != 1) {
		fprintf(stderr, "svctl: no response from init\n");
		goto done;
	}
	for (;;) {
		amount = read(fd, buffer, sizeof(buffer));
		if (amount < 0 && errno == EINTR)
			continue;
		if (amount < 0) {
			perror("svctl: read");
			goto done;
		}
		if (amount == 0)
			break;
		if (fwrite(buffer, 1, (size_t)amount, code == 0 ? stdout : stderr)
		    != (size_t)amount)
			goto done;
	}
	result = code != 0;
done:
	close(fd);
	return result;
}
