#include <Arduino.h>

#include "app/desk_modes.h"
#include "app/settings_store.h"
#include "ble/knob_service.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"
#include "render/sprites.h"

namespace ambient_mode {
namespace {

// How much of the screen the field fills; twist to change it.
RTC_DATA_ATTR float g_size = 0.75f;
constexpr float kSizePerStep = 1.0f / 160.0f;  // About half a turn end to end.
constexpr float kMinSize = 0.08f;
uint32_t g_lastFrameMs = 0;
constexpr uint32_t kFrameIntervalMs = 33;

// Tap and shake change the look on the knob itself; the app is told.
void changeLook(int packStep, int paletteStep) {
  knob::KnobSettings s = settings_store::current();
  s.spritePack = (uint8_t)((s.spritePack + packStep) % sprites::kPackCount);
  // Through the presets, then the custom colours, then round again.
  if (paletteStep) {
    s.palette = s.palette == knob::kCustomPalette ? 0
                : s.palette + 1 >= sprites::kPaletteCount ? knob::kCustomPalette
                                                          : s.palette + 1;
  }
  settings_store::save(s);
  field::setLook(s);
  knob_service::setSettings(s);
  Serial.printf("ambient: %s / palette %u\n", sprites::kPackNames[s.spritePack % sprites::kPackCount],
                s.palette);
}

}  // namespace

void begin() {}

void onGestures(const gestures::Events& g, const button::Events& b) {
  if (g.tap || b.shortPress) {
    changeLook(0, 1);
    haptics::play(haptics::Pattern::TargetSelect);
  }
  if (g.shake) {
    changeLook(1, 0);
    haptics::play(haptics::Pattern::ModeChange);
  }
  if (g.twistSteps != 0) {
    const float before = g_size;
    g_size = constrain(g_size + g.twistSteps * kSizePerStep, kMinSize, 1.0f);
    if (g_size == before) {
      haptics::play(haptics::Pattern::Limit);
    } else if ((int)(before * 10) != (int)(g_size * 10)) {
      haptics::play(haptics::Pattern::Detent);
    }
  }
}

bool tick() {
  const uint32_t ms = millis();
  if (ms - g_lastFrameMs < kFrameIntervalMs) return false;
  g_lastFrameMs = ms;
  if (display::backlightLevel() == 0) return false;
  // Saturation 0: the palette's own "white", with its accent through it.
  field::State s{};
  s.value = g_size;
  s.on = true;
  field::draw(s);
  return false;
}

}  // namespace ambient_mode
