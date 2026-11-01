#pragma once

#include <NimBLEDevice.h>

#include "ams_codec.h"

// The knob as a standard Bluetooth media remote (HID consumer control): the
// same profile as headphone buttons. Android - or any phone or computer - pairs
// with it from its own Bluetooth settings, no app, and takes play/pause,
// next/previous and volume from it. Apple's music service (AMS) stays the
// first choice on an iPhone; this is for everything else.
namespace hid_remote {

// Adds the HID service to `server`. Call before advertising starts.
void begin(NimBLEServer* server);
bool enabled();
// A phone is listening for key presses.
bool ready();
// Presses and releases the key for `cmd`. False if nobody is listening.
bool send(ams::Command cmd);

}  // namespace hid_remote
