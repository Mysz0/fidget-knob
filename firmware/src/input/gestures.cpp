#include "gestures.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"

namespace gestures {
namespace {

// Angular velocity below this is treated as zero. Gyro bias is calibrated at
// startup and whenever the device rests, but it drifts with temperature, and
// without a deadband that residual drift would slowly walk the value while the
// knob sits untouched on a desk.
constexpr float kTwistDeadbandDps = 4.0f;

// Axis check. A twist spins the puck about its screen axis; a swinging arm or
// a walk spins it about the other two. Recorded twists had 0.89-0.92 of their
// rotation about the screen axis, swinging only ~0.4. Rotation only counts
// while the smoothed share of it about the screen axis is at least
// kTwistAxisShare: that cut arm-swing rotation by 85-93% for about 10% of a
// real twist.
constexpr float kTwistAxisShare = 0.7f;
constexpr float kAxisSmoothS = 0.06f;

// Ratchet. Twist one way to change the value, then snap the knob back fast to
// reset your grip - the snap does not count. Every reversal is held back
// (not counted) while it is decided, so a snap never shows as a little jump
// the wrong way:
//   - reaching kSnapDps within kSnapWindowMs makes it a snap;
//   - staying under kSlowReverseDps for kSnapDecideMs makes it a real turn
//     back, released in one go - slow reversals feel as quick as ever;
//   - still undecided at kSnapWindowMs, it is a real turn back.
// The window was 60 ms with a 300 dps bar, which only worked one way: the
// recordings show the wrist whips back fast one way (500 dps inside 80 ms)
// but builds speed slower the other (often 150-360 dps, peaking after
// 60 ms) - so snaps that way were taken for real turns.
// A snap ends after kSnapQuietMs below kSnapEndDps.
constexpr float kSnapDps = 280.0f;
constexpr float kSlowReverseDps = 150.0f;
constexpr uint32_t kSnapDecideMs = 60;
constexpr uint32_t kSnapWindowMs = 140;
constexpr float kSnapEndDps = 60.0f;
constexpr uint32_t kSnapQuietMs = 100;

// Deliberate-twist gate, used by Music mode (see setDeliberateTwist). Picking
// the puck up, carrying it or setting it down all rotate it a little, and in
// Lights mode that nudge is harmless - but on a phone's volume it is a jump.
// With the gate on, rotation only counts once it has run faster than
// kIntentDps for kIntentEngageMs; the degrees turned while engaging are kept,
// so a real twist loses nothing. It disengages after kIntentReleaseMs slower.
constexpr float kIntentDps = 25.0f;
constexpr uint32_t kIntentEngageMs = 60;
constexpr uint32_t kIntentReleaseMs = 200;

// Above this the device is definitely being moved on purpose.
constexpr float kMotionGyroDps = 12.0f;
constexpr float kMotionAccelG = 0.08f;

// How long without meaningful motion before the device counts as still.
constexpr uint32_t kStillnessDelayMs = 400;

// Gyro bias tracking. The bias is re-measured from any stretch of samples
// that is *steady* - gyro and accelerometer both flat - however far from zero
// the gyro reads. Judging rest by "gyro near zero" instead, as this used to,
// can never recover from a bad bias: the wrong bias makes the device look like
// it is spinning, so it never looks still enough to be re-zeroed.
constexpr int kSteadyBlockSamples = 200;       // 1 s at 200 Hz.
constexpr float kSteadyGyroSpreadDps = 2.5f;   // Table noise is ~0.5.
constexpr float kSteadyAccelSpreadG = 0.04f;   // A hand wobbles far more.

// Gravity is tracked as a slow low-pass of the accelerometer. Slow enough that
// a shake barely moves it, fast enough to follow the puck being tilted.
constexpr float kGravityTauS = 0.4f;

// The tap and shake thresholds below were fitted to a recorded session of real
// taps, shakes, twists and ordinary handling (tools/record_gestures.py), and
// sit mid-way across the range that classified all of it correctly.

// Tap: a knock is an impact - the acceleration jumps by several g between one
// 5 ms sample and the next (3.1-4.8 g recorded). Nothing a hand does moves
// that fast: shaking, twisting and handling peaked at 1.0 g per sample. So a
// tap is recognised on the jump itself, instantly, with no waiting to see
// whether a shake follows.
//
// This replaced a threshold on acceleration *magnitude*, which could not tell a
// knock from its own rebound and ringing (the case swings back and forth for
// ~50 ms after a knock - to that detector, a shake), and which also ignored
// taps whenever the gyro read over 70 dps. A tap on a hand-held puck spins it
// at up to ~280 dps for an instant, so most real taps were thrown away.
// Set toward the low end of that gap, because light taps were being missed at
// 2.0; handling still leaves half a g of margin.
constexpr float kTapJerkG = 1.5f;
constexpr uint32_t kPostTapLockoutMs = 350;  // The rebound must not re-fire.

// A shake has jolts too. Once the accelerometer range went from 2 g to 8 g,
// the sharp turnaround at the end of an up-and-down flick jumped as fast as a
// knock - and a vertical shake toggled the light instead of changing mode.
// What separates them is what came just before: a knock lands on a puck that
// was nearly still (at most 1.4 g in the preceding 40 ms, recorded), while a
// flick's jolt arrives in the middle of 3-4 g of motion. So a jolt only counts
// as a tap if the recent past was calm.
constexpr int kTapCalmSamples = 8;       // 40 ms at 200 Hz.
constexpr float kTapCalmMaxG = 2.2f;

// And a tap has to happen in the hand. Setting the puck down, or thumping the
// desk it sits on, jolts it exactly like a knock - but afterwards it lies dead
// still, while a puck in a hand never does. So a jolt is only a candidate until
// the hand shows itself: the gyro must wander by kTapHandGyroDps between 60 and
// 200 ms after the jolt (past the impact's own ringing). Recorded in-hand taps
// wandered 21-105 dps there; a resting puck 0.3-2.5.
constexpr uint32_t kTapHandFromMs = 60;
constexpr uint32_t kTapHandUntilMs = 200;
constexpr float kTapHandGyroDps = 8.0f;

// Shake: one back-and-forth flick. Gravity-free acceleration above kLobeG for
// at least kLobeMinSamples is a "lobe"; a shake is two lobes pointing in
// opposite directions with at most kLobePairGapMs between them. Recorded
// flicks swung at 4-9 g; ordinary handling and twisting stayed under 1 g. Every
// threshold from 0.8 to 1.4 g classified the recordings perfectly once lobes
// split on reversal (see the shake section), so it sits low in that range:
// the lighter the flick that works, the better.
constexpr float kLobeG = 1.0f;
constexpr int kLobeMinSamples = 8;  // 40 ms. A knock's own spike is shorter.
constexpr uint32_t kLobePairGapMs = 150;
constexpr uint32_t kPostShakeLockoutMs = 600;  // One flick, one mode change.

// Twist detents are held while gravity-free acceleration exceeds this; see the
// twist section. Separate from kLobeG so the shake can be made more sensitive
// without also freezing a brisk twist.
constexpr float kTwistHoldG = 1.4f;

// After a shake, ignore rotation briefly so the tail of the shake does not
// register as twist.
constexpr uint32_t kPostShakeTwistLockoutMs = 350;

float g_twistDegrees = 0.0f;
float g_axisRate = 0.0f;   // Smoothed |rotation about the screen axis|.
float g_totalRate = 0.0f;  // Smoothed |rotation about any axis|.

bool g_ratchetOn = true;
int g_lastStepDir = 0;            // Sign of the last counted step.
bool g_reverseHeld = false;       // Inside a reversal's decide window.
uint32_t g_reverseSinceMs = 0;
float g_reverseDegrees = 0.0f;
float g_reversePeak = 0.0f;
bool g_snapping = false;
uint32_t g_snapQuietSinceMs = 0;

bool g_deliberateOnly = false;
bool g_intentEngaged = false;
uint32_t g_intentAboveSinceMs = 0;
uint32_t g_intentBelowSinceMs = 0;
float g_intentPendingDegrees = 0.0f;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastMotionMs = 0;
int g_steadyCount = 0;
float g_steadySum = 0.0f;
float g_steadyGyroLo = 0.0f, g_steadyGyroHi = 0.0f;
float g_steadyMagLo = 0.0f, g_steadyMagHi = 0.0f;
bool g_still = false;

float g_gravity[3] = {0.0f, 0.0f, 1.0f};
// The same, smoothed over ~80 ms instead of 400: quick enough for steering a
// game by tilt (the slow one made every tip feel late and heavy).
float g_gravityFast[3] = {0.0f, 0.0f, 1.0f};

bool g_gravityValid = false;

float g_prevAccel[3] = {0.0f, 0.0f, 0.0f};

// Gravity-free acceleration of the last kTapCalmSamples samples, not counting
// the current one: the "was it calm just before" window for taps.
float g_recentDyn[kTapCalmSamples] = {0.0f};
int g_recentDynIndex = 0;

// The lobe being measured, and the last finished one waiting for its opposite.
int g_lobeSamples = 0;
float g_lobeSum[3] = {0.0f, 0.0f, 0.0f};
uint32_t g_lobeEndMs = 0;
bool g_havePrevLobe = false;
float g_prevLobeDir[3] = {0.0f, 0.0f, 0.0f};
uint32_t g_prevLobeEndMs = 0;

bool g_tapPending = false;
uint32_t g_tapPendingSinceMs = 0;
float g_tapGyroLo = 0.0f, g_tapGyroHi = 0.0f;

uint32_t g_twistLockoutUntilMs = 0;
uint32_t g_tapLockoutUntilMs = 0;
uint32_t g_shakeLockoutUntilMs = 0;

// A lobe just ended. Keeps it if it lasted, and declares a shake if it
// reverses the one before.
void finishLobe(uint32_t now, Events* ev) {
  if (g_lobeSamples >= kLobeMinSamples && now >= g_shakeLockoutUntilMs) {
    const float len = sqrtf(g_lobeSum[0] * g_lobeSum[0] +
                            g_lobeSum[1] * g_lobeSum[1] +
                            g_lobeSum[2] * g_lobeSum[2]);
    float dir[3];
    for (int i = 0; i < 3; ++i) dir[i] = g_lobeSum[i] / len;

    const float along = dir[0] * g_prevLobeDir[0] + dir[1] * g_prevLobeDir[1] +
                        dir[2] * g_prevLobeDir[2];
    const uint32_t lobeStartMs = g_lobeEndMs - (g_lobeSamples - 1) * 5;

    if (g_havePrevLobe && lobeStartMs - g_prevLobeEndMs <= kLobePairGapMs &&
        along < -0.3f) {
      ev->shake = true;
      g_havePrevLobe = false;
      g_shakeLockoutUntilMs = now + kPostShakeLockoutMs;
      g_tapLockoutUntilMs = max(g_tapLockoutUntilMs, now + 300);
      // Discard rotation picked up during the shake - it was not intentional.
      g_twistDegrees = 0.0f;
      g_twistLockoutUntilMs = now + kPostShakeTwistLockoutMs;
    } else {
      for (int i = 0; i < 3; ++i) g_prevLobeDir[i] = dir[i];
      g_prevLobeEndMs = g_lobeEndMs;
      g_havePrevLobe = true;
    }
  }
  g_lobeSamples = 0;
}

// Returns how many of `turned` degrees count under the ratchet.
float ratchet(float rateDps, float turned, uint32_t now) {
  if (!g_ratchetOn) return turned;

  if (g_snapping) {
    if (fabsf(rateDps) < kSnapEndDps) {
      if (g_snapQuietSinceMs == 0) g_snapQuietSinceMs = now;
      if (now - g_snapQuietSinceMs >= kSnapQuietMs) {
        g_snapping = false;
        g_lastStepDir = 0;  // Fresh start: the next twist may go either way.
      }
    } else {
      g_snapQuietSinceMs = 0;
    }
    return 0.0f;
  }

  const int dir = rateDps > 0 ? 1 : (rateDps < 0 ? -1 : 0);
  const bool reversing = g_lastStepDir != 0 && dir == -g_lastStepDir;

  if (!reversing) {
    if (g_reverseHeld) {  // It turned back before deciding: drop the hold.
      g_reverseHeld = false;
      g_reverseDegrees = 0.0f;
    }
    return turned;
  }

  if (!g_reverseHeld) {
    g_reverseHeld = true;
    g_reverseSinceMs = now;
    g_reverseDegrees = 0.0f;
    g_reversePeak = 0.0f;
  }
  g_reversePeak = max(g_reversePeak, fabsf(rateDps));
  if (fabsf(rateDps) >= kSnapDps) {
    g_reverseHeld = false;
    g_reverseDegrees = 0.0f;
    g_snapping = true;
    g_snapQuietSinceMs = 0;
    g_twistDegrees = 0.0f;  // Any part-step from before is stale now.
    return 0.0f;
  }
  g_reverseDegrees += turned;
  const uint32_t held = now - g_reverseSinceMs;
  const bool slow = held >= kSnapDecideMs && g_reversePeak < kSlowReverseDps;
  if (!slow && held < kSnapWindowMs) return 0.0f;

  // Slow enough to be a real reversal: count it, including what was held.
  g_reverseHeld = false;
  g_lastStepDir = dir;
  const float released = g_reverseDegrees;
  g_reverseDegrees = 0.0f;
  return released;
}

// Returns how many of `turned` degrees count under the deliberate-twist gate.
float gateTwist(float rateDps, float turned, uint32_t now) {
  const bool fast = fabsf(rateDps) > kIntentDps;
  if (fast) {
    g_intentBelowSinceMs = 0;
    if (g_intentEngaged) return turned;
    if (g_intentAboveSinceMs == 0) {
      g_intentAboveSinceMs = now;
      g_intentPendingDegrees = 0.0f;
    }
    g_intentPendingDegrees += turned;
    if (now - g_intentAboveSinceMs >= kIntentEngageMs) {
      g_intentEngaged = true;
      const float held = g_intentPendingDegrees;
      g_intentPendingDegrees = 0.0f;
      return held;
    }
    return 0.0f;
  }

  g_intentAboveSinceMs = 0;
  g_intentPendingDegrees = 0.0f;
  if (!g_intentEngaged) return 0.0f;
  // A twist slows at its end; keep counting through that, then let go.
  if (g_intentBelowSinceMs == 0) g_intentBelowSinceMs = now;
  if (now - g_intentBelowSinceMs > kIntentReleaseMs) {
    g_intentEngaged = false;
    return 0.0f;
  }
  return turned;
}

}  // namespace

void setRatchet(bool on) {
  g_ratchetOn = on;
  g_snapping = false;
  g_reverseHeld = false;
  g_lastStepDir = 0;
}

void setDeliberateTwist(bool on) {
  g_deliberateOnly = on;
  g_intentEngaged = false;
  g_intentAboveSinceMs = 0;
  g_intentBelowSinceMs = 0;
  g_intentPendingDegrees = 0.0f;
}

void begin() {
  g_twistDegrees = 0.0f;
  g_lastSampleMs = millis();
  g_lastMotionMs = millis();
  g_steadyCount = 0;
  g_still = false;
  g_gravityValid = false;
  g_lobeSamples = 0;
  g_havePrevLobe = false;
  g_twistLockoutUntilMs = 0;
  g_tapLockoutUntilMs = 0;
  g_shakeLockoutUntilMs = 0;
}

Events update(const imu::Sample& s) {
  Events ev{0, false, false, false, false};

  const uint32_t now = s.timestampMs;
  float dt = (now - g_lastSampleMs) / 1000.0f;
  g_lastSampleMs = now;

  // A long gap means we were asleep or blocked; integrating across it would
  // inject a huge bogus rotation.
  if (dt <= 0.0f || dt > 0.2f) return ev;

  const float accel[3] = {s.accelX, s.accelY, s.accelZ};
  const float magnitude =
      sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);

