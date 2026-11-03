#include <Arduino.h>

#include "app/App.h"

namespace {

ledframe::App app;

}  // namespace

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
