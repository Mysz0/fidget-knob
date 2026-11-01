#pragma once

#include <stdint.h>

#include <string>

namespace knob {

// What the knob is for right now. Stored in NVS so it survives deep sleep.
enum class Mode : uint8_t {
  Lights = 0, Music = 1, Timer = 2, Clock = 3, Dice = 4, Ambient = 5, Photo = 6,
  Notify = 7, Games = 8
};
constexpr int kLastMode = 8;

// Anything unrecognised - a blank NVS, a value from a future firmware - boots
// into Lights, the mode that works with no phone at all.
Mode modeFromStored(int raw);

// "mode lights" / "mode music" / "mode timer" ..., case-insensitive, surrounding space ignored.
bool parseModeCommand(const std::string& line, Mode* out);

const char* modeName(Mode m);

}  // namespace knob
