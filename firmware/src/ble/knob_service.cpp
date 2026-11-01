#include "knob_service.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "knob_mode.h"

namespace knob_service {
namespace {

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

NimBLECharacteristic* g_nowPlaying = nullptr;
NimBLECharacteristic* g_status = nullptr;
NimBLECharacteristic* g_modeChar = nullptr;
NimBLECharacteristic* g_settingsChar = nullptr;
bool g_quietRejects = false;
volatile bool g_npSubscribed = false;
NimBLECharacteristic* g_scan = nullptr;
std::vector<uint8_t> g_lastStatus;

// A few commands can arrive back to back (the time on connect, then a tap in
// the app), so they queue rather than overwrite one another.
constexpr int kCommandSlots = 4;
knob::Command g_commands[kCommandSlots];
int g_commandHead = 0, g_commandCount = 0;
NimBLECharacteristic* g_modeState = nullptr;
std::vector<uint8_t> g_lastModeState;
bool g_lampReady = false;
knob::LampState g_lampState;

bool g_modeRequested = false;
uint8_t g_requestedMode = 0;
bool g_styleChanged = false;
knob::CoverStyle g_style = knob::CoverStyle::Mosaic;

bool g_homeReady = false;
knob::HomeConfig g_home;
bool g_settingsChanged = false;
knob::KnobSettings g_settings;

knob::ArtworkAssembler g_assembler;  // Touched only on the NimBLE task.
bool g_artworkReady = false;
uint8_t g_artworkKind = 0;
std::vector<uint8_t> g_artwork;

class ModeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    Serial.printf("knob: app wrote mode (%u bytes, first %u)\n", (unsigned)v.size(),
                  v.size() ? v[0] : 0);
    if (v.size() != 1 || v[0] > knob::kLastMode) return;
    portENTER_CRITICAL(&g_lock);
    g_requestedMode = v[0];
    g_modeRequested = true;
    portEXIT_CRITICAL(&g_lock);
  }
};

class StyleCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    Serial.printf("knob: app wrote cover style (%u bytes)\n", (unsigned)v.size());
    if (v.size() != 1 || v[0] > 1) return;
    portENTER_CRITICAL(&g_lock);
    g_style = (knob::CoverStyle)v[0];
    g_styleChanged = true;
    portEXIT_CRITICAL(&g_lock);
  }
};

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    knob::KnobSettings parsed;
    if (!knob::parseSettings(v.data(), v.size(), &parsed)) {
      Serial.println("knob: settings write rejected");
      return;
    }
    portENTER_CRITICAL(&g_lock);
    g_settings = parsed;
    g_settingsChanged = true;
    portEXIT_CRITICAL(&g_lock);
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    knob::Command cmd;
    if (!knob::parseCommand(v.data(), v.size(), &cmd)) return;
    portENTER_CRITICAL(&g_lock);
    if (g_commandCount < kCommandSlots) {
      g_commands[(g_commandHead + g_commandCount) % kCommandSlots] = cmd;
      ++g_commandCount;
    }
    portEXIT_CRITICAL(&g_lock);
  }
};

class LampStateCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    knob::LampState st;
    if (!knob::parseLampState(v.data(), v.size(), &st)) return;
    // Strings allocate, so the copy happens outside the critical section;
    // the flag is what the main loop waits on.
    g_lampState = st;
    portENTER_CRITICAL(&g_lock);
    g_lampReady = true;
    portEXIT_CRITICAL(&g_lock);
  }
};

class NowPlayingCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    Serial.printf("knob: app %s now playing\n", subValue ? "subscribed to" : "unsubscribed from");
    if (subValue) g_npSubscribed = true;
  }
};

class ArtworkCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    // An image the heap has no room for is refused up front: allocating it
    // anyway aborts the whole knob (no exceptions on this core).
    if (v.size() >= 4 && v[0] == 0x01) {
      const size_t total = v[2] | (v[3] << 8);
      const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      // 2 KB spare: enough for the copy-free hand-over (the old 8 KB margin
      // refused every full-size photo in Photos mode, silently).
      if (total + 2048 > largest) {
        g_assembler.onFrame(nullptr, 0);  // Drops any transfer in progress.
        Serial.printf("knob: no room for a %u-byte image (largest block %u)\n",
                      (unsigned)total, (unsigned)largest);
        g_quietRejects = true;
        return;
      }
      g_quietRejects = false;
    }
    const auto result = g_assembler.onFrame(v.data(), v.size());
    if (result == knob::ArtworkAssembler::Result::Rejected) {
      if (!g_quietRejects) Serial.println("knob: artwork transfer rejected");
      g_quietRejects = true;  // Once per transfer, not once per frame.
      return;
    }
    if (result != knob::ArtworkAssembler::Result::Complete) return;
    if (g_assembler.kind() == knob::kArtworkHomeConfig) {
      knob::HomeConfig parsed;
      if (!knob::parseHomeConfig(g_assembler.data(), g_assembler.size(), &parsed)) {
        Serial.println("knob: home setup rejected");
        return;
      }
      portENTER_CRITICAL(&g_lock);
      g_home = parsed;
      g_homeReady = true;
      portEXIT_CRITICAL(&g_lock);
      return;
    }
    // Copied out under the lock so the main loop never sees a buffer the
    // next transfer is already overwriting.
    const uint8_t kind = g_assembler.kind();
    std::vector<uint8_t> image = g_assembler.release();
    portENTER_CRITICAL(&g_lock);
    g_artwork.swap(image);
    g_artworkKind = kind;
    g_artworkReady = true;
    portEXIT_CRITICAL(&g_lock);
  }
};

}  // namespace

