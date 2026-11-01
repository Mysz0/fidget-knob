#include "knob_protocol.h"

namespace knob {

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

std::string encodeNowPlaying(const std::string& artist,
                             const std::string& album,
                             const std::string& title, bool playing) {
  return artist + '\x1F' + album + '\x1F' + title + '\x1F' +
         (playing ? '1' : '0');
}

namespace {

uint8_t clampU8(int v, int lo, int hi) {
  return (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
}

Action action(uint8_t raw) { return raw <= 3 ? (Action)raw : Action::None; }

}  // namespace

namespace {

std::string* homeField(HomeConfig* c, uint8_t key) {
  switch (key) {
    case HomeConfig::kSsid: return &c->ssid;
    case HomeConfig::kPassword: return &c->password;
    case HomeConfig::kHubHost: return &c->hubHost;
    case HomeConfig::kHubToken: return &c->hubToken;
    case HomeConfig::kHubCert: return &c->hubCertPem;
    case HomeConfig::kLampId: return &c->lampId;
    case HomeConfig::kLampCycle: return &c->lampCycle;
    default: return nullptr;
  }
}

}  // namespace

std::vector<uint8_t> encodeHomeConfig(const HomeConfig& c) {
  std::vector<uint8_t> out;
  HomeConfig copy = c;
  for (uint8_t key = HomeConfig::kSsid; key <= HomeConfig::kLampCycle; ++key) {
    const std::string* v = homeField(&copy, key);
    // Encode what is set; an explicitly present empty field is kept too.
    if (v->empty() && !c.has((HomeConfig::Key)key)) continue;
    out.push_back(key);
    out.push_back((uint8_t)(v->size() & 0xFF));
    out.push_back((uint8_t)(v->size() >> 8));
    out.insert(out.end(), v->begin(), v->end());
  }
  return out;
}

bool parseHomeConfig(const uint8_t* d, size_t len, HomeConfig* out) {
  if (!d || len == 0) return false;
  HomeConfig c;
  size_t i = 0;
  while (i < len) {
    if (len - i < 3) return false;
    const uint8_t key = d[i];
    const size_t n = d[i + 1] | (d[i + 2] << 8);
    i += 3;
    std::string* field = homeField(&c, key);
    if (!field || n > len - i) return false;
    field->assign(reinterpret_cast<const char*>(d + i), n);
    c.present |= (uint8_t)(1u << key);
    i += n;
  }
  *out = c;
  return true;
}

std::vector<uint8_t> encodeSettings(const KnobSettings& s) {
  return {1,
          s.screenOffSeconds,
          s.sleepMinutes,
          (uint8_t)s.musicTap,
          (uint8_t)s.musicShake,
          (uint8_t)s.lightsTap,
          (uint8_t)s.lightsShake,
          (uint8_t)(s.overlayOn ? 1 : 0),
          s.overlaySeconds,
          (uint8_t)(s.ratchet ? 1 : 0),
          (uint8_t)(s.spinCover ? 1 : 0),
          s.spritePack,
          s.palette,
          s.effect,
          (uint8_t)s.musicTwist,
          (uint8_t)s.lightsTwist,
          s.twistSensitivity,
          (uint8_t)(s.twistReverse ? 1 : 0),
          s.customWhite[0], s.customWhite[1], s.customWhite[2],
          s.customAccent[0], s.customAccent[1], s.customAccent[2],
          s.dimAfterSeconds,
          s.dimPercent,
          s.brightness,
          (uint8_t)(s.mediaRemote ? 1 : 0)};
}

bool parseSettings(const uint8_t* d, size_t len, KnobSettings* out) {
  if (!d || len < 9 || d[0] != 1) return false;
  out->screenOffSeconds = d[1] == 0 ? 0 : clampU8(d[1], 5, 255);
  out->sleepMinutes = clampU8(d[2], 1, 60);
  out->musicTap = action(d[3]);
  out->musicShake = action(d[4]);
  out->lightsTap = action(d[5]);
  out->lightsShake = action(d[6]);
  out->overlayOn = d[7] != 0;
  out->overlaySeconds = clampU8(d[8], 2, 10);
  out->ratchet = len >= 10 ? d[9] != 0 : true;  // Absent in older apps.
  out->spinCover = len >= 11 ? d[10] != 0 : false;
  // Unknown indices are kept: the knob falls back to the first pack or
  // palette when it draws, and the app shows what it sent.
  out->spritePack = len >= 12 ? d[11] : 0;
  out->palette = len >= 13 ? d[12] : 0;
  out->effect = len >= 14 && d[13] <= 2 ? d[13] : 0;
  out->musicTwist = len >= 15 && d[14] <= 2 ? (MusicTwist)d[14] : MusicTwist::Volume;
  out->lightsTwist = len >= 16 && (d[15] == 0 || d[15] == 3) ? (LightsTwist)d[15]
                                                              : LightsTwist::Brightness;
  out->twistSensitivity = len >= 17 && d[16] <= 2 ? d[16] : 1;
  out->twistReverse = len >= 18 && d[17] != 0;
  if (len >= 24) {
    for (int i = 0; i < 3; ++i) {
      out->customWhite[i] = d[18 + i];
      out->customAccent[i] = d[21 + i];
    }
  }
  // Older apps: dim at half the screen-off time, as before; full brightness.
  out->dimAfterSeconds = len >= 25 ? d[24] : (uint8_t)(out->screenOffSeconds / 2);
  out->dimPercent = len >= 26 ? clampU8(d[25], 1, 90) : 16;
  out->brightness = len >= 27 ? clampU8(d[26], 10, 100) : 100;
  out->mediaRemote = len >= 28 && d[27] != 0;
  return true;
}

ArtworkAssembler::Result ArtworkAssembler::reject() {
  active_ = false;
  buffer_.clear();
  buffer_.shrink_to_fit();  // Do not sit on up to 24 KB after a failure.
  return Result::Rejected;
}

ArtworkAssembler::Result ArtworkAssembler::onFrame(const uint8_t* f,
                                                   size_t len) {
  if (!f || len == 0) return reject();

  switch (f[0]) {
    case 0x01: {
      if (len < 4) return reject();
      const uint8_t kind = f[1];
      const size_t total = f[2] | (f[3] << 8);
      if (kind < kArtworkMosaic || kind > kArtworkPhotoMosaic || total == 0 ||
          total > kMaxBytes) {
        return reject();
      }
      // A begin always restarts: a new track may cut an old transfer short.
      active_ = true;
      kind_ = kind;
      expected_ = total;
      buffer_.clear();
      buffer_.reserve(total);
      return Result::Pending;
    }
    case 0x02: {
      if (!active_ || len < 3) return reject();
      const size_t offset = f[1] | (f[2] << 8);
      const size_t n = len - 3;
      if (offset != buffer_.size() || buffer_.size() + n > expected_) {
        return reject();
      }
      buffer_.insert(buffer_.end(), f + 3, f + len);
      return Result::Pending;
    }
    case 0x03: {
      if (!active_ || len < 5) return reject();
      const uint32_t crc = (uint32_t)f[1] | ((uint32_t)f[2] << 8) |
                           ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 24);
      if (buffer_.size() != expected_ ||
          crc32(buffer_.data(), buffer_.size()) != crc) {
        return reject();
      }
      active_ = false;
      return Result::Complete;
    }
    default:
      return reject();
  }
}

}  // namespace knob

