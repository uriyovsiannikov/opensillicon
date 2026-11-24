#ifndef ABI_HDA_H
#define ABI_HDA_H
#include <stdint.h>
#define OS_HDA_PROBE 0x4f480001U
#define OS_HDA_TONE 0x4f480002U
#define OS_HDA_REPORT_MAX 16384U
struct os_hda_report {
	uint64_t length;
	char text[OS_HDA_REPORT_MAX];
};
#endif
