#ifndef KERNEL_HDA_PROBE_H
#define KERNEL_HDA_PROBE_H
#include <stddef.h>
/* Explicit, privileged diagnostic. Results are cached until reboot. */
int hda_probe_snapshot(char *buffer, size_t capacity, size_t *length);
int hda_tone_snapshot(char *buffer, size_t capacity, size_t *length);
#endif
