#ifndef PIN_DIAG  // The pindiag environment has its own setup()/loop().

#include <Arduino.h>

#include "app/app.h"

// Waking from deep sleep leaves the IMU in low-power motion-detect mode with
// its interrupt latched. No special handling is needed here: imu::begin()
// issues a device reset, which clears the latch and returns the sensor to
// full-rate operation regardless of how the ESP32 got here.

void setup() {
  Serial.begin(115200);
  app::begin();
}

void loop() { app::loop(); }

#endif  // PIN_DIAG
