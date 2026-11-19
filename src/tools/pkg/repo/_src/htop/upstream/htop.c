/*
htop - htop.c
(C) 2004-2011 Hisham H. Muhammad
(C) 2020-2021 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stdlib.h>

#include "config.h" // IWYU pragma: keep

#include "CommandLine.h"


const char* program = PACKAGE;

int main(int argc, char** argv) {
#ifdef OPENSILLICON
   if (getenv("TERM") == NULL && setenv("TERM", "xterm", 0) < 0)
      return 1;
#endif
   return CommandLine_run(argc, argv);
}
