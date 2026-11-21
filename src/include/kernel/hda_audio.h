#ifndef KERNEL_HDA_AUDIO_H
#define KERNEL_HDA_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
struct vnode;
struct fd_object;
int hda_audio_open(struct vnode *, uint32_t, struct fd_object **);
bool hda_audio_is_object(const struct fd_object *);
/* Timer interrupt: bounded DMA retirement/refill, no allocation or codec verbs. */
void hda_audio_tick(void);
/* BSP safe point: reclaim resources after final close, including exit on an AP. */
void hda_audio_poll(void);
#endif
