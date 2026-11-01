#pragma once

#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "knob_protocol.h"

// The knob's own GATT service, for the companion app: mode, cover style,
// now-playing text, and album artwork. Lives on the same NimBLE server as the
// AMS link. Writes arrive on the NimBLE task; they only record what happened,
// and the main loop collects it with the take*() calls.
namespace knob_service {

// Adds the service to `server`. Call before advertising starts.
void attach(NimBLEServer* server, uint8_t currentMode, knob::CoverStyle style,
            const knob::KnobSettings& settings);

// True once each time the app starts listening for now playing - the cue to
// announce the current track again, which it may have missed.
bool takeNowPlayingSubscribed();

// Notifies the app of a new track (raw AMS strings).
void setNowPlaying(const std::string& artist, const std::string& album,
                   const std::string& title, bool playing);

// The app wrote new settings. Returns true once per write.
bool takeSettings(knob::KnobSettings* settings);

// The app asked for another mode. Returns true once per request.
bool takeModeRequest(uint8_t* mode);

// The app changed the cover style. Returns true once per change.
bool takeCoverStyle(knob::CoverStyle* style);

// The app sent WiFi / hub / lamp setup. Returns true once per bundle.
bool takeHomeConfig(knob::HomeConfig* config);

// A command from the app (scan WiFi, set the time). True once per command.
bool takeCommand(knob::Command* command);

// The app changed a lamp through the hub. True once per change.
bool takeLampState(knob::LampState* state);

// The knob changed a setting itself (Ambient's tap and shake): tells the app.
void setSettings(const knob::KnobSettings& s);

// The knob switched mode (without restarting): tells the app.
void setMode(uint8_t mode);

// Publishes the current mode's live state (timer, dice, photos); notifies
// only when it changed.
void setModeState(const std::vector<uint8_t>& bytes);

// Publishes the knob's status (WiFi, hub) and notifies the app if it changed.
void setStatus(const knob::KnobStatus& status);

// Publishes a finished WiFi scan and notifies the app.
void setScanResults(const std::string& encoded);

// A complete, CRC-checked image arrived. Returns true once per image.
bool takeArtwork(uint8_t* kind, std::vector<uint8_t>* bytes);

}  // namespace knob_service
