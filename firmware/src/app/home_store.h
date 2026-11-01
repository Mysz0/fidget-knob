#pragma once

#include <string>

#include "knob_protocol.h"

// WiFi, IKEA hub and lamp, as set up from the app and kept in NVS. Every
// getter falls back to include/config.h, so a knob that was never set up
// from the app behaves exactly as before.
namespace home_store {

void load();

// Saves the fields present in `c`, leaving the others as they were.
void save(const knob::HomeConfig& c);

// The current WiFi worked: remember it as the one to fall back to.
void markWifiGood();
// The current WiFi failed: go back to the last one that worked. False if
// there is none, or it is the same.
bool revertWifi();

const char* ssid();
const char* password();
const char* hubHost();
const char* hubToken();
const char* hubCertPem();
uint16_t hubPort();
// Empty when no lamp was chosen in the app.
const char* lampId();
// Comma-separated lamp ids for the next-lamp gesture; empty means all.
const char* lampCycle();

}  // namespace home_store
