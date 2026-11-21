#ifndef ABI_AUDIO_H
#define ABI_AUDIO_H
#include <stdint.h>
#define OS_AUDIO_GETINFO 0x4f534101U
#define OS_AUDIO_SETVOLUME 0x4f534102U
#define OS_AUDIO_DRAIN 0x4f534103U
#define OS_AUDIO_RESET 0x4f534104U
#define OS_AUDIO_RUNNING 1U
#define OS_AUDIO_ERROR 2U
struct os_audio_info {
    uint32_t rate, channels, bits, volume;
    uint32_t queue_capacity, queued_bytes, period_bytes, flags;
    uint64_t written_bytes, played_bytes, underruns;
};
#endif
