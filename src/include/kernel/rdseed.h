#ifndef KERNEL_RDSEED_H
#define KERNEL_RDSEED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_RDSEED_MAX_REQUEST 256U
#define KERNEL_RDSEED_RETRY_LIMIT 10U

enum kernel_rdseed_status {
	KERNEL_RDSEED_NOT_INITIALIZED = 0,
	KERNEL_RDSEED_UNSUPPORTED,
	KERNEL_RDSEED_SUPPORTED,
	KERNEL_RDSEED_AVAILABLE,
	KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE
};

enum kernel_rdseed_result {
	KERNEL_RDSEED_OK = 0,
	KERNEL_RDSEED_INVALID_ARGUMENT,
	KERNEL_RDSEED_NOT_READY,
	KERNEL_RDSEED_UNSUPPORTED_RESULT,
	KERNEL_RDSEED_TEMPORARY_FAILURE
};

struct kernel_rdseed_cpuid_result {
	uint32_t eax, ebx, ecx, edx;
};

/* One-shot CPUID detection. This only discovers architectural support; it
 * does not execute RDSEED or establish entropy quality. Status SUPPORTED
 * means an instruction attempt is permitted. AVAILABLE is set only after a
 * recent CF=1 result and does not guarantee the next request will succeed. */
bool kernel_rdseed_init(void);
enum kernel_rdseed_status kernel_rdseed_status(void);

/* Bounded, all-or-nothing output. Length is at most 256 bytes; output is
 * unchanged on failure. A zero-length request is a no-op after initialization
 * and feature detection. Not for interrupt context; current kernel is UP. */
enum kernel_rdseed_result kernel_rdseed_bytes(void *output, size_t length);

#endif
