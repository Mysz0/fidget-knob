#include "haptics.h"

#include <Arduino.h>

#include "pins.h"
#include "pwm_compat.h"

namespace haptics {
namespace {

constexpr int kPwmFrequency = 20000;  // Hz, above audible.
constexpr int kPwmBits = 8;           // 0..255 duty.

// One step of an envelope: hold `duty` for `ms`, then move to the next step.
// A step with duty 0 is an active gap - the diode brakes the rotor through it,
// so a zero step is a real part of the feel and not just dead time.
struct Step {
  uint8_t duty;
  uint16_t ms;
};

// Duty levels. The MT35 is a 3V motor run from the 3V3 rail through a series
// resistor, so "full" is already within its rating.
constexpr uint8_t kKick = 255;  // Overcome static friction and inertia.
constexpr uint8_t kHold = 120;  // Sustain rotation without ramping up further.
constexpr uint8_t kSoft = 80;   // Perceptible but gentle.
constexpr uint8_t kOff = 0;

// Patterns are terminated by a step with ms == 0.
constexpr Step kDetent[] = {
    {kKick, 12}, {kOff, 0},
};

constexpr Step kModeChange[] = {
    {kKick, 14}, {kOff, 45}, {kKick, 14}, {kOff, 0},
};

// Each tick a little longer than the last, so the pattern reads as ascending
// even though an ERM cannot change pitch.
constexpr Step kTargetSelect[] = {
    {kSoft, 8}, {kOff, 34}, {kHold, 11}, {kOff, 34}, {kKick, 15}, {kOff, 0},
};

constexpr Step kConfirm[] = {
    {kKick, 16}, {kHold, 26}, {kSoft, 20}, {kOff, 0},
};

constexpr Step kLimit[] = {
    {kSoft, 18}, {kOff, 60}, {kSoft, 18}, {kOff, 0},
};

constexpr Step kWake[] = {
    {kSoft, 10}, {kOff, 0},
};

// Long enough to notice in a pocket or on a desk across the room.
constexpr Step kAlarm[] = {
    {kKick, 30}, {kHold, 350}, {kOff, 250}, {kKick, 30}, {kHold, 350},
    {kOff, 250}, {kKick, 30}, {kHold, 500}, {kOff, 0},
};

constexpr Step kRattle[] = {
    {kHold, 7}, {kOff, 0},
};

const Step* g_steps = nullptr;
uint8_t g_index = 0;
uint32_t g_stepStartedAt = 0;
bool g_active = false;

const Step* stepsFor(Pattern p) {
  switch (p) {
    case Pattern::Detent:       return kDetent;
    case Pattern::ModeChange:   return kModeChange;
    case Pattern::TargetSelect: return kTargetSelect;
    case Pattern::Confirm:      return kConfirm;
    case Pattern::Limit:        return kLimit;
    case Pattern::Wake:         return kWake;
    case Pattern::Alarm:        return kAlarm;
    case Pattern::Rattle:       return kRattle;
  }
  return kDetent;
}

void drive(uint8_t duty) { pwmWrite(pins::MOTOR, duty); }

}  // namespace

void begin() {
  pwmAttach(pins::MOTOR, kPwmFrequency, kPwmBits);
  drive(kOff);
}

void play(Pattern p) {
  g_steps = stepsFor(p);
  g_index = 0;
  g_stepStartedAt = millis();
  g_active = true;
  drive(g_steps[0].duty);
}

void tick() {
  if (!g_active) return;

  const Step& current = g_steps[g_index];
  if (millis() - g_stepStartedAt < current.ms) return;

  ++g_index;
  const Step& next = g_steps[g_index];

  // ms == 0 terminates the pattern. Cut the drive and let the diode brake.
  if (next.ms == 0) {
    stop();
    return;
  }

  g_stepStartedAt = millis();
  drive(next.duty);
}

bool busy() { return g_active; }

void stop() {
  drive(kOff);
  g_active = false;
  g_steps = nullptr;
  g_index = 0;
}

}  // namespace haptics
