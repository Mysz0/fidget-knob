#include "ams_codec.h"

#include <stdlib.h>

namespace ams {

bool parseUpdate(const uint8_t* data, size_t len, Update* out) {
  if (!data || len < 3) return false;
  if (data[0] > (uint8_t)Entity::Track) return false;
  out->entity = (Entity)data[0];
  out->attribute = data[1];
  out->truncated = (data[2] & 0x01) != 0;
  out->value.assign(reinterpret_cast<const char*>(data + 3), len - 3);
  return true;
}

std::vector<uint8_t> playerSubscription() {
  return {(uint8_t)Entity::Player, kPlayerPlaybackInfo, kPlayerVolume};
}

std::vector<uint8_t> trackSubscription() {
  return {(uint8_t)Entity::Track, kTrackArtist, kTrackAlbum, kTrackTitle};
}

bool parsePlaying(const std::string& playbackInfo, bool* playing) {
  if (playbackInfo.empty()) return false;
  char* end = nullptr;
  const long state = strtol(playbackInfo.c_str(), &end, 10);
  if (end == playbackInfo.c_str()) return false;
  // 1 = playing; 2 and 3 (rewinding, fast-forwarding) also count as moving.
  *playing = state >= 1 && state <= 3;
  return true;
}

bool parseVolume(const std::string& value, float* volume) {
  if (value.empty()) return false;
  char* end = nullptr;
  const float v = strtof(value.c_str(), &end);
  if (end == value.c_str()) return false;
  *volume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  return true;
}

}  // namespace ams
