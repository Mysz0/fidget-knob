#pragma once

#include <stdint.h>

// MPU-6050 driver.
//
// Written against the registers directly rather than using an Arduino library,
// because the feature this design depends on most - wake-on-motion interrupt -
// is the one most libraries do not expose. The power model (spec section 7)
// requires the IMU to hold the interrupt line while the ESP32 sleeps.
//
// Ranges are fixed at +/-500 dps and +/-8 g (see imu.cpp for why not 2 g).
// 500 dps comfortably covers a
// hand twist without clipping, and the narrower range gives better resolution
// than the +/-2000 dps default.

namespace imu {

struct Sample {
  float gyroZ;    // deg/s about the screen normal. Positive = counter-clockwise.
  float gyroX;    // deg/s about the in-plane axes (bias not removed).
  float gyroY;
  float accelX;   // g
  float accelY;   // g
  float accelZ;   // g
  uint32_t timestampMs;
};

// Initialises the device and configures motion detection. Returns false if the
// MPU-6050 does not answer on I2C.
bool begin();

// Drains the sensor's FIFO into `out`, oldest first, and returns how many
// samples were written. Gyro Z has the calibrated bias already subtracted.
//
// The MPU-6050 samples at 200 Hz into its own 1 KB FIFO whether or not anyone
// is reading, so nothing is lost while the CPU is busy - pushing a frame to the
// display blocks for ~25 ms, and a tap is a spike only ~10 ms wide. Polling the
// live registers between frames is what used to make taps land in the gaps.
//
// Timestamps are reconstructed at the fixed 5 ms sample period.
int readAll(Sample* out, int maxSamples);

// Configures the motion interrupt to fire on GPIO4 and puts the IMU into
// low-power accelerometer-only cycling. Call before sleeping.
//
// `thresholdMg` is how hard the device must be disturbed to wake, in milli-g.
// `durationMs` is how long that disturbance must persist, which filters out
// single-sample noise spikes.
// `samples`: how many consecutive accelerometer checks must exceed the
// threshold. In this low-power mode the duration counter steps once per check
// (not per millisecond), so 60 meant twelve seconds of solid shaking at 5 Hz.
// `fast` checks at 20 Hz instead of 5, so a short hard shake is caught.
void enableWakeOnMotion(uint16_t thresholdMg, uint8_t samples, bool fast = false);

// Returns the IMU to full-rate gyro + accel operation after waking.
void resumeFullPower();

// Gyro zero-rate bias drifts with temperature, and an uncorrected bias makes
// the value creep while the device sits untouched.
//
// Measures the bias directly from the sensor, blocking ~200 ms. Refuses - and
// returns false, leaving the old bias - if the device moved during the
// measurement, since a bias taken in the hand is wrong by however fast the hand
// was turning. Used at boot and on wake, where there is no sample history yet.
bool calibrateGyroBias();

// Nudges the bias by a residual measured from bias-corrected samples the
// gesture layer has already seen at rest. Non-blocking; this is how the bias is
// kept right during normal use.
void adjustGyroBias(float residualDps);

// Clears a latched interrupt. Until this is called the INT line stays asserted.
void clearInterrupt();

#ifdef IMU_TRACE
void traceDebug();
#endif

}  // namespace imu
