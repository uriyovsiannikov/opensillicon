/*
htop - UnsupportedProcessTable.c
(C) 2014 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "UnsupportedProcessTable.h"

#include "abi/process_info.h"
#include "UsersTable.h"

#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#include "ProcessTable.h"
#include "UnsupportedProcess.h"


ProcessTable* ProcessTable_new(Machine* host, Hashtable* pidMatchList) {
   UnsupportedProcessTable* this = xCalloc(1, sizeof(UnsupportedProcessTable));
   Object_setClass(this, Class(ProcessTable));

   ProcessTable* super = &this->super;
   ProcessTable_init(super, Class(Process), host, pidMatchList);

   return super;
}

void ProcessTable_delete(Object* cast) {
   UnsupportedProcessTable* this = (UnsupportedProcessTable*) cast;
   ProcessTable_done(&this->super);
   free(this);
}

void ProcessTable_goThroughEntries(ProcessTable* super) {
   static const ProcessState states[] = {
      [OS_PROCESS_READY] = RUNNABLE,
      [OS_PROCESS_RUNNING] = RUNNING,
      [OS_PROCESS_BLOCKED] = SLEEPING,
      [OS_PROCESS_ZOMBIE] = ZOMBIE,
   };
   struct os_process_info entries[OS_PROCESS_INFO_MAX];
   size_t size = sizeof(entries);
   if (sysctlbyname("kern.proc.all", entries, &size, NULL, 0) < 0) {
      /* Keep the previous rows if a refresh fails. */
      for (int i = 0; i < Vector_size(super->super.rows); i++)
         ((Process*)Vector_get(super->super.rows, i))->super.updated = true;
      return;
   }

   for (size_t i = 0; i < size / sizeof(entries[0]); i++) {
      const struct os_process_info* entry = &entries[i];
      if (entry->pid > INT_MAX)
         continue;
      pid_t pid = (pid_t)entry->pid;
      bool existing;
      Process* proc = ProcessTable_getProcess(super, pid, &existing, UnsupportedProcess_new);
      Process_setParent(proc, entry->parent_pid <= INT_MAX ? (pid_t)entry->parent_pid : 0);
      Process_setThreadGroup(proc, pid);

      /* Avoid passing control characters from executable names to curses. */
      char name[OS_PROCESS_NAME_SIZE];
      memcpy(name, entry->name, sizeof(name));
      name[sizeof(name) - 1] = '\0';
      for (char* c = name; *c; c++) {
         if ((unsigned char)*c < 32 || (unsigned char)*c > 126)
            *c = '?';
      }
      Process_updateComm(proc, name);
      Process_updateCmdline(proc, name, 0, strlen(name));

      proc->state = entry->state < ARRAYSIZE(states) ? states[entry->state] : UNKNOWN;
      proc->super.updated = true;
      proc->super.show = true;
      proc->percent_cpu = NAN;
      proc->percent_mem = NAN;
      proc->m_virt = proc->m_resident = ULLONG_MAX;
      proc->nice = PROCESS_NICE_UNKNOWN;
      proc->nlwp = 1;
      if (proc->st_uid != entry->uid) {
         proc->st_uid = entry->uid;
         proc->user = UsersTable_getRef(super->super.host->usersTable, entry->uid);
      }
      super->totalTasks++;
      if (proc->state == RUNNING)
         super->runningTasks++;
      if (!existing)
         ProcessTable_add(super, proc);
   }
}