  if (!g_gravityValid) {
    for (int i = 0; i < 3; ++i) g_gravity[i] = g_gravityFast[i] = g_prevAccel[i] = accel[i];
    g_gravityValid = true;
  } else {
    const float alpha = dt / (kGravityTauS + dt);
    const float fast = dt / (0.08f + dt);
    for (int i = 0; i < 3; ++i) {
      g_gravity[i] += (accel[i] - g_gravity[i]) * alpha;
      g_gravityFast[i] += (accel[i] - g_gravityFast[i]) * fast;
    }
  }

  float dyn[3];
  float jerk2 = 0.0f;
  for (int i = 0; i < 3; ++i) {
    dyn[i] = accel[i] - g_gravity[i];
    const float d = accel[i] - g_prevAccel[i];
    jerk2 += d * d;
    g_prevAccel[i] = accel[i];
  }
  const float dynMagnitude =
      sqrtf(dyn[0] * dyn[0] + dyn[1] * dyn[1] + dyn[2] * dyn[2]);

  // ------------------------------------------------------------------ tap --
  float recentPeak = 0.0f;
  for (int i = 0; i < kTapCalmSamples; ++i) {
    recentPeak = max(recentPeak, g_recentDyn[i]);
  }
  g_recentDyn[g_recentDynIndex] = dynMagnitude;
  g_recentDynIndex = (g_recentDynIndex + 1) % kTapCalmSamples;

