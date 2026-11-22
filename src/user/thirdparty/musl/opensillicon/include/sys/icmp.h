#ifndef _SYS_ICMP_H
#define _SYS_ICMP_H

#include <abi/icmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OpenSillicon extension: one bounded IPv4 ICMP Echo transaction. */
int os_icmp_echo(struct os_icmp_echo_request *);

#ifdef __cplusplus
}
#endif

#endif
