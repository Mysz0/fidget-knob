#pragma once

#include "knob_mode.h"
#include "knob_protocol.h"

// The knob's mode, persisted in NVS. The app switches it; so does typing
// "mode music", "mode dice"... into the USB serial monitor.
namespace mode_store {

knob::Mode load();
void save(knob::Mode m);

// How album covers are drawn in Music mode, chosen in the app.
knob::CoverStyle loadCoverStyle();
void saveCoverStyle(knob::CoverStyle style);

// Reads any complete serial line; true with the mode when it was a mode
// command. Call each loop.
bool pollSerial(knob::Mode* out);

// "waketest" / "waketest bag" on serial: 1 or 2 once, else 0. Runs the sleep
// wake detector awake, to check a wake setting before trusting it.
int takeWakeTest();
int testMg();       // For "waketest <mg> <samples>" (kind 4).
int testSamples();

}  // namespace mode_store
