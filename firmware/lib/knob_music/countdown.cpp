#include "countdown.h"

namespace timer {

uint32_t stepFor(uint32_t length, int direction) {
  // Going down, the step is the one for the length just below, so 5:00 goes
  // to 4:45 rather than 4:00.
  const uint32_t probe = direction < 0 && length > 0 ? length - 1 : length;
  if (probe < 5 * 60) return 15;
  if (probe < 20 * 60) return 60;
  if (probe < 60 * 60) return 5 * 60;
  return 10 * 60;
}

void Countdown::setLength(uint32_t seconds) {
  if (state_ == State::Running || state_ == State::Paused) return;
  length_ = seconds < kMinSeconds ? kMinSeconds : seconds > kMaxSeconds ? kMaxSeconds : seconds;
  state_ = State::Idle;
}

void Countdown::toggle(uint32_t now) {
  switch (state_) {
    case State::Idle:
    case State::Done:
      state_ = State::Running;
      endAt_ = now + length_;
      break;
    case State::Running:
      pausedLeft_ = remaining(now);
      state_ = State::Paused;
      break;
    case State::Paused:
      endAt_ = now + pausedLeft_;
      state_ = State::Running;
      break;
  }
}

void Countdown::reset() {
  state_ = State::Idle;
  endAt_ = 0;
  pausedLeft_ = 0;
}

bool Countdown::tick(uint32_t now) {
  if (state_ != State::Running || now < endAt_) return false;
  state_ = State::Done;
  return true;
}

void Countdown::shift(int64_t delta) {
  if (state_ == State::Running) endAt_ = (uint32_t)((int64_t)endAt_ + delta);
}

uint32_t Countdown::remaining(uint32_t now) const {
  switch (state_) {
    case State::Running: return endAt_ > now ? endAt_ - now : 0;
    case State::Paused: return pausedLeft_;
    case State::Done: return 0;
    default: return length_;
  }
}

float Countdown::left(uint32_t now) const {
  return length_ ? (float)remaining(now) / (float)length_ : 0.0f;
}

Countdown::Saved Countdown::save() const {
  return Saved{(uint8_t)state_, {0, 0, 0}, length_, endAt_, pausedLeft_};
}

void Countdown::restore(const Saved& s) {
  if (s.state > (uint8_t)State::Done || s.length < kMinSeconds || s.length > kMaxSeconds) {
    reset();
    return;
  }
  state_ = (State)s.state;
  length_ = s.length;
  endAt_ = s.endAt;
  pausedLeft_ = s.pausedLeft;
}

}  // namespace timer
