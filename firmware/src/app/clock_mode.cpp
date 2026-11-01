#include <Arduino.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "app/desk_modes.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"

namespace wall {
namespace {
// Both survive deep sleep, like the system clock itself.
RTC_DATA_ATTR int16_t g_offsetMinutes = 0;
RTC_DATA_ATTR bool g_known = false;
}  // namespace

void set(uint32_t unix, int16_t utcOffsetMinutes) {
  const int64_t delta = (int64_t)unix - (int64_t)now();
  timeval tv{(time_t)unix, 0};
  settimeofday(&tv, nullptr);
  g_offsetMinutes = utcOffsetMinutes;
  g_known = true;
  timer_mode::shiftClock(delta);
}

uint32_t now() { return (uint32_t)time(nullptr); }
bool known() { return g_known; }
int16_t utcOffsetMinutes() { return g_offsetMinutes; }

}  // namespace wall

namespace clock_mode {
namespace {

RTC_DATA_ATTR bool g_showSeconds = false;
uint32_t g_lastFrameMs = 0;
constexpr uint32_t kFrameIntervalMs = 100;

const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void draw() {
  field::Face f{};
  f.field.on = true;
  f.field.hue = 32.0f;
  f.field.saturation = 0.25f;
  f.field.colourMode = false;
  f.dim = 0.32f;
  f.ring = -1.0f;

  char big[12], small[24];
  if (!wall::known()) {
    snprintf(big, sizeof big, "--:--");
    snprintf(small, sizeof small, "no time yet");
    f.field.value = 0.05f;
  } else {
    const time_t local = (time_t)wall::now() + wall::utcOffsetMinutes() * 60;
    tm t;
    gmtime_r(&local, &t);
    snprintf(big, sizeof big, "%02d:%02d", t.tm_hour, t.tm_min);
    if (g_showSeconds) {
      snprintf(small, sizeof small, ":%02d", t.tm_sec);
    } else {
      snprintf(small, sizeof small, "%s %d %s", kDays[t.tm_wday], t.tm_mday,
               kMonths[t.tm_mon]);
    }
    // The diamond fills over each minute and starts again at the next.
    f.field.value = 0.04f + 0.6f * (t.tm_sec / 59.0f);
  }
  f.big = big;
  f.small = small;
  field::drawFace(f);
}

}  // namespace

void begin() {}

void onGestures(const gestures::Events& g, const button::Events& b) {
  (void)b;
  if (g.tap) {
    g_showSeconds = !g_showSeconds;
    haptics::play(haptics::Pattern::Detent);
  }
}

bool tick() {
  const uint32_t ms = millis();
  if (ms - g_lastFrameMs < kFrameIntervalMs) return false;
  g_lastFrameMs = ms;
  if (display::backlightLevel() > 0) draw();
  return false;
}

}  // namespace clock_mode
