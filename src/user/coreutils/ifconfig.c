#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include "abi/inet.h"

int
main(int argc, char **argv)
{
	struct os_ipv4_config config;
	char interface[32];
	size_t length = sizeof(interface);
	(void)argv;
	if (argc != 1) {
		fputs("usage: ifconfig\n", stderr);
		return 1;
	}
	if (sysctlbyname("net.interface", interface, &length, NULL, 0) < 0) {
		perror("ifconfig: interface");
		return 1;
	}
	length = sizeof(config);
	if (sysctlbyname("net.inet.ip.config", &config, &length, NULL, 0) < 0) {
		if (errno == EAGAIN)
			fprintf(stderr, "%s: IPv4 configuration pending (link down or DHCP lease unavailable)\n", interface);
		else
			perror("ifconfig: IPv4 configuration");
		return 1;
	}
	char address[INET_ADDRSTRLEN], netmask[INET_ADDRSTRLEN], gateway[INET_ADDRSTRLEN];
	if (!inet_ntop(AF_INET, config.address, address, sizeof(address)) ||
	    !inet_ntop(AF_INET, config.netmask, netmask, sizeof(netmask)) ||
	    !inet_ntop(AF_INET, config.gateway, gateway, sizeof(gateway))) {
		perror("ifconfig: IPv4 address");
		return 1;
	}
	if (printf("%s: inet %s netmask %s gateway %s", interface, address, netmask, gateway) < 0)
		return 1;
	uint8_t dns[4];
	char dns_text[INET_ADDRSTRLEN];
	length = sizeof(dns);
	if (sysctlbyname("net.inet.ip.dns", dns, &length, NULL, 0) == 0 &&
	    inet_ntop(AF_INET, dns, dns_text, sizeof(dns_text))) {
		if (printf(" dns %s\n", dns_text) < 0)
			return 1;
	} else if (fputs(" dns unavailable\n", stdout) == EOF) {
		return 1;
	}
	return fflush(stdout) == EOF;
}