  if (jerk2 > kTapJerkG * kTapJerkG && recentPeak < kTapCalmMaxG &&
      now >= g_tapLockoutUntilMs) {
    g_tapPending = true;
    g_tapPendingSinceMs = now;
    g_tapGyroLo = 1e9f;
    g_tapGyroHi = -1e9f;
    g_tapLockoutUntilMs = now + kPostTapLockoutMs;
    // The knock's rebound and ringing look like the lobes of a shake; and the
    // flick of the hand that delivered it is not a twist.
    g_shakeLockoutUntilMs = max(g_shakeLockoutUntilMs, now + kPostTapLockoutMs);
    g_lobeSamples = 0;
    g_havePrevLobe = false;
    g_twistDegrees = 0.0f;
    g_twistLockoutUntilMs = now + kPostTapLockoutMs / 2;
  }

  if (g_tapPending) {
    const uint32_t age = now - g_tapPendingSinceMs;
    if (age >= kTapHandFromMs) {
      g_tapGyroLo = min(g_tapGyroLo, s.gyroZ);
      g_tapGyroHi = max(g_tapGyroHi, s.gyroZ);
    }
    if (age >= kTapHandUntilMs) {
      ev.tap = g_tapGyroHi - g_tapGyroLo >= kTapHandGyroDps;
      g_tapPending = false;
    }
  }

