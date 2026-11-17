/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "quakedef.h"
#include "format.h"

qboolean isDedicated;
/* QuakeC stores signed 32-bit offsets from its strings to engine strings. */
static _Alignas(64) byte game_heap[24 * 1024 * 1024];

void Sys_Printf(char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
}

void Sys_Error(char *format, ...)
{
	static qboolean shutting_down;
	va_list args;

	fputs("Quake: ", stderr);
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
	if (!shutting_down) {
		shutting_down = true;
		Host_Shutdown();
	}
	exit(1);
}

void Sys_Quit(void)
{
	Host_Shutdown();
	exit(0);
}

int Sys_FileOpenRead(char *path, int *handle)
{
	struct stat info;
	*handle = open(path, O_RDONLY);
	if (*handle < 0)
		return -1;
	if (fstat(*handle, &info) || !S_ISREG(info.st_mode) || info.st_size > INT_MAX) {
		close(*handle);
		*handle = -1;
		return -1;
	}
	return (int)info.st_size;
}

int Sys_FileOpenWrite(char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		Sys_Error("cannot write %s: %s", path, strerror(errno));
	return fd;
}

void Sys_FileClose(int handle) { close(handle); }

void Sys_FileSeek(int handle, int position)
{
	if (lseek(handle, position, SEEK_SET) < 0)
		Sys_Error("seek failed: %s", strerror(errno));
}

int Sys_FileRead(int handle, void *dest, int count)
{
	int done = 0;

	while (done < count) {
		ssize_t amount = read(handle, (char *)dest + done, count - done);
		if (amount < 0 && errno == EINTR)
			continue;
		if (amount <= 0)
			break;
		done += amount;
	}
	return done;
}

int Sys_FileWrite(int handle, void *src, int count)
{
	int done = 0;

	while (done < count) {
		ssize_t amount = write(handle, (const char *)src + done, count - done);
		if (amount < 0 && errno == EINTR)
			continue;
		if (amount <= 0)
			Sys_Error("write failed: %s", strerror(errno));
		done += amount;
	}
	return done;
}

int Sys_FileTime(char *path)
{
	struct stat info;
	return stat(path, &info) == 0 ? (int)info.st_mtime : -1;
}

void Sys_mkdir(char *path) { mkdir(path, 0755); }

void Sys_DebugLog(char *path, char *format, ...)
{
	FILE *stream = fopen(path, "a");
	if (stream) {
		va_list args;
		va_start(args, format);
		vfprintf(stream, format, args);
		va_end(args);
		fclose(stream);
	}
}

double Sys_FloatTime(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		Sys_Error("clock_gettime failed");
	return now.tv_sec + now.tv_nsec * 1e-9;
}

void Sys_Sleep(void)
{
	struct timespec delay = {0, 1000000};
	nanosleep(&delay, NULL);
}

char *Sys_ConsoleInput(void) { return NULL; }
void Sys_HighFPPrecision(void) {}
void Sys_LowFPPrecision(void) {}

int main(int argc, char **argv)
{
	quakeparms_t parms = {0};
	char pak[MAX_OSPATH];
	const char *base;
	int index;

	COM_InitArgv(argc, argv);
	parms.argc = com_argc;
	parms.argv = com_argv;
	if (COM_CheckParm("-help") || COM_CheckParm("--help")) {
		puts("usage: quake [-basedir directory] [-nomouse] [+map e1m1]");
		return 0;
	}
	if (COM_CheckParm("-dedicated") || COM_CheckParm("-listen"))
		Sys_Error("this build supports local single-player games");

	index = COM_CheckParm("-basedir");
	if (index && index + 1 >= com_argc)
		Sys_Error("-basedir requires a directory");
	base = index ? com_argv[index + 1] :
	    (access("id1/pak0.pak", R_OK) == 0 ? "." : "/usr/share/quake");
	if (!*base || strlen(base) >= sizeof(pak) - 24)
		Sys_Error("invalid Quake data directory");
	snprintf(pak, sizeof(pak), "%s/id1/pak0.pak", base);
	if (access(pak, R_OK) != 0)
		Sys_Error("missing %s; use -basedir with the directory containing id1/pak0.pak", pak);
	parms.basedir = (char *)base;
	parms.memsize = sizeof(game_heap);
	parms.membase = game_heap;

	Host_Init(&parms);
	double previous = Sys_FloatTime();
	for (;;) {
		double now = Sys_FloatTime();
		if (now - previous < 1.0 / 72.0) {
			Sys_Sleep();
			continue;
		}
		Host_Frame(now - previous);
		previous = now;
	}
}
