#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

// Apple Media Service wire format.
//
// iOS exposes AMS to any bonded accessory: it reports what is playing in
// whichever app owns playback (Spotify, Apple Music, YouTube, ...) and accepts
// remote commands for it. This file is only the byte format - no Bluetooth -
// so it can be tested on the host.
//
// Reference: Apple "Apple Media Service Specification".

namespace ams {

constexpr const char* kServiceUuid = "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
constexpr const char* kRemoteCommandUuid = "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2";
constexpr const char* kEntityUpdateUuid = "2F7CABCE-808D-411F-9A0C-BB92BA96C102";
constexpr const char* kEntityAttributeUuid = "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7";

enum class Command : uint8_t {
  Play = 0,
  Pause = 1,
  TogglePlayPause = 2,
  NextTrack = 3,
  PreviousTrack = 4,
  VolumeUp = 5,
  VolumeDown = 6,
};

enum class Entity : uint8_t { Player = 0, Queue = 1, Track = 2 };

enum PlayerAttr : uint8_t {
  kPlayerName = 0,
  kPlayerPlaybackInfo = 1,  // "state,rate,elapsed" - state 1 = playing.
  kPlayerVolume = 2,        // "0.0".."1.0"
};

enum TrackAttr : uint8_t {
  kTrackArtist = 0,
  kTrackAlbum = 1,
  kTrackTitle = 2,
  kTrackDuration = 3,
};

struct Update {
  Entity entity;
  uint8_t attribute;
  bool truncated;  // iOS cut the value to fit one notification.
  std::string value;
};

// Entity Update notification: [entity][attribute][flags][UTF-8 value...].
// Returns false for anything too short or naming an unknown entity.
bool parseUpdate(const uint8_t* data, size_t len, Update* out);

// Writes to the Entity Update characteristic that subscribe to what we show.
std::vector<uint8_t> playerSubscription();
std::vector<uint8_t> trackSubscription();

bool parsePlaying(const std::string& playbackInfo, bool* playing);
bool parseVolume(const std::string& value, float* volume);

}  // namespace ams
