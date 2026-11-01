#include "knob_mode.h"

#include <ctype.h>

namespace knob {

Mode modeFromStored(int raw) {
  return raw >= 0 && raw <= kLastMode ? (Mode)raw : Mode::Lights;
}

bool parseModeCommand(const std::string& line, Mode* out) {
  std::string s;
  for (char c : line) s += (char)tolower((unsigned char)c);
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return false;
  const size_t b = s.find_last_not_of(" \t\r\n");
  s = s.substr(a, b - a + 1);

  for (int m = 0; m <= kLastMode; ++m) {
    if (s == std::string("mode ") + modeName((Mode)m)) {
      *out = (Mode)m;
      return true;
    }
  }
  return false;
}

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Music: return "music";
    case Mode::Timer: return "timer";
    case Mode::Clock: return "clock";
    case Mode::Dice: return "dice";
    case Mode::Ambient: return "ambient";
    case Mode::Photo: return "photo";
    case Mode::Notify: return "notify";
    case Mode::Games: return "games";
    default: return "lights";
  }
}

}  // namespace knob