void attach(NimBLEServer* server, uint8_t currentMode, knob::CoverStyle style,
            const knob::KnobSettings& settings) {
  g_style = style;
  NimBLEService* svc = server->createService(knob::kKnobServiceUuid);

  g_modeChar = svc->createCharacteristic(
      knob::kModeCharUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  g_modeChar->setValue(currentMode);
  g_modeChar->setCallbacks(new ModeCallbacks());

  NimBLECharacteristic* cover = svc->createCharacteristic(
      knob::kCoverStyleCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  cover->setValue((uint8_t)style);
  cover->setCallbacks(new StyleCallbacks());

  g_nowPlaying = svc->createCharacteristic(
      knob::kNowPlayingCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_nowPlaying->setValue("");
  g_nowPlaying->setCallbacks(new NowPlayingCallbacks());

  NimBLECharacteristic* art = svc->createCharacteristic(
      knob::kArtworkCharUuid, NIMBLE_PROPERTY::WRITE_NR);
  art->setCallbacks(new ArtworkCallbacks());

  g_settingsChar = svc->createCharacteristic(
      knob::kSettingsCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  const auto bytes = knob::encodeSettings(settings);
  g_settingsChar->setValue(bytes.data(), bytes.size());
  g_settingsChar->setCallbacks(new SettingsCallbacks());

  NimBLECharacteristic* command = svc->createCharacteristic(
      knob::kCommandCharUuid, NIMBLE_PROPERTY::WRITE);
  command->setCallbacks(new CommandCallbacks());

  g_status = svc->createCharacteristic(
      knob::kStatusCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_scan = svc->createCharacteristic(
      knob::kScanCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_scan->setValue("");

  NimBLECharacteristic* lamp = svc->createCharacteristic(
      knob::kLampStateCharUuid, NIMBLE_PROPERTY::WRITE);
  lamp->setCallbacks(new LampStateCallbacks());

  g_modeState = svc->createCharacteristic(
      knob::kModeStateCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_modeState->setValue("");

  svc->start();
}

void setNowPlaying(const std::string& artist, const std::string& album,
                   const std::string& title, bool playing) {
  if (!g_nowPlaying) return;
  g_nowPlaying->setValue(knob::encodeNowPlaying(artist, album, title, playing));
  g_nowPlaying->notify();
}

bool takeModeRequest(uint8_t* mode) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_modeRequested;
  g_modeRequested = false;
  *mode = g_requestedMode;
  portEXIT_CRITICAL(&g_lock);
  return r;
}

bool takeCoverStyle(knob::CoverStyle* style) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_styleChanged;
  g_styleChanged = false;
  *style = g_style;
  portEXIT_CRITICAL(&g_lock);
  return r;
}

bool takeHomeConfig(knob::HomeConfig* config) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_homeReady;
  g_homeReady = false;
  portEXIT_CRITICAL(&g_lock);
  // Copied outside the lock: strings allocate. The NimBLE task only writes
  // g_home before setting the flag, and a second bundle within one loop
  // pass is not a case the app produces.
  if (r) *config = g_home;
  return r;
}

bool takeCommand(knob::Command* command) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_commandCount > 0;
  if (r) {
    *command = g_commands[g_commandHead];
    g_commandHead = (g_commandHead + 1) % kCommandSlots;
    --g_commandCount;
  }
  portEXIT_CRITICAL(&g_lock);
  return r;
}

bool takeNowPlayingSubscribed() {
  const bool r = g_npSubscribed;
  g_npSubscribed = false;
  return r;
}

bool takeLampState(knob::LampState* state) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_lampReady;
  g_lampReady = false;
  portEXIT_CRITICAL(&g_lock);
  if (r) *state = g_lampState;
  return r;
}

void setStatus(const knob::KnobStatus& status) {
  if (!g_status) return;
  const auto bytes = knob::encodeStatus(status);
  if (bytes == g_lastStatus) return;
  g_lastStatus = bytes;
  g_status->setValue(bytes.data(), bytes.size());
  g_status->notify();
}

void setSettings(const knob::KnobSettings& s) {
  if (!g_settingsChar) return;
  const auto bytes = knob::encodeSettings(s);
  g_settingsChar->setValue(bytes.data(), bytes.size());
  g_settingsChar->notify();
}

void setMode(uint8_t mode) {
  if (!g_modeChar) return;
  g_modeChar->setValue(mode);
  g_modeChar->notify();
}

void setModeState(const std::vector<uint8_t>& bytes) {
  if (!g_modeState || bytes == g_lastModeState) return;
  g_lastModeState = bytes;
  g_modeState->setValue(bytes.data(), bytes.size());
  g_modeState->notify();
}

void setScanResults(const std::string& encoded) {
  if (!g_scan) return;
  g_scan->setValue(encoded);
  g_scan->notify();
}

bool takeSettings(knob::KnobSettings* settings) {
  portENTER_CRITICAL(&g_lock);
  const bool r = g_settingsChanged;
  g_settingsChanged = false;
  if (r) *settings = g_settings;
  portEXIT_CRITICAL(&g_lock);
  return r;
}

bool takeArtwork(uint8_t* kind, std::vector<uint8_t>* bytes) {
  std::vector<uint8_t> previous;  // Freed after the lock, never inside it.
  portENTER_CRITICAL(&g_lock);
  const bool r = g_artworkReady;
  if (r) {
    g_artworkReady = false;
    *kind = g_artworkKind;
    bytes->swap(g_artwork);    // Caller gets the image...
    previous.swap(g_artwork);  // ...and its old buffer leaves the shared slot.
  }
  portEXIT_CRITICAL(&g_lock);
  return r;
}

}  // namespace knob_service
