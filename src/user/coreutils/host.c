#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	struct addrinfo hints = {.ai_family = AF_INET};
	struct addrinfo *result = NULL;
	int found = 0;

	if (argc == 2 && !strcmp(argv[1], "help")) {
		puts("usage: host help | host <name>");
		return fflush(stdout) == EOF;
	}
	if (argc != 2) {
		fputs("usage: host help | host <name>\n", stderr);
		return 2;
	}
	int error = getaddrinfo(argv[1], NULL, &hints, &result);
	if (error != 0) {
		fprintf(stderr, "host: %s\n", gai_strerror(error));
		return 1;
	}
	for (struct addrinfo *entry = result; entry != NULL;
	    entry = entry->ai_next) {
		const struct sockaddr_in *address = (const struct sockaddr_in *)entry->ai_addr;
		char text[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == NULL ||
		    printf("%s has address %s\n", argv[1], text) < 0) {
			freeaddrinfo(result);
			return 1;
		}
		found = 1;
	}
	freeaddrinfo(result);
	return !found || fflush(stdout) == EOF;
}
