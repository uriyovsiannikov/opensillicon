/* SPDX-License-Identifier: GPL-2.0-or-later */
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_NOUNALIGNED
#include "stb_sprintf.h"

#include <stdio.h>
#include <stdlib.h>

int Quake_VFPrintf(FILE *stream, const char *format, va_list args)
{
	va_list copy;
	va_copy(copy, args);
	int size = stbsp_vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (size < 0)
		return -1;
	char *buffer = malloc((size_t)size + 1);
	if (!buffer)
		return -1;
	stbsp_vsnprintf(buffer, size + 1, format, args);
	size_t written = fwrite(buffer, 1, size, stream);
	free(buffer);
	return written == (size_t)size ? size : -1;
}

int Quake_FPrintf(FILE *stream, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int result = Quake_VFPrintf(stream, format, args);
	va_end(args);
	return result;
}

int Quake_ScanFloat(FILE *stream, float *value)
{
	char token[80], *end;
	if (fscanf(stream, " %79s", token) != 1)
		return 0;
	float parsed = strtof(token, &end);
	if (end == token || *end)
		return 0;
	*value = parsed;
	return 1;
}
