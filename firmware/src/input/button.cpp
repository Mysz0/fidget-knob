#include "button.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace button {
namespace {

constexpr uint32_t kDebounceMs = 25;

bool g_stable = false;      // Debounced state. True = pressed.
bool g_lastRaw = false;
uint32_t g_lastChangeMs = 0;
uint32_t g_pressedAtMs = 0;
bool g_holdReported = false;

bool readRaw() { return digitalRead(pins::BUTTON) == LOW; }  // Active low.

}  // namespace

void begin() {
  pinMode(pins::BUTTON, INPUT_PULLUP);
  g_stable = false;
  g_lastRaw = false;
  g_lastChangeMs = millis();
  g_holdReported = false;
}

Events update() {
  Events ev{false, false, false};

  const bool raw = readRaw();
  const uint32_t now = millis();

  if (raw != g_lastRaw) {
    g_lastRaw = raw;
    g_lastChangeMs = now;
  }

  if (now - g_lastChangeMs >= kDebounceMs && raw != g_stable) {
    g_stable = raw;

    if (g_stable) {
      g_pressedAtMs = now;
      g_holdReported = false;
    } else {
      if (g_holdReported) {
        ev.holdEnded = true;
      } else {
        ev.shortPress = true;
      }
      g_holdReported = false;
    }
  }

  // The hold fires while the button is still down, not on release, so the
  // picker opens under the user's finger.
  if (g_stable && !g_holdReported && now - g_pressedAtMs >= BUTTON_HOLD_MS) {
    g_holdReported = true;
    ev.holdBegan = true;
  }

  return ev;
}

bool isDown() { return g_stable; }

}  // namespace button
