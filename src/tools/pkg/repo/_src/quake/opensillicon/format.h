/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QUAKE_OPENSILLICON_FORMAT_H
#define QUAKE_OPENSILLICON_FORMAT_H

#include <stdio.h>
#include "stb_sprintf.h"

#define sprintf stbsp_sprintf
#define vsprintf stbsp_vsprintf
#define snprintf stbsp_snprintf
#define vsnprintf stbsp_vsnprintf
#define fprintf Quake_FPrintf
#define vfprintf Quake_VFPrintf

int Quake_FPrintf(FILE *, const char *, ...);
int Quake_VFPrintf(FILE *, const char *, va_list);
int Quake_ScanFloat(FILE *, float *);

#endif
