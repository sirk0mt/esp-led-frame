// Reads the reset reason and the core dump summary after a restart (ESP32 only).
//
// The core dump partition (partitions.csv) receives an ELF dump when the firmware
// panics. captureCrashReport() reads its summary once at start, logs it and erases
// the image, so the same crash is reported only for this run (in /api/info and the
// console "info").
#pragma once

#include "app/CrashInfo.h"

namespace ledframe {

// Call once in setup(), before anything else can crash.
void captureCrashReport();

// The report read by captureCrashReport().
const CrashInfo& crashReport();

}  // namespace ledframe