  // ---------------------------------------------------------------- shake --
  if (dynMagnitude > kLobeG) {
    // A swing can turn round without ever dropping below the threshold - a
    // brisk flick goes straight from "there" into "back". Splitting only on a
    // dip merged the two halves into one lobe and missed the shake, so a
    // reversal of direction also ends the lobe in progress.
    if (g_lobeSamples > 0 && dyn[0] * g_lobeSum[0] + dyn[1] * g_lobeSum[1] +
                                     dyn[2] * g_lobeSum[2] < 0.0f) {
      finishLobe(now, &ev);
    }
    if (g_lobeSamples == 0) {
      for (int i = 0; i < 3; ++i) g_lobeSum[i] = 0.0f;
    }
    for (int i = 0; i < 3; ++i) g_lobeSum[i] += dyn[i];
    ++g_lobeSamples;
    g_lobeEndMs = now;
  } else if (g_lobeSamples > 0) {
    finishLobe(now, &ev);
  }

  // ---------------------------------------------------------------- twist --
  {
    const float total = sqrtf(s.gyroX * s.gyroX + s.gyroY * s.gyroY +
                              s.gyroZ * s.gyroZ);
    const float a = dt / (kAxisSmoothS + dt);
    g_axisRate += (fabsf(s.gyroZ) - g_axisRate) * a;
    g_totalRate += (total - g_totalRate) * a;
  }
  const bool aboutScreenAxis =
      g_totalRate <= 0.0f || g_axisRate >= kTwistAxisShare * g_totalRate;

