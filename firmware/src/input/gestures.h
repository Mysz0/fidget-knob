#pragma once

#include <stdint.h>

#include "imu.h"

// Turns raw IMU samples into the three gestures the device understands.
//
// The hard part is that twist and shake are both "the user moved it", and a
// shake produces plenty of gyro Z. They are separated in the frequency domain:
// a twist is sustained angular velocity in one direction, a shake is a burst of
// linear acceleration that reverses direction several times a second. When a shake
// is recognised the twist accumulated during it is discarded, because that
// rotation was incidental rather than intentional.
//
// Rotation is measured on gyro Z in the body frame, which *is* rotation about
// the screen normal no matter how the device is being held. No gravity vector
// or orientation estimate is needed.
//
// A tap is a third gesture in the same signal: one sharp acceleration spike
// that decays immediately, rather than a shake's repeated reversals. The two
// overlap, so a candidate tap is held back briefly - if more spikes arrive it
// was the start of a shake, and the tap is discarded.

namespace gestures {

struct Events {
  // Detents crossed since the last update. Positive is counter-clockwise.
  // Usually 0 or +/-1, but a fast spin can cross several in one update.
  int8_t twistSteps;

  bool shake;         // A shake completed this update.
  bool tap;           // A single sharp knock on the case.
  bool becameStill;   // Transitioned from moving to still.
  bool becameActive;  // Transitioned from still to moving.
};

void begin();

// Feeds one sample and reports what it produced.
Events update(const imu::Sample& s);

// True while the device has been undisturbed long enough to be considered at
// rest. The app uses this to dim the screen and to decide when to sleep.
bool isStill();

// Gravity as the accelerometer sees it, low-passed, in g, sensor axes - for
// tilt. Which sensor axis is "up" on the screen is not known here.
void gravity(float out[3]);
// The same with ~80 ms of smoothing instead of ~400: for games.
void gravityFast(float out[3]);

// Milliseconds since the device was last meaningfully disturbed.
uint32_t stillForMs();

// Discards accumulated sub-detent rotation. Called when the app changes what
// the twist is bound to, so a half-turn toward the previous target does not
// leak into the new one.
void resetTwist();

// Only count rotation that is clearly a twist - sustained and brisk - and
// ignore the incidental turning of picking the puck up or setting it down.
// Off by default (Lights); Music mode turns it on.
void setDeliberateTwist(bool on);

// Ratchet: a fast twist back (snapping the knob back to re-grip) does not
// count. On by default; the app can turn it off.
void setRatchet(bool on);

}  // namespace gestures
