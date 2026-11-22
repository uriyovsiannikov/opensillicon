#ifndef KERNEL_ICMP_ECHO_H
#define KERNEL_ICMP_ECHO_H

#include "abi/icmp.h"

/* Runs one bounded CycloneTCP IPv4 ICMP Echo transaction. */
int network_icmp_echo(struct os_icmp_echo_request *request);

#endif
