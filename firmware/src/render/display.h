#pragma once

#include <stdint.h>

// LGFX_USE_V1 is set globally in platformio.ini so every translation unit
// agrees; defining it here as well would redefine it.
#include <LovyanGFX.hpp>

// LovyanGFX binding for the Waveshare 19192 (GC9A01, 240x240) on the Xiao C3.
//
// Configured in code rather than through build flags so the pin choices live
// next to the reasoning for them in include/pins.h.

namespace display {

// The panel driver. Exposed because the renderer needs pushImageDMA.
extern LGFX_Device& lcd;

constexpr int kWidth = 240;
constexpr int kHeight = 240;

void begin();

// Backlight, 0..255. Ramped rather than stepped - the backlight dominates
// active power draw, so it is dimmed aggressively when idle, and a visible
// snap to black would read as a fault rather than as a power saving.
void setBacklight(uint8_t level);

// Moves the backlight one step toward the target. Call from the main loop.
void tickBacklight();

// Sets the level the backlight is ramping toward.
void setBacklightTarget(uint8_t level);

uint8_t backlightLevel();

}  // namespace display
