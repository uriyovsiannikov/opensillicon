/* OpenSillicon compatibility functions for htop, GPLv2 or later. */

#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

char* strtok_r(char* text, const char* delim, char** next) {
   if (!text)
      text = *next;
   if (!text)
      return NULL;
   text += strspn(text, delim);
   if (!*text) {
      *next = NULL;
      return NULL;
   }
   char* end = text + strcspn(text, delim);
   *next = *end ? end + 1 : NULL;
   *end = '\0';
   return text;
}

const char* strsignal(int signal) {
   switch (signal) {
   case SIGINT: return "Interrupt";
   case SIGQUIT: return "Quit";
   case SIGTERM: return "Terminated";
   default: return NULL;
   }
}

int raise(int signal) {
   return kill(getpid(), signal);
}

struct tm* localtime_r(const time_t* time, struct tm* result) {
   const struct tm* value = localtime(time);
   if (!value)
      return NULL;
   *result = *value;
   return result;
}
