#ifndef ABI_TERMIOS_H
#define ABI_TERMIOS_H
#include <stdint.h>
/* Native terminal ABI: no Linux c_line byte. */
struct os_termios {
 uint32_t input_flags, output_flags, control_flags, local_flags;
 uint8_t control_chars[32];
 uint32_t input_speed, output_speed;
};
#endif
