/* Native PCM output for the statically linked XMMS port. */
#include "abi/audio.h"
#include "xmms/plugin.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int fd = -1, volume = 70, paused, rate, channels, bytes, origin;
static int left_volume = 70, right_volume = 70;
static AFormat format;
static uint64_t count, phase;
static void get_volume(int *l, int *r) { *l = left_volume; *r = right_volume; }
static void set_volume(int l, int r) {
  left_volume = l < 0 ? 0 : l > 100 ? 100 : l;
  right_volume = r < 0 ? 0 : r > 100 ? 100 : r;
  uint32_t v = left_volume > right_volume ? left_volume : right_volume;
  volume = v;
  if (fd >= 0) ioctl(fd, OS_AUDIO_SETVOLUME, &v);
}
static int open_audio(AFormat f, int hz, int nch) {
  if (hz < 8000 || hz > 192000 || (nch != 1 && nch != 2))
    return 0;
  fd = open("/dev/audio", O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    return 0;
  format = f;
  rate = hz;
  channels = nch;
  bytes = f == FMT_U8 || f == FMT_S8 ? 1 : 2;
  count = phase = 0;
  origin = paused = 0;
  set_volume(left_volume, right_volume);
  return 1;
}
static int sample(const unsigned char *p) {
  unsigned v;
  if (bytes == 1)
    return format == FMT_U8 ? ((int)*p - 128) * 256 : (int)(int8_t)*p * 256;
  v = (format == FMT_S16_BE || format == FMT_U16_BE) ? p[1] | p[0] << 8
                                                     : p[0] | p[1] << 8;
  if (format == FMT_U16_LE || format == FMT_U16_NE || format == FMT_U16_BE)
    return (int)v - 32768;
  return (int)(v & 32767) - (int)(v & 32768);
}
static void write_audio(void *data, int length) {
  int16_t out[1024];
  int used = 0;
  const unsigned char *p = data;
  int frames = length / (bytes * channels);
  for (int i = 0; i < frames; i++) {
    int l = sample(p + i * bytes * channels),
        r = channels == 2 ? sample(p + (i * channels + 1) * bytes) : l;
    if (volume) { l = l * left_volume / volume; r = r * right_volume / volume; }
    phase += 48000;
    while (phase >= (unsigned)rate) {
      phase -= rate;
      out[used++] = l;
      out[used++] = r;
      if (used == 1024) {
        int at = 0;
        while (at < used * 2 && fd >= 0) {
          int n = write(fd, (char *)out + at, used * 2 - at);
          if (n > 0) {
            at += n;
            count += n;
          } else if (errno == EAGAIN || errno == EINTR)
            usleep(1000);
          else {
            close(fd);
            fd = -1;
          }
        }
        used = 0;
      }
    }
  }
  if (used) {
    int at = 0;
    while (at < used * 2 && fd >= 0) {
      int n = write(fd, (char *)out + at, used * 2 - at);
      if (n > 0) {
        at += n;
        count += n;
      } else if (errno == EAGAIN || errno == EINTR)
        usleep(1000);
      else {
        close(fd);
        fd = -1;
      }
    }
  }
}
static void close_audio(void) {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
  paused = 0;
}
static void flush(int ms) {
  if (fd >= 0)
    ioctl(fd, OS_AUDIO_RESET);
  origin = ms;
  count = phase = 0;
}
static void pause_audio(short p) { paused = p; }
static int buffer_free(void) {
  struct os_audio_info i;
  if (fd < 0 || paused || ioctl(fd, OS_AUDIO_GETINFO, &i))
    return 0;
  return (uint64_t)(i.queue_capacity - i.queued_bytes) * rate * bytes *
         channels / (48000 * 4);
}
static int playing(void) {
  struct os_audio_info i;
  if (fd < 0)
    return 0;
  if (ioctl(fd, OS_AUDIO_DRAIN) == 0)
    return 0;
  if (ioctl(fd, OS_AUDIO_GETINFO, &i))
    return 0;
  return i.queued_bytes != 0;
}
static int output_time(void) {
  struct os_audio_info i;
  if (fd < 0 || ioctl(fd, OS_AUDIO_GETINFO, &i))
    return origin;
  return origin + i.played_bytes / 192;
}
static int written_time(void) { return origin + count / 192; }
OutputPlugin *opensillicon_output_plugin(void) {
  static OutputPlugin p = {.description = "OpenSillicon HDA PCM",
                           .get_volume = get_volume,
                           .set_volume = set_volume,
                           .open_audio = open_audio,
                           .write_audio = write_audio,
                           .close_audio = close_audio,
                           .flush = flush,
                           .pause = pause_audio,
                           .buffer_free = buffer_free,
                           .buffer_playing = playing,
                           .output_time = output_time,
                           .written_time = written_time};
  return &p;
}
