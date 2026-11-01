#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

// The knob's own BLE protocol with the companion app (the "Knob service").
// Only the byte formats live here, so they can be tested on the host; the
// GATT plumbing is in src/ble/knob_service.

namespace knob {

constexpr const char* kKnobServiceUuid = "6B6E6F62-0001-4000-8000-000000000001";
constexpr const char* kModeCharUuid = "6B6E6F62-0002-4000-8000-000000000001";
constexpr const char* kCoverStyleCharUuid = "6B6E6F62-0003-4000-8000-000000000001";
constexpr const char* kNowPlayingCharUuid = "6B6E6F62-0004-4000-8000-000000000001";
constexpr const char* kArtworkCharUuid = "6B6E6F62-0005-4000-8000-000000000001";
constexpr const char* kSettingsCharUuid = "6B6E6F62-0006-4000-8000-000000000001";
constexpr const char* kCommandCharUuid = "6B6E6F62-0007-4000-8000-000000000001";
constexpr const char* kStatusCharUuid = "6B6E6F62-0008-4000-8000-000000000001";
constexpr const char* kScanCharUuid = "6B6E6F62-0009-4000-8000-000000000001";
constexpr const char* kLampStateCharUuid = "6B6E6F62-000A-4000-8000-000000000001";
// Live state of the current mode (timer, dice, photos), read + notify.
constexpr const char* kModeStateCharUuid = "6B6E6F62-000B-4000-8000-000000000001";

enum class CoverStyle : uint8_t { Mosaic = 0, Photo = 1 };

// Artwork kinds the app can send.
constexpr uint8_t kArtworkMosaic = 1;  // 23x23 RGB565, one colour per cell.
constexpr uint8_t kArtworkJpeg = 2;    // 240x240 baseline JPEG.
// Title overlay drawn by the app in real fonts: [width][height][alpha...],
// one byte of coverage per pixel. Covers every script the iPhone can render,
// which the knob's built-in ASCII font cannot.
constexpr uint8_t kArtworkTitle = 3;
constexpr int kTitleMaxWidth = 220;
constexpr int kTitleMaxHeight = 96;
// Home setup from the app: WiFi, the paired IKEA hub, and the chosen lamp.
constexpr uint8_t kArtworkHomeConfig = 4;
// Photos mode: [slot][JPEG...] and [slot][23x23 RGB565 mosaic], slot 0..4.
constexpr uint8_t kArtworkPhotoJpeg = 5;
constexpr uint8_t kArtworkPhotoMosaic = 6;
constexpr int kPhotoSlots = 5;
constexpr int kMosaicCells = 23;

// CRC-32 (IEEE, as zlib), used to make sure a reassembled image is whole.
uint32_t crc32(const uint8_t* data, size_t len);

// "artist<US>album<US>title<US>playing" - the raw AMS strings for the app's
// cover lookup, and "1"/"0" so it can show whether the track is playing.
std::string encodeNowPlaying(const std::string& artist,
                             const std::string& album,
                             const std::string& title, bool playing);

// What a gesture does. Which values make sense depends on the mode: Music
// uses None/PlayPause/Next/Previous, Lights uses None/Toggle/ColourMode/
// NextLight. Same byte values, read per mode.
enum class Action : uint8_t {
  None = 0,
  PlayPause = 1,
  Next = 2,
  Previous = 3,
  Toggle = 1,
  ColourMode = 2,
  NextLight = 3,
};

// Everything the app can tune, stored on the knob. Byte layout (version 1):
//   [0] version  [1] screen-off seconds  [2] sleep minutes
//   [3] music tap  [4] music shake  [5] lights tap  [6] lights shake
//   [7] title overlay on  [8] title overlay seconds
//   [9] ratchet: a fast twist back does not count (optional; default on)
//   [10] spin cover (optional; default off)
// What a twist does, chosen per mode in the app.
enum class MusicTwist : uint8_t { None = 0, Volume = 1, Tracks = 2 };
// Brightness: brightness, or colour after a shake. (2 was "always colour",
// dropped because it left shake nothing to do; it reads as Brightness.)
enum class LightsTwist : uint8_t { None = 0, Brightness = 1, Lamps = 3 };

// The palette index meaning "the colours in customWhite/customAccent".
constexpr uint8_t kCustomPalette = 0xFF;

struct KnobSettings {
  uint8_t screenOffSeconds = 12;  // 5..255, or 0 = never (only dims).
  uint8_t sleepMinutes = 2;       // 1..60
  Action musicTap = Action::PlayPause;
  Action musicShake = Action::Next;
  Action lightsTap = Action::Toggle;
  Action lightsShake = Action::ColourMode;
  bool overlayOn = true;
  uint8_t overlaySeconds = 3;     // 2..10
  bool ratchet = true;
  bool spinCover = false;  // [10] Music: the mosaic cover turns like a record.
  uint8_t spritePack = 0;  // [11] Index into the knob's sprite packs.
  uint8_t palette = 0;     // [12] Index into its palettes.
  uint8_t effect = 0;      // [13] 0 still, 1 twinkle, 2 drift.
  MusicTwist musicTwist = MusicTwist::Volume;          // [14]
  LightsTwist lightsTwist = LightsTwist::Brightness;   // [15]
  uint8_t twistSensitivity = 1;  // [16] 0 low, 1 normal, 2 high.
  bool twistReverse = false;     // [17] Clockwise and anticlockwise swap.
  // [18..20] / [21..23] The custom palette (palette == kCustomPalette): the
  // field's main colour and its accent, as RGB.
  uint8_t customWhite[3] = {255, 214, 160};
  uint8_t customAccent[3] = {46, 168, 84};
  uint8_t dimAfterSeconds = 6;  // [24] 0 = never dims; else seconds still.
  uint8_t dimPercent = 16;      // [25] 1..90: how bright when dimmed.
  uint8_t brightness = 100;     // [26] 10..100: how bright normally.
  // [27] Also be a standard Bluetooth media remote (HID), for Android and
  // anything else without Apple's music service. Off by default: iPhones can
  // take a HID device for a keyboard and hide the on-screen one.
  bool mediaRemote = false;
};

// WiFi, hub and lamp, sent by the app. Each present field is one
// [key][lenLo][lenHi][bytes] record, so the app can send only what changed -
// a lamp choice alone, without the WiFi password.
struct HomeConfig {
  enum Key : uint8_t {
    kSsid = 1,
    kPassword = 2,
    kHubHost = 3,
    kHubToken = 4,
    kHubCert = 5,  // PEM, the hub's issuer chain (see tools/pair.py).
    kLampId = 6,
    kLampCycle = 7,  // Comma-separated ids the next-lamp gesture steps through.
  };
  std::string ssid, password, hubHost, hubToken, hubCertPem, lampId, lampCycle;
  uint8_t present = 0;  // Bit (1 << key) per field that was sent.

