// Crash trap: the Kinetos board has UART0 wired to the EVSE controller, so panic output is never
// visible. The panic handler is wrapped (-Wl,--wrap=esp_panic_handler) to keep the exception
// cause, reason and a backtrace in RTC memory, which survives the reboot. /status reports it as
// "last_panic"; decode the addresses with xtensa-esp32-elf-addr2line against firmware.elf.
#include <Arduino.h>
#include <string.h>
#include <esp_attr.h>
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <freertos/xtensa_context.h>
#include "kinetos_crash.h"

#define KINETOS_CRASH_MAGIC 0x4B434F52 // "KCOR"

RTC_NOINIT_ATTR static KinetosCrash s_crash;

extern "C" void __real_esp_panic_handler(panic_info_t *info);

extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
  memset(&s_crash, 0, sizeof(s_crash));
  s_crash.core = info->core;
  s_crash.exception = info->exception;
  if(info->reason) {
    strncpy(s_crash.reason, info->reason, sizeof(s_crash.reason) - 1);
  }
  if(info->description) {
    strncpy(s_crash.description, info->description, sizeof(s_crash.description) - 1);
  }

  const XtExcFrame *frame = (const XtExcFrame *)info->frame;
  if(frame)
  {
    s_crash.pc = frame->pc;
    s_crash.exccause = frame->exccause;
    s_crash.excvaddr = frame->excvaddr;

    esp_backtrace_frame_t bt = {
      .pc = (uint32_t)frame->pc,
      .sp = (uint32_t)frame->a1,
      .next_pc = (uint32_t)frame->a0
    };
    s_crash.backtrace[0] = esp_cpu_process_stack_pc(bt.pc);
    s_crash.depth = 1;
    while(s_crash.depth < KINETOS_CRASH_DEPTH && bt.next_pc != 0 && esp_backtrace_get_next_frame(&bt)) {
      s_crash.backtrace[s_crash.depth++] = esp_cpu_process_stack_pc(bt.pc);
    }
  }
  s_crash.magic = KINETOS_CRASH_MAGIC;

  __real_esp_panic_handler(info);
}

const KinetosCrash *kinetos_last_crash()
{
  return KINETOS_CRASH_MAGIC == s_crash.magic ? &s_crash : NULL;
}

void kinetos_clear_crash()
{
  s_crash.magic = 0;
}
