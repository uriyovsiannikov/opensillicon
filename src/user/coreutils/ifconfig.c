#include "../include/os.h"

static void
print_ipv4(const uint8_t address[4])
{
	for (size_t i = 0; i < 4; i++) {
		if (i != 0)
			u_puts(".");
		u_put_u64(address[i]);
	}
}

static void
print_interface(void)
{
	char name[32];
	long length = 0;
	long fd = u_open("/proc/netif", OS_O_RDONLY);
	if (fd >= 0) {
		length = u_read((uint64_t)fd, name, sizeof(name));
		u_close((uint64_t)fd);
	}
	if (length > 0 && (size_t)length < sizeof(name))
		u_putn(name, (size_t)length);
	else
		u_puts("ifconfig");
}

int
user_main(uint64_t argc, char **argv)
{
	struct u_ipv4_config config;
	uint8_t dns_server[4];
	long result;
	(void)argv;
	if (argc != 1) {
		u_puts("usage: ifconfig\n");
		return 1;
	}
	result = u_network_ipv4_config(&config);
	if (result == -11) {
		print_interface();
		u_puts(": IPv4 configuration pending (link down or DHCP lease unavailable)\n");
		return 1;
	}
	if (result < 0) {
		u_puts("ifconfig: unable to read network configuration (errno ");
		u_put_u64((uint64_t)-result);
		u_puts(")\n");
		return 1;
	}
	print_interface();
	u_puts(": inet ");
	print_ipv4(config.address);
	u_puts(" netmask ");
	print_ipv4(config.netmask);
	u_puts(" gateway ");
	print_ipv4(config.gateway);
	if (u_network_dns_server(dns_server) == 0) {
		u_puts(" dns ");
		print_ipv4(dns_server);
	} else {
		u_puts(" dns unavailable");
	}
	u_puts("\n");
	return 0;
}