namespace knob {

bool lampInCycle(const std::string& cycle, const std::string& id) {
  if (cycle.empty()) return true;
  size_t start = 0;
  while (start <= cycle.size()) {
    const size_t comma = cycle.find(',', start);
    const size_t end = comma == std::string::npos ? cycle.size() : comma;
    if (cycle.compare(start, end - start, id) == 0 && end - start == id.size()) {
      return true;
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

bool parseLampState(const uint8_t* d, size_t len, LampState* out) {
  if (!d || len < 6) return false;  // Five fixed bytes and at least one id char.
  out->on = d[0] != 0;
  out->level = d[1] > 100 ? 100 : d[1];
  out->hue = (uint16_t)((d[2] | (d[3] << 8)) % 360);
  out->saturation = d[4] > 100 ? 100 : d[4];
  out->lampId.assign(reinterpret_cast<const char*>(d + 5), len - 5);
  return true;
}

std::vector<uint8_t> encodeStatus(const KnobStatus& s) {
  const size_t n = s.ssid.size() > 32 ? 32 : s.ssid.size();
  std::vector<uint8_t> out = {1, s.mode, (uint8_t)s.wifi, (uint8_t)s.rssi,
                              (uint8_t)(s.hubConnected ? 1 : 0), (uint8_t)n};
  out.insert(out.end(), s.ssid.begin(), s.ssid.begin() + n);
  return out;
}

std::string encodeScanList(const std::vector<ScanEntry>& nets, size_t budget) {
  std::string out;
  std::vector<std::string> seen;
  for (const auto& n : nets) {
    if (n.ssid.empty()) continue;  // Hidden networks cannot be picked by name.
    bool dup = false;
    for (const auto& s : seen) dup = dup || s == n.ssid;
    if (dup) continue;
    std::string rec = n.ssid + '\x1F' + std::to_string(n.rssi) + '\x1F' +
                      (n.secured ? "1" : "0");
    const size_t need = rec.size() + (out.empty() ? 0 : 1);
    if (out.size() + need > budget) break;
    if (!out.empty()) out += '\x1E';
    out += rec;
    seen.push_back(n.ssid);
  }
  return out;
}

bool parseCommand(const uint8_t* d, size_t len, Command* out) {
  if (!d || len < 1) return false;
  if (d[0] == (uint8_t)CommandType::ScanWifi) {
    out->type = CommandType::ScanWifi;
    return true;
  }
  if (d[0] == (uint8_t)CommandType::PowerOff) {
    out->type = CommandType::PowerOff;
    return true;
  }
  if (d[0] == (uint8_t)CommandType::SetTime && len >= 7) {
    out->type = CommandType::SetTime;
    out->unixTime = (uint32_t)d[1] | ((uint32_t)d[2] << 8) |
                    ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 24);
    out->utcOffsetMinutes = (int16_t)(d[5] | (d[6] << 8));
    return true;
  }
  if (d[0] == (uint8_t)CommandType::Timer && len >= 2 && d[1] >= 1 && d[1] <= 3) {
    out->type = CommandType::Timer;
    out->op = d[1];
    if (d[1] == (uint8_t)TimerOp::SetLength) {
      if (len < 4) return false;
      out->args[0] = d[2];
      out->args[1] = d[3];
    }
    return true;
  }
  if (d[0] == (uint8_t)CommandType::Photo && len >= 2 && d[1] >= 1 && d[1] <= 3) {
    out->type = CommandType::Photo;
    out->op = d[1];
    if (d[1] != (uint8_t)PhotoOp::ToggleView) {
      if (len < 3 || d[2] >= kPhotoSlots) return false;
      out->args[0] = d[2];
    }
    return true;
  }
  if (d[0] == (uint8_t)CommandType::Dice && len >= 2 && d[1] >= 1 && d[1] <= 2) {
    out->type = CommandType::Dice;
    out->op = d[1];
    if (d[1] == (uint8_t)DiceOp::SetKind) {
      if (len < 3) return false;
      out->args[0] = d[2];
    }
    return true;
  }
  return false;
}

std::vector<uint8_t> encodeTimerState(const TimerState& s) {
  return {2, s.state, (uint8_t)(s.left & 0xFF), (uint8_t)(s.left >> 8),
          (uint8_t)(s.length & 0xFF), (uint8_t)(s.length >> 8)};
}

std::vector<uint8_t> encodePhotoState(const PhotoState& s) {
  return {6, s.present, s.current, (uint8_t)(s.mosaic ? 1 : 0), s.saved};
}

std::vector<uint8_t> encodeDiceState(const DiceState& s) {
  return {4, s.kind, (uint8_t)(s.result & 0xFF), (uint8_t)(s.result >> 8),
          (uint8_t)(s.rolling ? 1 : 0)};
}

}  // namespace knob
