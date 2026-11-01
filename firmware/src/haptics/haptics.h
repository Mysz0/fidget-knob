#pragma once

#include <stdint.h>

// Haptic feedback for the MT35 ERM motor.
//
// An ERM (eccentric rotating mass) motor has real mechanical inertia: it takes
// tens of milliseconds to spin up and coasts for tens more after the drive is
// removed. Driving it with a plain on/off pulse therefore produces a mushy buzz
// with no defined start or end. Two things make pulses feel crisp instead:
//
//   Overdrive envelope - kick at full duty to overcome inertia, then drop to a
//   lower hold duty. A detent tick is short enough to be only the kick.
//
//   Diode braking - the 1N4148 across the motor gives the coil a path to dump
//   its energy when the drive turns off, which dynamically brakes the rotor
//   rather than letting it coast. The diode is load-bearing for feel, not just
//   flyback protection for the transistor.
//
// PWM runs at 20 kHz: above hearing, and fast enough that the motor integrates
// it into a smooth effective voltage.

namespace haptics {

enum class Pattern : uint8_t {
  Detent,        // Single short kick. Fires on every twist step.
  ModeChange,    // Two crisp pulses. Shake registered.
  TargetSelect,  // Rising triple tick. Picker moved to a new target.
  Confirm,       // Kick with a short decaying tail. Selection made.
  Limit,         // Soft double bump. Value pinned at 0% or 100%.
  Wake,          // Very soft single bump. Woken by motion.
  Alarm,         // Three long buzzes. A timer ran out.
  Rattle,        // One tiny tick. Dice tumbling.
};

// Configures the LEDC channel. Call once from setup().
void begin();

// Starts a pattern, replacing whatever is playing. Returns immediately.
//
// Replacing rather than queueing is deliberate: during a fast spin, detents can
// arrive faster than they take to play. Queueing would smear them into a
// continuous buzz that lags the user's hand, which is exactly the mush we are
// avoiding. Dropping the older pulse keeps the feedback locked to the motion.
void play(Pattern p);

// Advances the active pattern. Must be called frequently from the main loop;
// timing resolution is bounded by how often this runs.
void tick();

// True while a pattern is playing. The app uses this to avoid sleeping mid-buzz.
bool busy();

// Cuts the motor immediately. Called before sleeping.
void stop();

}  // namespace haptics
