#include <assert.h>
#include <string.h>
#include "arch/x86_64/ata_identify.h"
int main(void)
{
 uint16_t words[256]={0};char text[41];
 words[27]=0x4142;words[28]=0x1b20;words[29]=0x2020;
 ata_identify_text(words,27,6,text);assert(strcmp(text,"AB?")==0);
}
