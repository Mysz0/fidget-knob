#include "mode_store.h"

#include <Arduino.h>
#include <Preferences.h>

namespace mode_store {
namespace {

constexpr const char* kNamespace = "knob";
constexpr const char* kKey = "mode";
constexpr const char* kCoverKey = "cover";
String g_line;
int g_wakeTest = 0;
int g_testMg = 0, g_testSamples = 0;  // 1 = off, 2 = bag: "waketest" / "waketest bag".

}  // namespace

knob::Mode load() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);
  const int raw = prefs.getInt(kKey, -1);
  prefs.end();
  return knob::modeFromStored(raw);
}

void save(knob::Mode m) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putInt(kKey, (int)m);
  prefs.end();
}

knob::CoverStyle loadCoverStyle() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);
  const int raw = prefs.getInt(kCoverKey, 0);
  prefs.end();
  return raw == 1 ? knob::CoverStyle::Photo : knob::CoverStyle::Mosaic;
}

void saveCoverStyle(knob::CoverStyle style) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putInt(kCoverKey, (int)style);
  prefs.end();
}

int testMg() { return g_testMg; }
int testSamples() { return g_testSamples; }

int takeWakeTest() {
  const int t = g_wakeTest;
  g_wakeTest = 0;
  return t;
}

bool pollSerial(knob::Mode* out) {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c != '\n') {
      if (g_line.length() < 64) g_line += c;
      continue;
    }
    knob::Mode m;
    if (g_line.startsWith("grav")) {
      g_wakeTest = 5;
      g_line = "";
      continue;
    }
    if (g_line.startsWith("waketest")) {
      g_wakeTest = g_line.endsWith("bag") ? 2 : 1;
      // "waketest <mg> <samples>": try a setting without reflashing.
      int mg = 0, n = 0;
      if (sscanf(g_line.c_str(), "waketest %d %d", &mg, &n) == 2) {
        g_wakeTest = 4;
        g_testMg = mg;
        g_testSamples = n;
      }
      g_line = "";
      continue;
    }
    const bool ok = knob::parseModeCommand(std::string(g_line.c_str()), &m);
    g_line = "";
    if (ok) {
      *out = m;
      return true;
    }
  }
  return false;
}

}  // namespace mode_store
