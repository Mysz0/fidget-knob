#pragma once

#include <stdint.h>

// A plain countdown timer. Like the Pomodoro timer it replaced, it has no
// clock of its own: every call takes "now" in whole seconds (wall time, which
// keeps counting through deep sleep).
namespace timer {

enum class State : uint8_t { Idle = 0, Running = 1, Paused = 2, Done = 3 };

constexpr uint32_t kMinSeconds = 15;
constexpr uint32_t kMaxSeconds = 3 * 3600;

// The step a twist notch moves the length by, finer for short timers:
// 15 s under 5 min, 1 min under 20, 5 min under an hour, 10 min beyond.
uint32_t stepFor(uint32_t length, int direction);

class Countdown {
 public:
  void setLength(uint32_t seconds);  // Clamped; ignored while running/paused.
  uint32_t length() const { return length_; }
  // Idle/Done: starts. Running: pauses. Paused: resumes.
  void toggle(uint32_t now);
  void reset();  // Back to Idle at the full length.
  // True once, when the time runs out.
  bool tick(uint32_t now);
  void shift(int64_t delta);  // The wall clock jumped; keep the time left.

  State state() const { return state_; }
  uint32_t remaining(uint32_t now) const;
  float left(uint32_t now) const;  // 1 at the start, 0 at the end.
  uint32_t endsAt() const { return state_ == State::Running ? endAt_ : 0; }

  struct Saved {
    uint8_t state, pad[3];
    uint32_t length, endAt, pausedLeft;
  };
  Saved save() const;
  void restore(const Saved& s);

 private:
  State state_ = State::Idle;
  uint32_t length_ = 5 * 60;
  uint32_t endAt_ = 0;
  uint32_t pausedLeft_ = 0;
};

}  // namespace timer
