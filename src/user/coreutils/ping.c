#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/icmp.h>
#include <unistd.h>

static const uint8_t ping_payload[] = "OpenSillicon ICMP Echo";

static int
ping_one(const uint8_t destination[4], uint16_t sequence)
{
	struct os_icmp_echo_request request = {0};
	memcpy(request.destination, destination, sizeof(request.destination));
	request.identifier = (uint16_t)getpid();
	request.sequence = sequence;
	request.payload_length = sizeof(ping_payload) - 1;
	memcpy(request.payload, ping_payload, request.payload_length);
	if (os_icmp_echo(&request) < 0) {
		if (errno == ETIMEDOUT)
			fputs("ping: request timed out\n", stderr);
		else if (errno == EHOSTUNREACH)
			fputs("ping: no IPv4 route (DHCP lease or gateway unavailable)\n", stderr);
		else if (errno == EAGAIN)
			fputs("ping: another ICMP Echo request is already active\n", stderr);
		else
			fprintf(stderr, "ping: %s\n", strerror(errno));
		return 1;
	}
	if (memcmp(request.reply_source, destination, sizeof(request.reply_source))) {
		fputs("ping: kernel returned an unexpected reply source\n", stderr);
		return 1;
	}
	char address[INET_ADDRSTRLEN];
	if (!inet_ntop(AF_INET, request.reply_source, address, sizeof(address))) {
		perror("ping: reply address");
		return 1;
	}
	return printf("ping: reply from %s sequence=%u elapsed=%" PRIu64 ".%03" PRIu64 " ms\n",
	    address, (unsigned)sequence, request.elapsed_ns / 1000000U,
	    request.elapsed_ns / 1000U % 1000U) < 0;
}

int
main(int argc, char **argv)
{
	uint8_t destination[4];
	unsigned long count = 1;
	int failures = 0;

	if (argc == 2 && !strcmp(argv[1], "help")) {
		puts("usage: ping help | ping <IPv4> [count 1-4]");
		return fflush(stdout) == EOF;
	}
	if ((argc != 2 && argc != 3) || inet_pton(AF_INET, argv[1], destination) != 1)
		goto usage;
	if (argc == 3) {
		char *end;
		if (argv[2][0] < '0' || argv[2][0] > '9')
			goto usage;
		errno = 0;
		count = strtoul(argv[2], &end, 10);
		if (errno != 0 || *end != '\0' || count < 1 || count > 4)
			goto usage;
	}
	for (uint16_t sequence = 1; sequence <= count; sequence++)
		failures += ping_one(destination, sequence);
	if (printf("ping: %lu replies, %u failures\n", count - (unsigned)failures,
	    (unsigned)failures) < 0 || fflush(stdout) == EOF)
		return 1;
	return failures != 0;
usage:
	fputs("usage: ping help | ping <IPv4> [count 1-4]\n", stderr);
	return 2;
}
