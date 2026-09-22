#ifndef KINETOS_CRASH_H
#define KINETOS_CRASH_H

#include <stdint.h>

#define KINETOS_CRASH_DEPTH 16

struct KinetosCrash {
  uint32_t magic;
  int core;
  int exception;
  uint32_t pc;
  uint32_t exccause;
  uint32_t excvaddr;
  char reason[48];
  char description[48];
  uint32_t depth;
  uint32_t backtrace[KINETOS_CRASH_DEPTH];
};

// Last panic recorded before the current boot, or NULL
const KinetosCrash *kinetos_last_crash();
void kinetos_clear_crash();

#endif // KINETOS_CRASH_H
