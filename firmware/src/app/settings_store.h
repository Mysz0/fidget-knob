#pragma once

#include "knob_protocol.h"

// The app-tunable settings (screen time, sleep, gestures, title overlay),
// kept in NVS and in memory. Read anywhere with current().
namespace settings_store {

void load();
const knob::KnobSettings& current();
void save(const knob::KnobSettings& s);

}  // namespace settings_store
