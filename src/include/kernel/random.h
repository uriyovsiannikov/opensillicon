#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <stdbool.h>
#include <stddef.h>

#define KERNEL_RANDOM_MAX_REQUEST 256U

enum kernel_random_source_status {
	KERNEL_RANDOM_NOT_INITIALIZED = 0,
	KERNEL_RANDOM_UNSUPPORTED,
	KERNEL_RANDOM_AVAILABLE,
	KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE,
	KERNEL_RANDOM_PERMANENTLY_FAILED
};

/* Legacy raw RDRAND byte source. UNSUPPORTED means CPUID lacks the feature;
 * TEMPORARILY_UNAVAILABLE means the fixed ten-attempt bound saw no CF=1.
 * Such a failure is retryable and does not permanently latch the source.
 * PERMANENTLY_FAILED is retained for a separately established fatal provider
 * fault; current RDRAND instruction semantics expose no such runtime signal.
 * This is not a trusted entropy pool or CSPRNG. Output is unchanged on error.
 * These calls are not for interrupt context and are UP-only, not SMP-safe. */
bool kernel_random_init(void);
/* Reports only the raw RDRAND provider lifecycle; AVAILABLE is not a trust
 * decision or cryptographic-readiness indication. */
enum kernel_random_source_status kernel_random_status(void);
bool kernel_random_bytes(void *output, size_t length);

#endif
