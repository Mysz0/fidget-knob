#include <Arduino.h>
#include <esp_random.h>

#include "app/desk_modes.h"
#include "ble/knob_service.h"
#include "dice.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"

namespace dice_mode {
namespace {

RTC_DATA_ATTR uint8_t g_kind = 0;
RTC_DATA_ATTR uint16_t g_result = 0;  // 0 = nothing rolled yet.

// A roll tumbles for a moment: faces flick by, slowing down, each with a tiny
// tick, before it lands with a firm one.
constexpr uint32_t kRollMs = 1100;
bool g_rolling = false;
uint32_t g_rollStartMs = 0, g_nextFlickMs = 0;
uint16_t g_shown = 0;  // Face on screen while tumbling.
float g_tumbleValue = 0.4f;

constexpr int kStepsPerKind = 30;
int g_twist = 0;
uint32_t g_lastFrameMs = 0;
uint32_t g_lastPublishMs = 0;
constexpr uint32_t kFrameIntervalMs = 33;

dice::Kind kind() { return dice::kindFromByte(g_kind); }

uint16_t randomFace() { return (uint16_t)(esp_random() % dice::faces(kind()) + 1); }

void startRoll() {
  g_rolling = true;
  g_rollStartMs = millis();
  g_nextFlickMs = 0;
}

void publish() {
  knob::DiceState s;
  s.kind = g_kind;
  s.result = g_result;
  s.rolling = g_rolling;
  knob_service::setModeState(knob::encodeDiceState(s));
}

void draw() {
  field::Face f{};
  f.field.on = true;
  f.field.colourMode = false;
  f.ring = -1.0f;
  std::string big;
  if (g_rolling) {
    big = dice::resultText(kind(), g_shown);
    // The palette's own colours throughout; the tumble is in the motion.
    f.field.hue = 0.0f;
    f.field.saturation = 0.0f;
    f.field.value = g_tumbleValue;
    f.dim = 0.5f;
  } else {
    // No roll yet (or the kind just changed): the kind itself, big - the
    // picker. After a roll: the result, with the kind under it.
    big = g_result ? dice::resultText(kind(), g_result) : dice::kindShort(kind());
    // Landed: the field settles, in the palette's colours.
    f.field.hue = 0.0f;
    f.field.saturation = 0.0f;
    f.field.value = g_result ? 0.5f : 0.08f;
    f.dim = 0.35f;
  }
  f.big = big.c_str();
  f.small = g_result || g_rolling ? dice::kindShort(kind()) : "";
  field::drawFace(f);
}

}  // namespace

void begin() {}

void onGestures(const gestures::Events& g, const button::Events& b) {
  if ((g.tap || g.shake || b.shortPress) && !g_rolling) startRoll();
  if (g.twistSteps != 0 && !g_rolling) {
    g_twist += g.twistSteps;
    while (abs(g_twist) >= kStepsPerKind) {
      const int dir = g_twist > 0 ? 1 : -1;
      g_twist -= dir * kStepsPerKind;
      g_kind = (uint8_t)dice::nextKind(kind(), dir);
      g_result = 0;
      haptics::play(haptics::Pattern::Detent);
    }
  }
}

bool tick() {
  const uint32_t ms = millis();
  if (g_rolling) {
    const uint32_t t = ms - g_rollStartMs;
    if (t >= kRollMs) {
      g_rolling = false;
      g_result = randomFace();
      haptics::play(haptics::Pattern::Confirm);
      Serial.printf("dice: %s -> %s\n", dice::kindName(kind()),
                    dice::resultText(kind(), g_result).c_str());
    } else if (ms >= g_nextFlickMs) {
      g_shown = randomFace();
      g_tumbleValue = 0.25f + (esp_random() % 60) / 100.0f;
      haptics::play(haptics::Pattern::Rattle);
      // 40 ms between faces at the start, ~220 ms at the end.
      const float p = (float)t / kRollMs;
      g_nextFlickMs = ms + 40 + (uint32_t)(180.0f * p * p);
    }
  }
  if (ms - g_lastPublishMs >= 100) {
    g_lastPublishMs = ms;
    publish();
  }
  if (ms - g_lastFrameMs >= kFrameIntervalMs) {
    g_lastFrameMs = ms;
    if (display::backlightLevel() > 0) draw();
  }
  return false;
}

void onCommand(const knob::Command& c) {
  if (c.op == (uint8_t)knob::DiceOp::SetKind) {
    g_kind = (uint8_t)dice::kindFromByte(c.args[0]);
    g_result = 0;
    haptics::play(haptics::Pattern::Detent);
  } else if (c.op == (uint8_t)knob::DiceOp::Roll && !g_rolling) {
    startRoll();
  }
}

}  // namespace dice_mode