  // Detents are held while acceleration is shake-strong: a flick spins the puck
  // by tens of degrees before it can be recognised as a shake, and those
  // degrees would otherwise step the brightness. Twisting stays well under
  // kTwistHoldG (under 1 g recorded), so a real twist is never held.
  if (now >= g_twistLockoutUntilMs && dynMagnitude < kTwistHoldG &&
      aboutScreenAxis && fabsf(s.gyroZ) > kTwistDeadbandDps) {
    float turned = s.gyroZ * dt;
    if (g_deliberateOnly) turned = gateTwist(s.gyroZ, turned, now);
    if (turned != 0.0f) turned = ratchet(s.gyroZ, turned, now);
    g_twistDegrees += turned;

    while (g_twistDegrees >= TWIST_DEGREES_PER_DETENT) {
      g_twistDegrees -= TWIST_DEGREES_PER_DETENT;
      ++ev.twistSteps;
      g_lastStepDir = 1;
    }
    while (g_twistDegrees <= -TWIST_DEGREES_PER_DETENT) {
      g_twistDegrees += TWIST_DEGREES_PER_DETENT;
      --ev.twistSteps;
      g_lastStepDir = -1;
    }
  } else {
    // Rotation is not counting right now, but the gate and ratchet still have
    // to see time pass: a twist that stops dead must disengage the gate, and
    // a snap ends once the knob has been quiet.
    if (g_deliberateOnly) gateTwist(0.0f, 0.0f, now);
    if (g_snapping) ratchet(0.0f, 0.0f, now);
  }

