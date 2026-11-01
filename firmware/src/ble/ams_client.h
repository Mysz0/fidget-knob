#pragma once

#include <vector>

#include "ams_codec.h"
#include "knob_protocol.h"

// The knob as an AMS accessory.
//
// It advertises as a peripheral that *solicits* the Apple Media Service; iOS
// then lists it under Settings -> Bluetooth, and tapping it pairs (bonding is
// stored in NVS, so this happens once). Once the link is encrypted, the knob
// opens a GATT client over the iPhone's own connection - AMS is a service the
// iPhone hosts - subscribes to track and player updates, and writes remote
// commands.
//
// NimBLE callbacks run on its own task, where blocking GATT calls deadlock.
// Callbacks only set flags and queue raw bytes; poll() does the discovery,
// subscription and parsing on the main loop.

namespace ams_client {

// Starts BLE: the AMS client plus the Knob service for the companion app.
// Runs in every mode, so the app can switch the knob out of Lights too.
void begin(const char* deviceName, uint8_t mode, knob::CoverStyle style);
void poll();
bool ready();
bool send(ams::Command cmd);
bool nextUpdate(ams::Update* out);

// iPhone notifications (ANCS), set up alongside AMS. nextAncs() yields raw
// packets (kind 0 = notification source, 1 = data source) for the codec;
// ancsCommand() writes the control point. From the main loop only.
bool nextAncs(uint8_t* kind, std::vector<uint8_t>* bytes);
bool ancsCommand(const std::vector<uint8_t>& cmd);
bool ancsReady();
void shutdown();

}  // namespace ams_client
