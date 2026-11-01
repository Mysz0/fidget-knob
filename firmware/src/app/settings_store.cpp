#include "settings_store.h"

#include <Preferences.h>

namespace settings_store {
namespace {

knob::KnobSettings g_current;

}  // namespace

void load() {
  Preferences prefs;
  prefs.begin("knob", /*readOnly=*/true);
  // Room to grow: a blob bigger than the buffer is refused whole, which
  // silently reset every setting at boot once the settings passed 16 bytes.
  uint8_t raw[64];
  const size_t stored = prefs.getBytesLength("settings");
  const size_t n = stored <= sizeof(raw) ? prefs.getBytes("settings", raw, sizeof(raw)) : 0;
  prefs.end();
  knob::KnobSettings parsed;
  // A missing or unreadable blob leaves the defaults, which match the
  // knob's behaviour before settings existed.
  if (n > 0 && knob::parseSettings(raw, n, &parsed)) g_current = parsed;
}

const knob::KnobSettings& current() { return g_current; }

void save(const knob::KnobSettings& s) {
  g_current = s;
  const auto bytes = knob::encodeSettings(s);
  Preferences prefs;
  prefs.begin("knob", /*readOnly=*/false);
  prefs.putBytes("settings", bytes.data(), bytes.size());
  prefs.end();
}

}  // namespace settings_store
