#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <vector>

#include "app/desk_modes.h"
#include "ble/knob_service.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"

namespace photo_mode {
namespace {

// Each slot is two files: the JPEG for the photo view and the 23x23 colour
// grid for the mosaic view. LittleFS lives in the "spiffs" partition (896 KB).
bool g_fsReady = false;
uint8_t g_present = 0;  // Bit per slot holding a photo.
uint8_t g_current = 0;
bool g_mosaicView = false;
uint8_t g_savedCount = 0;  // Photos stored since boot; the app watches it.
bool g_shown = false;   // The panel holds the current picture.
bool g_dirty = true;    // The picture to show changed.
int g_twist = 0;
constexpr int kStepsPerPhoto = 30;
uint32_t g_lastFrameMs = 0, g_lastPublishMs = 0;

String path(uint8_t slot, const char* ext) {
  return String("/p") + slot + ext;
}

void saveChoice() {
  Preferences p;
  if (!p.begin("photo", false)) return;
  p.putUChar("cur", g_current);
  p.putBool("mosaic", g_mosaicView);
  p.end();
}

void scan() {
  g_present = 0;
  if (!g_fsReady) return;
  // Listing the folder rather than asking exists() per file, which logs an
  // error for every missing one on this core.
  File dir = LittleFS.open("/");
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const String name = f.name();  // "p3.jpg"
    if (name.length() == 6 && name[0] == 'p' && name.endsWith(".jpg")) {
      const int slot = name[1] - '0';
      if (slot >= 0 && slot < knob::kPhotoSlots) g_present |= 1u << slot;
    }
    f.close();
  }
  dir.close();
}

bool has(uint8_t slot) { return g_present & (1u << slot); }

// Next (or previous) slot holding a photo, wrapping.
void step(int dir) {
  for (int k = 1; k <= knob::kPhotoSlots; ++k) {
    const int i = ((g_current + dir * k) % knob::kPhotoSlots + knob::kPhotoSlots) %
                  knob::kPhotoSlots;
    if (has(i)) {
      if (i != g_current) {
        g_current = (uint8_t)i;
        g_dirty = true;
        saveChoice();
        haptics::play(haptics::Pattern::TargetSelect);
      }
      return;
    }
  }
}

bool readFile(const String& p, std::vector<uint8_t>* out) {
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  out->resize(f.size());
  const size_t n = f.read(out->data(), out->size());
  f.close();
  return n == out->size();
}

bool writeFile(const String& p, const uint8_t* data, size_t len) {
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  const size_t n = f.write(data, len);
  f.close();
  return n == len;
}

void takeUploads() {
  uint8_t kind;
  std::vector<uint8_t> bytes;
  if (!knob_service::takeArtwork(&kind, &bytes)) return;
  if ((kind != knob::kArtworkPhotoJpeg && kind != knob::kArtworkPhotoMosaic) ||
      bytes.size() < 2 || bytes[0] >= knob::kPhotoSlots || !g_fsReady) {
    return;
  }
  const uint8_t slot = bytes[0];
  if (kind == knob::kArtworkPhotoMosaic) {
    if (bytes.size() != 1 + (size_t)knob::kMosaicCells * knob::kMosaicCells * 2) return;
    writeFile(path(slot, ".mos"), bytes.data() + 1, bytes.size() - 1);
  } else {
    if (!writeFile(path(slot, ".jpg"), bytes.data() + 1, bytes.size() - 1)) {
      Serial.printf("photo: could not save slot %u\n", slot);
      return;
    }
    g_present |= 1u << slot;
    ++g_savedCount;
    Serial.printf("photo: slot %u saved (%u bytes)\n", slot, (unsigned)bytes.size() - 1);
  }
  // A new photo is shown straight away.
  g_current = slot;
  g_dirty = true;
  saveChoice();
}

void loadMosaic() {
  std::vector<uint8_t> raw;
  if (!readFile(path(g_current, ".mos"), &raw) ||
      raw.size() != (size_t)knob::kMosaicCells * knob::kMosaicCells * 2) {
    field::setMosaic(nullptr);
    return;
  }
  std::vector<uint16_t> grid(knob::kMosaicCells * knob::kMosaicCells);
  for (size_t i = 0; i < grid.size(); ++i) grid[i] = raw[i * 2] | (raw[i * 2 + 1] << 8);
  field::setMosaic(grid.data());
}

void draw() {
  if (!g_present) {
    field::setMosaic(nullptr);
    field::Face f{};
    f.field.on = true;
    f.field.value = 0.06f;
    f.dim = 0.4f;
    f.big = "";
    f.small = "no photos";
    f.ring = -1.0f;
    field::drawFace(f);
    g_shown = false;
    return;
  }
  if (!has(g_current)) step(1);

  if (!g_mosaicView) {
    if (g_shown && !g_dirty) return;
    std::vector<uint8_t> jpeg;
    if (readFile(path(g_current, ".jpg"), &jpeg)) {
      display::lcd.drawJpg(jpeg.data(), jpeg.size(), 0, 0, display::kWidth, display::kHeight);
    }
    g_shown = true;
    g_dirty = false;
    field::invalidate();  // The field must repaint fully when it comes back.
    return;
  }

  if (g_dirty) {
    loadMosaic();
    g_dirty = false;
  }
  g_shown = false;
  // The mosaic through the sprite field - with the chosen pack and effect.
  field::MusicScreen m{true, true, false, 0.0f, false, "", "", 0.0f};
  field::drawMusic(m);
}

void publish() {
  knob::PhotoState s;
  s.present = g_present;
  s.current = g_current;
  s.mosaic = g_mosaicView;
  s.saved = g_savedCount;
  knob_service::setModeState(knob::encodePhotoState(s));
}

}  // namespace

