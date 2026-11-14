#ifndef X86_64_ATA_IDENTIFY_H
#define X86_64_ATA_IDENTIFY_H
#include <stddef.h>
#include <stdint.h>
/* ATA strings are byte-swapped words; never pass firmware control bytes to a
 * terminal. */
static inline void
ata_identify_text(
    const uint16_t *words, unsigned first, size_t count, char *text)
{
	for (size_t i = 0; i < count; i++) {
		unsigned char c =
		    (unsigned char)(words[first + i / 2] >> (i % 2 ? 0 : 8));
		text[i] = c >= 32 && c <= 126 ? (char)c : '?';
	}
	while (count && text[count - 1] == ' ')
		count--;
	text[count] = 0;
}
#endif