  // ------------------------------------------------------------ stillness --
  // Gravity-free acceleration, not |a| - 1 g: this sensor reads ~1.12 g at
  // rest, so the old test saw permanent motion and the knob never dimmed or
  // slept.
  const bool moving =
      fabsf(s.gyroZ) > kMotionGyroDps || dynMagnitude > kMotionAccelG;

  if (moving) {
    g_lastMotionMs = now;
    if (g_still) {
      g_still = false;
      ev.becameActive = true;
    }
  } else if (!g_still && now - g_lastMotionMs > kStillnessDelayMs) {
    g_still = true;
    ev.becameStill = true;
    // Sub-detent rotation left over from the last movement would otherwise sit
    // there and combine with the start of the next twist.
    g_twistDegrees = 0.0f;
  }

  // ------------------------------------------------------------- bias --
  if (g_steadyCount == 0) {
    g_steadySum = 0.0f;
    g_steadyGyroLo = g_steadyGyroHi = s.gyroZ;
    g_steadyMagLo = g_steadyMagHi = magnitude;
  }
  g_steadySum += s.gyroZ;
  g_steadyGyroLo = min(g_steadyGyroLo, s.gyroZ);
  g_steadyGyroHi = max(g_steadyGyroHi, s.gyroZ);
  g_steadyMagLo = min(g_steadyMagLo, magnitude);
  g_steadyMagHi = max(g_steadyMagHi, magnitude);

  if (++g_steadyCount >= kSteadyBlockSamples) {
    const bool steady =
        g_steadyGyroHi - g_steadyGyroLo < kSteadyGyroSpreadDps &&
        g_steadyMagHi - g_steadyMagLo < kSteadyAccelSpreadG;
    if (steady) {
      // These samples already have the old bias removed, so their mean is
      // exactly what the bias is still off by.
      imu::adjustGyroBias(g_steadySum / g_steadyCount);
    }
    g_steadyCount = 0;
  }

  return ev;
}

bool isStill() { return g_still; }

void gravity(float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = g_gravity[i];
}

void gravityFast(float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = g_gravityFast[i];
}

uint32_t stillForMs() {
  return g_still ? millis() - g_lastMotionMs : 0;
}

void resetTwist() { g_twistDegrees = 0.0f; }

}  // namespace gestures
