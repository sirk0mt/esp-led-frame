#include <Arduino.h>

#include "app/App.h"

namespace {

ledframe::App app;

}  // namespace

// Rollback after an update: Arduino would confirm a new firmware in initArduino(),
// before setup(). App confirms it only after it has worked for a while (App.h,
// confirmFirmware()), so a firmware that fails during its first seconds is undone.
extern "C" bool verifyRollbackLater() {
  return true;
}

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
