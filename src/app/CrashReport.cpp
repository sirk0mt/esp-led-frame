#include "app/CrashReport.h"

#include <esp_core_dump.h>
#include <esp_system.h>

#include <cstdio>

#include "core/Log.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "crash";

CrashInfo report;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

}  // namespace

void captureCrashReport() {
  report = CrashInfo{};
  report.resetReason = resetReasonName(esp_reset_reason());
  LOG_I(kTag, "reset reason: %s", report.resetReason);

  if (esp_core_dump_image_check() != ESP_OK) {
    return;  // No core dump (or an unreadable one) in flash.
  }
  esp_core_dump_summary_t summary = {};
  if (esp_core_dump_get_summary(&summary) == ESP_OK) {
    report.hasCoredump = true;
    std::snprintf(report.task, sizeof(report.task), "%s", summary.exc_task);
    report.pc = summary.exc_pc;
    const uint32_t depth = summary.exc_bt_info.depth;
    report.depth =
        static_cast<uint8_t>(depth < CrashInfo::kMaxBacktrace ? depth : CrashInfo::kMaxBacktrace);
    for (uint8_t i = 0; i < report.depth; ++i) {
      report.backtrace[i] = summary.exc_bt_info.bt[i];
    }
    report.backtraceCorrupted = summary.exc_bt_info.corrupted;
    LOG_E(kTag, "last crash in task '%s' at PC 0x%08lx", report.task,
          static_cast<unsigned long>(report.pc));
  }
  // Erase so the next restart does not report the same crash again.
  esp_core_dump_image_erase();
}

const CrashInfo& crashReport() {
  return report;
}

}  // namespace ledframe