  bool has(Key k) const { return present & (1u << k); }
  // Only the lamp changed: applied live, no restart needed.
  bool onlyLamp() const { return present == (1u << kLampId); }
};

std::vector<uint8_t> encodeHomeConfig(const HomeConfig& c);

// True if `id` is in the comma-separated `cycle`, or the cycle is empty
// (meaning every lamp).
bool lampInCycle(const std::string& cycle, const std::string& id);

// The app changed a lamp through the hub; the knob updates its cached state
// rather than showing stale values. [on][level][hueLo][hueHi][sat %][id...]
struct LampState {
  bool on = false;
  uint8_t level = 0;        // 1..100
  uint16_t hue = 0;         // 0..359
  uint8_t saturation = 0;   // 0..100
  std::string lampId;
};
bool parseLampState(const uint8_t* data, size_t len, LampState* out);

// What the knob reports about itself, for the app's status rows.
// [version][mode][wifi state][rssi][hub connected][ssid length][ssid...]
enum class WifiState : uint8_t { Off = 0, Connecting = 1, Connected = 2, Failed = 3 };
struct KnobStatus {
  uint8_t mode = 0;
  WifiState wifi = WifiState::Off;
  int8_t rssi = 0;
  bool hubConnected = false;
  std::string ssid;
};
std::vector<uint8_t> encodeStatus(const KnobStatus& s);

// WiFi networks the knob can see: "ssid<US>rssi<US>secured" records joined
// by <RS>, strongest first as given, duplicates dropped, cut to `budget`
// bytes on a record boundary.
struct ScanEntry {
  std::string ssid;
  int rssi;
  bool secured;
};
std::string encodeScanList(const std::vector<ScanEntry>& nets, size_t budget);

// Commands from the app:
//   [1]                                  scan WiFi
//   [2][unix time u32][utc offset i16]   set the clock
//   [3][1][seconds u16]                  timer length
//   [3][2] / [3][3]                      timer start-pause / reset
//   [4][1][kind]                         dice kind
//   [4][2]                               roll
//   [5][1][slot] / [5][2][slot] / [5][3]   photo delete / show / photo-mosaic
//   [6]                                    turn off (until the button)
enum class CommandType : uint8_t {
  ScanWifi = 1, SetTime = 2, Timer = 3, Dice = 4, Photo = 5, PowerOff = 6
};
enum class PhotoOp : uint8_t { Delete = 1, Show = 2, ToggleView = 3 };
enum class TimerOp : uint8_t { SetLength = 1, Toggle = 2, Reset = 3 };
enum class DiceOp : uint8_t { SetKind = 1, Roll = 2 };
struct Command {
  CommandType type;
  uint32_t unixTime = 0;
  int16_t utcOffsetMinutes = 0;
  uint8_t op = 0;
  uint8_t args[4] = {0, 0, 0, 0};
};

// Mode state, knob -> app.
// Timer:    [2][state][left u16][length u16]              6 bytes
// Dice:     [4][kind][result u16][rolling]                5 bytes
struct TimerState {
  uint8_t state = 0;  // timer::State
  uint16_t left = 0, length = 0;  // Seconds.
};
std::vector<uint8_t> encodeTimerState(const TimerState& s);
struct DiceState {
  uint8_t kind = 0;
  uint16_t result = 0;  // 0 = not rolled yet.
  bool rolling = false;
};
std::vector<uint8_t> encodeDiceState(const DiceState& s);
// Photos: [6][slots present, bit per slot][current slot][showing mosaic]
//         [saved count] - bumped each time a photo is stored, so the app can
//         tell its upload made it.
struct PhotoState {
  uint8_t present = 0;
  uint8_t current = 0;
  bool mosaic = false;
  uint8_t saved = 0;
};
std::vector<uint8_t> encodePhotoState(const PhotoState& s);
bool parseCommand(const uint8_t* data, size_t len, Command* out);
bool parseHomeConfig(const uint8_t* data, size_t len, HomeConfig* out);

std::vector<uint8_t> encodeSettings(const KnobSettings& s);
// Out-of-range values are clamped and unknown actions become None; a wrong
// version or a short buffer is refused.
bool parseSettings(const uint8_t* data, size_t len, KnobSettings* out);

// Reassembles artwork from write frames:
//   01 kind lenLo lenHi           begin
//   02 offLo offHi bytes...       data, strictly in order
//   03 crc32 (little-endian)      end
// Anything out of place - a frame before begin, a gap, an overrun, a bad
// CRC - throws the whole image away. A half-drawn cover is worse than none.
class ArtworkAssembler {
 public:
  static constexpr size_t kMaxBytes = 24 * 1024;

  enum class Result { Pending, Complete, Rejected };

  Result onFrame(const uint8_t* frame, size_t len);

  uint8_t kind() const { return kind_; }
  const uint8_t* data() const { return buffer_.data(); }
  size_t size() const { return buffer_.size(); }
  // Hands the finished image over without copying it (a copy would need a
  // second block as big as the image, which the knob may not have).
  std::vector<uint8_t> release() {
    active_ = false;
    std::vector<uint8_t> out;
    out.swap(buffer_);
    return out;
  }

 private:
  Result reject();

  bool active_ = false;
  uint8_t kind_ = 0;
  size_t expected_ = 0;
  std::vector<uint8_t> buffer_;
};

}  // namespace knob
