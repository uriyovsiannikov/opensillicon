#ifndef _SYS_SYSCTL_H
#define _SYS_SYSCTL_H

#include <stddef.h>
#include <abi/sysctl.h>

#ifdef __cplusplus
extern "C" {
#endif
int sysctl(const int *, unsigned int, void *, size_t *, const void *, size_t);
int sysctlbyname(const char *, void *, size_t *, const void *, size_t);
int sysctlnametomib(const char *, int *, size_t *);
#ifdef __cplusplus
}
#endif
#endif
