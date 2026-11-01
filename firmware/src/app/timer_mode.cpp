#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

#include "app/desk_modes.h"
#include "ble/knob_service.h"
#include "countdown.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"

namespace timer_mode {
namespace {

timer::Countdown g_timer;
// The running timer survives deep sleep here; the length is kept in NVS.
RTC_DATA_ATTR timer::Countdown::Saved g_saved = {};
RTC_DATA_ATTR bool g_savedValid = false;

constexpr int kStepsPerNotch = 24;  // As for volume: about 15 degrees.
int g_twist = 0;
uint32_t g_lastFrameMs = 0, g_lastPublishMs = 0;

// Rung when the time is up, then again every few seconds until someone
// touches it, for at most half a minute.
uint32_t g_doneAtMs = 0, g_lastRingMs = 0;
constexpr uint32_t kRingEveryMs = 4000, kRingForMs = 30000;

void persist() {
  g_saved = g_timer.save();
  g_savedValid = true;
}

void saveLength() {
  Preferences p;
  if (!p.begin("timer", false)) return;
  p.putUInt("len", g_timer.length());
  p.end();
}

void publish(uint32_t now) {
  knob::TimerState s;
  s.state = (uint8_t)g_timer.state();
  s.left = (uint16_t)min<uint32_t>(g_timer.remaining(now), 0xFFFF);
  s.length = (uint16_t)min<uint32_t>(g_timer.length(), 0xFFFF);
  knob_service::setModeState(knob::encodeTimerState(s));
}

void format(uint32_t secs, char* out, size_t n) {
  if (secs >= 3600) {
    // h:mm:ss is too wide for the sprite text; past an hour, minutes do.
    snprintf(out, n, "%uH%02u", (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60));
  } else {
    snprintf(out, n, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
  }
}

void draw(uint32_t now) {
  const timer::State st = g_timer.state();
  field::Face f{};
  f.field.on = true;
  f.field.hue = 32.0f;
  f.field.saturation = 0.0f;  // The palette's own white.
  f.field.colourMode = false;
  // The field shrinks with the time left; a still, small cluster when idle.
  f.field.value = st == timer::State::Idle ? 0.1f : 0.08f + 0.5f * g_timer.left(now);
  f.dim = st == timer::State::Running ? 0.4f : 0.25f;
  char big[16];
  format(g_timer.remaining(now), big, sizeof big);
  f.big = big;
  f.small = st == timer::State::Paused ? "paused" : st == timer::State::Done ? "done" : "";
  // The ring at the rim runs down with the time.
  f.ring = st == timer::State::Idle ? -1.0f : g_timer.left(now);
  f.ringR = 46;
  f.ringG = 168;
  f.ringB = 84;
  field::drawFace(f);
}

void stopRinging() { g_doneAtMs = 0; }

}  // namespace

void begin() {
  Preferences p;
  if (p.begin("timer", true)) {
    g_timer.setLength(p.getUInt("len", 5 * 60));
    p.end();
  }
  if (g_savedValid) g_timer.restore(g_saved);
}

void onGestures(const gestures::Events& g, const button::Events& b) {
  const uint32_t now = wall::now();
  if (g.tap || b.shortPress) {
    if (g_timer.state() == timer::State::Done) {
      g_timer.reset();
      stopRinging();
    } else {
      g_timer.toggle(now);
    }
    haptics::play(haptics::Pattern::Confirm);
  }
  if (g.shake && g_timer.state() != timer::State::Idle) {
    g_timer.reset();
    stopRinging();
    haptics::play(haptics::Pattern::ModeChange);
  }
  // Twisting sets the length, like a kitchen timer - not while it runs, so a
  // knock cannot change it mid-way.
  const timer::State st = g_timer.state();
  if (g.twistSteps != 0 && (st == timer::State::Idle || st == timer::State::Done)) {
    stopRinging();
    g_twist += g.twistSteps;
    while (abs(g_twist) >= kStepsPerNotch) {
      const int dir = g_twist > 0 ? 1 : -1;
      g_twist -= dir * kStepsPerNotch;
      const uint32_t len = g_timer.length();
      const int64_t next = (int64_t)len + dir * (int64_t)timer::stepFor(len, dir);
      if (next < (int64_t)timer::kMinSeconds || next > (int64_t)timer::kMaxSeconds) {
        haptics::play(haptics::Pattern::Limit);
        g_twist = 0;
        break;
      }
      g_timer.setLength((uint32_t)next);
      saveLength();
      haptics::play(haptics::Pattern::Detent);
    }
  }
  persist();
}

bool tick() {
  const uint32_t now = wall::now();
  const uint32_t ms = millis();
  bool poke = false;
  if (g_timer.tick(now)) {
    g_doneAtMs = ms;
    g_lastRingMs = ms;
    haptics::play(haptics::Pattern::Alarm);
    persist();
    poke = true;
    Serial.println("timer: done");
  } else if (g_doneAtMs && g_timer.state() == timer::State::Done) {
    if (ms - g_doneAtMs > kRingForMs) {
      stopRinging();
    } else if (ms - g_lastRingMs > kRingEveryMs) {
      g_lastRingMs = ms;
      haptics::play(haptics::Pattern::Alarm);
      poke = true;
    }
  }
  if (poke || ms - g_lastPublishMs >= 250) {
    g_lastPublishMs = ms;
    publish(now);
  }
  if (ms - g_lastFrameMs >= 33) {
    g_lastFrameMs = ms;
    if (display::backlightLevel() > 0) draw(now);
  }
  return poke;
}

void onCommand(const knob::Command& c) {
  const uint32_t now = wall::now();
  switch ((knob::TimerOp)c.op) {
    case knob::TimerOp::SetLength:
      g_timer.setLength(c.args[0] | (c.args[1] << 8));
      saveLength();
      break;
    case knob::TimerOp::Toggle:
      if (g_timer.state() == timer::State::Done) g_timer.reset();
      else g_timer.toggle(now);
      break;
    case knob::TimerOp::Reset:
      g_timer.reset();
      break;
  }
  stopRinging();
  haptics::play(haptics::Pattern::Detent);
  persist();
}

uint32_t wakeInSeconds() {
  const uint32_t end = g_timer.endsAt();
  const uint32_t now = wall::now();
  if (end == 0) return 0;
  return end > now ? end - now : 1;
}

void shiftClock(int64_t delta) {
  g_timer.shift(delta);
  persist();
}

}  // namespace timer_mode
