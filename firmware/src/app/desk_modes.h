#pragma once

#include <stdint.h>

#include "input/button.h"
#include "input/gestures.h"
#include "knob_protocol.h"

// The desk modes: Timer, Clock, Dice, Ambient and Photos. Like Music they are Bluetooth
// only (WiFi stays off), and each draws a big line of text over the sprite
// field. They share one shape so the app loop can treat them alike.

// Wall-clock time from the phone. The system clock keeps running through
// deep sleep; only a full power loss forgets it.
namespace wall {
// Sets the clock; a running timer keeps the time it had left.
void set(uint32_t unix, int16_t utcOffsetMinutes);
uint32_t now();         // Unix seconds (or seconds since boot, if never set).
bool known();           // The phone has set it since power-up.
int16_t utcOffsetMinutes();
}  // namespace wall

namespace timer_mode {
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
// Every loop. True when the knob should light up on its own (time ran out).
bool tick();
void onCommand(const knob::Command& c);
// Seconds until the running timer ends, for a deep-sleep wake-up; 0 if none.
uint32_t wakeInSeconds();
void shiftClock(int64_t delta);
}  // namespace timer_mode

namespace clock_mode {
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();
}  // namespace clock_mode

// Ambient: the sprite field for its own sake, in the look chosen in the app.
namespace ambient_mode {
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();
}  // namespace ambient_mode

// Photos: up to five pictures from the phone, kept in flash.
namespace photo_mode {
void begin();
void end();  // Leaving: the field goes back to its own colours.
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();
void onCommand(const knob::Command& c);
}  // namespace photo_mode

// Notifications: the iPhone's own (ANCS), kept current in every mode so the
// list is ready when this mode opens.
namespace notify_mode {
void poll();  // Every loop, in every mode: reads what the phone sends.
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();  // True when a new notification should light the screen.
}  // namespace notify_mode

// Games: slow, turn-based ones on the sprite lattice.
namespace games_mode {
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();
}  // namespace games_mode

namespace dice_mode {
void begin();
void onGestures(const gestures::Events& g, const button::Events& b);
bool tick();
void onCommand(const knob::Command& c);
}  // namespace dice_mode
