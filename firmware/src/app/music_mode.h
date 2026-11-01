#pragma once

#include "input/button.h"
#include "input/gestures.h"

// Music mode: the knob as a remote for whatever the iPhone is playing.
// Bluetooth only - WiFi is never started in this mode.
namespace music_mode {

void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
void tick();         // Every loop: BLE housekeeping, command pump, screen.
void beforeSleep();  // Drop the link cleanly before deep sleep.
void end();          // Leaving Music for another mode.

// Re-reads gesture mapping and title-overlay settings after the app changes
// them.
void applySettings();

}  // namespace music_mode
