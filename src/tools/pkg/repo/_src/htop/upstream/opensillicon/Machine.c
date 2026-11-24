/*
htop - UnsupportedMachine.c
(C) 2014 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "UnsupportedMachine.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/sysctl.h>

#include "Machine.h"


Machine* Machine_new(UsersTable* usersTable, uid_t userId) {
   UnsupportedMachine* this = xCalloc(1, sizeof(UnsupportedMachine));
   Machine* super = &this->super;

   Machine_init(super, usersTable, userId);

   super->existingCPUs = 1;
   super->activeCPUs = 1;

   return super;
}

void Machine_delete(Machine* super) {
   UnsupportedMachine* this = (UnsupportedMachine*) super;
   Machine_done(super);
   free(this);
}

bool Machine_isCPUonline(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);

   (void) host; (void) id;

   return true;
}

void Machine_scan(Machine* super) {
   UnsupportedMachine* this = (UnsupportedMachine*) super;
   super->existingCPUs = 1;
   super->activeCPUs = 1;
   super->totalSwap = 0;
   super->usedSwap = 0;
   super->cachedSwap = 0;
   super->totalMem = 0;
   this->usedMem = 0;
   this->cachedMem = 0;

   uint64_t total = 0, freePages = 0;
   int pageSize = 0;
   size_t size = sizeof(total);
   if (sysctlbyname("hw.physmem", &total, &size, NULL, 0) == 0) {
      size = sizeof(freePages);
      if (sysctlbyname("vm.freepages", &freePages, &size, NULL, 0) == 0) {
         size = sizeof(pageSize);
         if (sysctlbyname("hw.pagesize", &pageSize, &size, NULL, 0) == 0 && pageSize > 0) {
            super->totalMem = total / 1024;
            uint64_t freeBytes = freePages * (uint64_t)pageSize;
            this->usedMem = freeBytes <= total ? (total - freeBytes) / 1024 : 0;
         }
      }
   }
}

int Machine_getCPUPhysicalCoreID(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);
   (void) host;
   return (int)id;
}

int Machine_getCPUThreadIndex(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);
   (void) host; (void) id;
   return 0;
}