void begin() {
  if (!g_fsReady) {
    // Formats the partition the first time; never again after that.
    g_fsReady = LittleFS.begin(true, "/littlefs", 4, "spiffs");
    if (!g_fsReady) Serial.println("photo: no flash filesystem");
  }
  Preferences p;
  if (p.begin("photo", true)) {
    g_current = p.getUChar("cur", 0) % knob::kPhotoSlots;
    g_mosaicView = p.getBool("mosaic", false);
    p.end();
  }
  scan();
  g_dirty = true;
  g_shown = false;
}

void end() {
  field::setMosaic(nullptr);
  field::invalidate();
  g_shown = false;
}

void onGestures(const gestures::Events& g, const button::Events& b) {
  if ((g.tap || b.shortPress) && g_present) {
    g_mosaicView = !g_mosaicView;
    g_dirty = true;
    saveChoice();
    haptics::play(haptics::Pattern::Confirm);
  }
  if (g.twistSteps != 0 && g_present) {
    if ((g_twist > 0 && g.twistSteps < 0) || (g_twist < 0 && g.twistSteps > 0)) g_twist = 0;
    g_twist += g.twistSteps;
    if (abs(g_twist) >= kStepsPerPhoto) {
      step(g_twist > 0 ? 1 : -1);
      g_twist = 0;
    }
  }
}

bool tick() {
  takeUploads();
  const uint32_t ms = millis();
  if (ms - g_lastPublishMs >= 250) {
    g_lastPublishMs = ms;
    publish();
  }
  if (ms - g_lastFrameMs < 33) return false;
  g_lastFrameMs = ms;
  if (display::backlightLevel() == 0) {
    g_shown = false;  // Redraw the photo when the screen comes back.
    return false;
  }
  draw();
  return false;
}

void onCommand(const knob::Command& c) {
  const uint8_t slot = c.args[0];
  switch ((knob::PhotoOp)c.op) {
    case knob::PhotoOp::Delete:
      if (g_fsReady) {
        LittleFS.remove(path(slot, ".jpg"));
        LittleFS.remove(path(slot, ".mos"));
      }
      g_present &= ~(1u << slot);
      if (slot == g_current) step(1);
      g_dirty = true;
      break;
    case knob::PhotoOp::Show:
      if (has(slot)) {
        g_current = slot;
        g_dirty = true;
        saveChoice();
      }
      break;
    case knob::PhotoOp::ToggleView:
      g_mosaicView = !g_mosaicView;
      g_dirty = true;
      saveChoice();
      break;
  }
  haptics::play(haptics::Pattern::Detent);
}

}  // namespace photo_mode
