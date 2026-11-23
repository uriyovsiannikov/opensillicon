#ifndef KERNEL_VIRTIO_RNG_H
#define KERNEL_VIRTIO_RNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci.h"
#include "kernel/dma.h"

#define VIRTIO_RNG_MAX_READ 256U
#define VIRTIO_RNG_POLL_LIMIT 1000000U
#define VIRTIO_RNG_DEVICE_VENDOR 0x1af4U
#define VIRTIO_RNG_LEGACY_DEVICE 0x1005U

enum virtio_rng_result {
	VIRTIO_RNG_OK = 0,
	VIRTIO_RNG_UNAVAILABLE,
	VIRTIO_RNG_INVALID_ARGUMENT,
	VIRTIO_RNG_BUSY,
	VIRTIO_RNG_TIMEOUT,
	VIRTIO_RNG_MALFORMED_COMPLETION,
	VIRTIO_RNG_TRANSPORT_FAILURE
};

/* Candidate identity for the only supported transport: QEMU's transitional
 * PCI I/O entropy device (1af4:1005). This does not validate BAR assignment
 * or imply source trust/entropy quality. */
bool virtio_rng_matches_device(const struct pci_device_info *device);
enum virtio_rng_result virtio_rng_init(
	const struct pci_device_info *device);
bool virtio_rng_available(void);
/* Single outstanding caller; bounded polling. On every non-OK result,
 * *received is zero and output is unchanged. A successful read can be short.
 * capacity zero is a successful no-op; capacity is capped at 256 bytes. */
enum virtio_rng_result virtio_rng_read(void *output, size_t capacity,
	size_t *received);
/* Stops requests and releases DMA only after reset and bus-master shutdown
 * are verified. Otherwise it leaves device-visible allocations retained. */
enum virtio_rng_result virtio_rng_shutdown(void);

#endif
