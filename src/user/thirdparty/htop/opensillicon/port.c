/* OpenSillicon compatibility functions for htop, GPLv2 or later. */

#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

long long strtoll(const char *text, char **end, int base) {
   return strtol(text, end, base);
}

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

size_t strftime(char* out, size_t size, const char* format, const struct tm* date) {
   static const char* const months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
   };
   int length;

   if (strcmp(format, "%H:%M:%S") == 0)
      length = snprintf(out, size, "%02d:%02d:%02d", date->tm_hour, date->tm_min, date->tm_sec);
   else if (strcmp(format, "%F") == 0)
      length = snprintf(out, size, "%04d-%02d-%02d", date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
   else if (strcmp(format, "%F %H:%M:%S") == 0)
      length = snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d", date->tm_year + 1900, date->tm_mon + 1, date->tm_mday, date->tm_hour, date->tm_min, date->tm_sec);
   else if (strcmp(format, "%R ") == 0)
      length = snprintf(out, size, "%02d:%02d ", date->tm_hour, date->tm_min);
   else if (strcmp(format, "%b%d ") == 0 && date->tm_mon >= 0 && date->tm_mon < 12)
      length = snprintf(out, size, "%s%02d ", months[date->tm_mon], date->tm_mday);
   else if (strcmp(format, " %Y ") == 0)
      length = snprintf(out, size, " %04d ", date->tm_year + 1900);
   else
      return 0;
   return length >= 0 && (size_t)length < size ? (size_t)length : 0;
}

int vasprintf(char** out, const char* format, va_list args) {
   va_list copy;
   va_copy(copy, args);
   int length = vsnprintf(NULL, 0, format, copy);
   va_end(copy);
   *out = NULL;
   if (length < 0)
      return -1;
   char* buffer = malloc((size_t)length + 1);
   if (!buffer)
      return -1;
   if (vsnprintf(buffer, (size_t)length + 1, format, args) != length) {
      free(buffer);
      return -1;
   }
   *out = buffer;
   return length;
}
