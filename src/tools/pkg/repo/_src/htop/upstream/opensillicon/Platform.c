/*
htop - unsupported/Platform.c
(C) 2014 Hisham H. Muhammad
(C) 2015 David C. Hunt
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "unsupported/Platform.h"

#include <math.h>
#include <signal.h>
#include "UnsupportedMachine.h"

#include "CPUMeter.h"
#include "Macros.h"
#include "MemoryMeter.h"
#include "SysArchMeter.h"
#include "TasksMeter.h"
#include "UptimeMeter.h"


const ScreenDefaults Platform_defaultScreens[] = {
   {
      .name = "Main",
      .columns = "PID PPID USER STATE Command",
      .sortKey = "PID",
   },
};

const unsigned int Platform_numberOfDefaultScreens = ARRAYSIZE(Platform_defaultScreens);

const SignalItem Platform_signals[] = {
   { .name = " 0 Cancel",    .number = 0 },
   { .name = " 2 SIGINT",    .number = SIGINT },
   { .name = " 9 SIGKILL",   .number = SIGKILL },
   { .name = "15 SIGTERM",   .number = SIGTERM },
};

const unsigned int Platform_numberOfSignals = ARRAYSIZE(Platform_signals);

enum {
   MEMORY_CLASS_USED = 0,
   MEMORY_CLASS_CACHED,
}; // N.B. the chart will display categories in this order

const MemoryClass Platform_memoryClasses[] = {
   [MEMORY_CLASS_USED] = { .label = "used", .countsAsUsed = true, .countsAsCache = false, .color = MEMORY_1 },
   [MEMORY_CLASS_CACHED] = { .label = "cached", .countsAsUsed = false, .countsAsCache = true, .color = MEMORY_2 },
};

const unsigned int Platform_numberOfMemoryClasses = ARRAYSIZE(Platform_memoryClasses);

const MeterClass* const Platform_meterTypes[] = {
   &MemoryMeter_class,
   &TasksMeter_class,
   &UptimeMeter_class,
   &SecondsUptimeMeter_class,
   &SysArchMeter_class,
   &BlankMeter_class,
   NULL
};

bool Platform_init(void) {
   /* no platform-specific setup needed */
   return true;
}

void Platform_done(void) {
   /* no platform-specific cleanup needed */
}

void Platform_setBindings(Htop_Action* keys) {
   /* no platform-specific key bindings */
   (void) keys;
}

int Platform_getUptime(void) {
   struct timespec now;
   if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
      return -1;
   return now.tv_sec > INT_MAX ? INT_MAX : (int)now.tv_sec;
}

void Platform_getLoadAverage(double* one, double* five, double* fifteen) {
   *one = 0;
   *five = 0;
   *fifteen = 0;
}

pid_t Platform_getMaxPid(void) {
   return INT_MAX;
}

double Platform_setCPUValues(Meter* this, unsigned int cpu) {
   (void) cpu;

   double* v = this->values;
   v[CPU_METER_FREQUENCY] = NAN;
   v[CPU_METER_TEMPERATURE] = NAN;

   this->curItems = 1;

   return 0.0;
}

void Platform_setMemoryValues(Meter* this) {
   double* v = this->values;
   this->total = this->host->totalMem;
   v[MEMORY_CLASS_USED] = ((const UnsupportedMachine*)this->host)->usedMem;
   v[MEMORY_CLASS_CACHED] = NAN;

   this->curItems = 2;
}

void Platform_setSwapValues(Meter* this) {
   (void) this;
}

char* Platform_getProcessEnv(pid_t pid) {
   (void) pid;
   return NULL;
}

FileLocks_ProcessData* Platform_getProcessLocks(pid_t pid) {
   (void)pid;
   return NULL;
}

void Platform_getFileDescriptors(double* used, double* max) {
   *used = NAN;
   *max = NAN;
}

bool Platform_getDiskIO(DiskIOData* data) {
   (void)data;
   return false;
}

bool Platform_getNetworkIO(NetworkIOData* data) {
   (void)data;
   return false;
}

void Platform_getBattery(double* percent, ACPresence* isOnAC) {
   *percent = NAN;
   *isOnAC = AC_ERROR;
}

void Platform_getHostname(char* buffer, size_t size) {
   String_safeStrncpy(buffer, "OpenSillicon", size);
}

const char* Platform_getRelease(void) {
   return "OpenSillicon";
}
