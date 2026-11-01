#include "field.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include <string>
#include <vector>

#include "display.h"
#include "knob_protocol.h"
#include "pixel_font.h"
#include "sprites.h"

namespace field {
namespace {

// ------------------------------------------------------------------- grid --

// 9 px sprites on an 11 px pitch: a 2 px gutter keeps every mark distinct.
constexpr int kPitch = 11;
constexpr int kHalfCells = 11;  // Cells from the centre to the rim: 23 across.

constexpr int kCentreX = display::kWidth / 2;
constexpr int kCentreY = display::kHeight / 2;

// Cells whose centre lies beyond this are never drawn; the sprite still has to
// fit inside the 120 px panel radius.
constexpr int kClipRadius = 114;

// The diamond's half-width, measured corner to centre (the L1 radius), at the
// dimmest "on" and at full. Full is set so that even the rim at 45 degrees -
// 114 * sqrt(2) = 161 px away in L1 terms - sits well inside the diamond, and
// a full-brightness field fills the round screen edge to edge.
constexpr float kMinRadius = 14.0f;  // The centre and its four neighbours.
constexpr float kMaxRadius = 220.0f;

// How far inward the edge frays. Each cell gets its own cut-off somewhere in
// this band, so the diamond's outline dissolves instead of being a hard line.
constexpr float kFray = 0.28f;

// Where the weights hand over, as a fraction of the radius, and how much each
// cell's own jitter blurs those borders so the tiers interleave.
constexpr float kHeavyUntil = 0.45f;
constexpr float kMediumUntil = 0.78f;
constexpr float kTierJitter = 0.30f;

// The accent: a green scattered through the field, rarer at the centre.
// The accent and the "white" come from the palette chosen in the app.
uint8_t kAccentR = 46, kAccentG = 168, kAccentB = 84;
constexpr float kAccentAtCentre = 0.10f;
constexpr float kAccentAtEdge = 0.55f;

// Warm white, for a bulb at zero saturation: the peach of an incandescent
// filament rather than a flat grey.
float kWarmHue = 32.0f;
float kWarmSaturation = 0.36f;

// --------------------------------------------------------------- effects --

// Still is the original: nothing moves unless the value does. Twinkle dims
// and brightens a third of the marks, each on its own slow cycle; Drift lets
// each mark turn into another of its weight now and then.
uint8_t g_effect = 0;
uint32_t g_effectNow = 0;  // millis() of the frame being built.
constexpr uint32_t kEffectFrameMs = 83;  // ~12 fps while an effect runs.

// ---------------------------------------------------------------- sprites --

uint16_t g_bitmaps[sprites::kCount][sprites::kSize];
uint8_t g_byWeight[3][sprites::kCount];
uint8_t g_weightCount[3] = {0, 0, 0};

// A cell ready to blit.
struct Cell {
  int16_t x, y;  // Top-left of the sprite.
  uint8_t sprite;
  uint16_t colour;
};

constexpr int kMaxCells = (2 * kHalfCells + 1) * (2 * kHalfCells + 1) + 1;
Cell g_cells[kMaxCells];
int g_cellCount = 0;

// ------------------------------------------------------------------ bands --

// Band height. 240 * 24 * 2 bytes = 11.5 KB per buffer, two buffers.
constexpr int kBandHeight = 24;
constexpr int kBandPixels = display::kWidth * kBandHeight;

uint16_t* g_band[2] = {nullptr, nullptr};
uint8_t g_activeBand = 0;

// ------------------------------------------------------------------ label --

// The picker label, as an 8-bit coverage mask. Rendered at twice the size and
// averaged down, which is how a 1-bit GFX font gets anti-aliased edges.
struct Label {
  uint8_t* mask = nullptr;
  int w = 0, h = 0, x = 0, y = 0;
  char text[128] = "";  // Room for a notification block.
  const lgfx::IFont* font = nullptr;
  uint8_t scale = 1;
};
uint32_t g_labelGeneration = 0;  // Bumped whenever a label is rebuilt.

// ------------------------------------------------------------ sprite text --

// The big text of the Timer, Clock, Dice and Photos faces, drawn as sprites on
// the field's own lattice: each dot of the 5-row font is one sprite of the
// chosen pack. The field is cleared behind it so it stays readable. When the
// text changes, its new dots appear one after another over ~0.1 s.
constexpr int kLattice = 23;
uint32_t g_textBorn[kLattice * kLattice];  // 0 = off, else when it shows.
bool g_textActive = false;
char g_textKey[24] = "";
int g_textTop = 0;
int g_textMinI = 0, g_textMaxI = 0, g_textMinJ = 0, g_textMaxJ = 0;
uint8_t g_textSprite = 0;  // The pack's boldest heavy mark.
// Game pieces: the pack's heavy marks ranked by how many pixels they light -
// the boldest is the player, the next the good points, the lightest the bad
// ones, so the three are always different shapes at full sprite size.
uint8_t g_goalSprite = 0, g_hazardSprite = 0;
// Game pieces on the lattice: 0 = none, else BoardLook + 1.
uint8_t g_board[kLattice * kLattice] = {};
bool g_boardOn = false;
bool g_drawingBoard = false;  // drawFace() called by drawBoard().
uint32_t g_boardGeneration = 0;
// Pixel area kept clear of field sprites (under the small line), or w = 0.
int g_clearX = 0, g_clearY = 0, g_clearW = 0, g_clearH = 0;

// Album-cover mosaic: one native RGB565 colour per lattice cell, row-major
// from the top-left, or empty. When set, Music mode paints each sprite in the
// colour of its patch of the cover.
uint16_t g_mosaic[23 * 23];
bool g_mosaicOn = false;
uint32_t g_mosaicGeneration = 0;  // Forces a redraw when the cover changes.
bool g_titleFromApp = false;       // Label slot 0 holds the app's mask.
float g_mosaicSpinCos = 1.0f, g_mosaicSpinSin = 0.0f;
uint32_t g_titleGeneration = 0;

Label g_labels[2];  // 0 = main line (picker name / track title), 1 = artist.
bool g_labelOn[2] = {false, false};

// Position dots under the picker label.
constexpr int kMaxPickerDots = 12;
struct Dot {
  int16_t x, y;
  uint8_t r;
  uint16_t colour;
};
Dot g_pickerDots[kMaxPickerDots];
int g_pickerDotCount = 0;

// ----------------------------------------------------------------- motion --

// Time constant of every eased quantity. ~95% settled in 3 tau, so a step
// lands in about a quarter of a second - quick enough to feel direct, slow
// enough that the eye sees the field grow instead of jump.
constexpr float kEaseTauMs = 80.0f;

struct Shown {
  float radius, hue, saturation, dim, wheel;
  bool valid;
};
Shown g_shown{0, 0, 0, 1, 0, false};
uint32_t g_lastEaseMs = 0;

// What was last pushed to the panel. A frame identical to this is skipped.
struct Signature {
  int32_t radius, hue, saturation, dim, wheel, effectFrame;
  int16_t pickIndex, pickCount;
  bool picker;
};
Signature g_drawn{};
bool g_drawnValid = false;

// ------------------------------------------------------------------------

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

float wrap360(float degrees) {
  degrees = fmodf(degrees, 360.0f);
  return degrees < 0 ? degrees + 360.0f : degrees;
}

void hsvToRgb(float h, float s, float v, float* r, float* g, float* b) {
  h = wrap360(h);
  s = constrain(s, 0.0f, 1.0f);
  v = constrain(v, 0.0f, 1.0f);

  const float c = v * s;
  const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;

  float rr, gg, bb;
  if (h < 60)       { rr = c; gg = x; bb = 0; }
  else if (h < 120) { rr = x; gg = c; bb = 0; }
  else if (h < 180) { rr = 0; gg = c; bb = x; }
  else if (h < 240) { rr = 0; gg = x; bb = c; }
  else if (h < 300) { rr = x; gg = 0; bb = c; }
  else              { rr = c; gg = 0; bb = x; }

  *r = (rr + m) * 255.0f;
  *g = (gg + m) * 255.0f;
  *b = (bb + m) * 255.0f;
}

uint16_t rgb565(float r, float g, float b) {
  const uint16_t r8 = (uint16_t)constrain(r, 0.0f, 255.0f);
  const uint16_t g8 = (uint16_t)constrain(g, 0.0f, 255.0f);
  const uint16_t b8 = (uint16_t)constrain(b, 0.0f, 255.0f);
  return ((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3);
}

// Moves `shown` toward `target` by one frame's worth of easing, and snaps the
// last sliver so the picture actually comes to rest and stops being redrawn.
float easeToward(float shown, float target, float alpha, float snap) {
  const float next = shown + (target - shown) * alpha;
  return fabsf(target - next) < snap ? target : next;
}

// Same, around the hue circle - the short way, so 350 -> 10 does not sweep
// backwards through every colour in between.
float easeHue(float shown, float target, float alpha) {
  const float diff = fmodf(target - shown + 540.0f, 360.0f) - 180.0f;
  if (fabsf(diff * (1.0f - alpha)) < 0.2f) return wrap360(target);
  return wrap360(shown + diff * alpha);
}

// The diamond's radius for a value. Area, not radius, tracks the value: a
// light at 10% then shows a real cluster rather than a single mark, which
// matches how brightness is perceived far better than a linear radius does.
float radiusFor(const State& s) {
  if (!s.on) return 0.0f;
  const float v = constrain(s.value, 0.0f, 1.0f);
  return kMinRadius + (kMaxRadius - kMinRadius) * sqrtf(v);
}

void ease(const State& s, float targetDim) {
  const uint32_t now = millis();
  const float targetRadius = radiusFor(s);
  const float targetWheel = s.colourMode ? 1.0f : 0.0f;

  if (!g_shown.valid) {
    g_shown = {targetRadius, wrap360(s.hue), s.saturation, targetDim,
               targetWheel, true};
    g_lastEaseMs = now;
    return;
  }

  const float dt = (float)(now - g_lastEaseMs);
  g_lastEaseMs = now;
  // A long gap (connecting, backlight off) means the old picture is stale;
  // gliding from it would replay a change that happened long ago.
  const float alpha = dt > 500.0f ? 1.0f : 1.0f - expf(-dt / kEaseTauMs);

  g_shown.radius = easeToward(g_shown.radius, targetRadius, alpha, 0.2f);
  g_shown.hue = easeHue(g_shown.hue, s.hue, alpha);
  g_shown.saturation = easeToward(g_shown.saturation, s.saturation, alpha,
                                  0.004f);
  g_shown.dim = easeToward(g_shown.dim, targetDim, alpha, 0.004f);
  g_shown.wheel = easeToward(g_shown.wheel, targetWheel, alpha, 0.004f);
}

// ------------------------------------------------------------------ build --

void clearFaceText() {
  g_boardOn = false;
  g_textActive = false;
  g_textKey[0] = '\0';
  g_clearW = 0;
  memset(g_textBorn, 0, sizeof(g_textBorn));
}

// Only the text's own lattice spots: the field carries on around and between
// the letters (a cleared box behind them looked like a black hole).
bool inTextBox(int i, int j) {
  const int idx = (j + kHalfCells) * kLattice + (i + kHalfCells);
  return (g_textActive && g_textBorn[idx] != 0) || (g_boardOn && g_board[idx] != 0);
}

bool inClearRect(int x, int y) {
  return g_clearW > 0 && x + sprites::kSize > g_clearX && x < g_clearX + g_clearW &&
         y + sprites::kSize > g_clearY && y < g_clearY + g_clearH;
}

// The text's sprites that are due by now, in the palette's white at full
// strength - the text never dims with the field under it.
void appendBoardCells() {
  if (!g_boardOn) return;
  float wr, wg, wb;
  hsvToRgb(kWarmHue, kWarmSaturation * 0.6f, 1.0f, &wr, &wg, &wb);
  for (int idx = 0; idx < kLattice * kLattice; ++idx) {
    const uint8_t look = g_board[idx];
    if (!look || g_cellCount >= kMaxCells) continue;
    const int i = idx % kLattice - kHalfCells, j = idx / kLattice - kHalfCells;
    Cell& c = g_cells[g_cellCount++];
    c.x = kCentreX + i * kPitch - sprites::kSize / 2;
    c.y = kCentreY + j * kPitch - sprites::kSize / 2;
    switch ((BoardLook)(look - 1)) {
      case BoardLook::Player:
        c.sprite = g_textSprite;
        c.colour = rgb565(wr, wg, wb);
        break;
      case BoardLook::Goal:
        c.sprite = g_goalSprite;
        c.colour = rgb565(kAccentR, kAccentG, kAccentB);
        break;
      case BoardLook::Hazard:
        c.sprite = g_hazardSprite;
        c.colour = rgb565(225, 70, 60);
        break;
      case BoardLook::Dim:
        c.sprite = g_byWeight[2][0];
        c.colour = rgb565(wr * 0.35f, wg * 0.35f, wb * 0.35f);
        break;
      case BoardLook::Lit:
        c.sprite = g_textSprite;
        c.colour = rgb565(kAccentR, kAccentG, kAccentB);
        break;
    }
  }
}

void appendTextCells() {
  appendBoardCells();
  if (!g_textActive) return;
  float r, g, b;
  hsvToRgb(kWarmHue, kWarmSaturation * 0.6f, 1.0f, &r, &g, &b);
  const uint16_t colour = rgb565(r, g, b);
  for (int idx = 0; idx < kLattice * kLattice; ++idx) {
    const uint32_t born = g_textBorn[idx];
    if (!born || (int32_t)(g_effectNow - born) < 0 || g_cellCount >= kMaxCells) continue;
    const int i = idx % kLattice - kHalfCells, j = idx / kLattice - kHalfCells;
    Cell& c = g_cells[g_cellCount++];
    c.x = kCentreX + i * kPitch - sprites::kSize / 2;
    c.y = kCentreY + j * kPitch - sprites::kSize / 2;
    c.sprite = g_textSprite;
    c.colour = colour;
  }
}

void buildCells() {
  g_cellCount = 0;
  g_effectNow = millis();
  const float radius = g_shown.radius;
  const float dim = g_shown.dim;

  // Off, or still fading toward it: a single dim accent mark at the centre.
  // It says "awake, light off" - distinct from a light on at its dimmest,
  // which is a bright cluster in the light's own colour.
  if (radius < kMinRadius * 0.5f) {
    if (g_textActive || g_boardOn) {
      appendTextCells();
      return;
    }
    Cell& c = g_cells[g_cellCount++];
    c.x = kCentreX - sprites::kSize / 2;
    c.y = kCentreY - sprites::kSize / 2;
    c.sprite = g_byWeight[(int)sprites::Weight::Medium][5 % g_weightCount[1]];
    c.colour = rgb565(kAccentR * 0.6f * dim, kAccentG * 0.6f * dim,
                      kAccentB * 0.6f * dim);
    return;
  }

  const bool white = g_shown.saturation < 0.08f;
  float baseR, baseG, baseB;
  hsvToRgb(white ? kWarmHue : g_shown.hue,
           white ? kWarmSaturation : g_shown.saturation, 1.0f, &baseR, &baseG,
           &baseB);
  const float wheel = g_shown.wheel;

  for (int j = -kHalfCells; j <= kHalfCells; ++j) {
    for (int i = -kHalfCells; i <= kHalfCells; ++i) {
      const int dx = i * kPitch;
      const int dy = j * kPitch;
      if (dx * dx + dy * dy > kClipRadius * kClipRadius) continue;
      if (inTextBox(i, j)) continue;
      if (inClearRect(kCentreX + dx - sprites::kSize / 2, kCentreY + dy - sprites::kSize / 2)) {
        continue;
      }

      // Four independent random bytes per cell, fixed for its position.
      const uint32_t h = hash32((uint32_t)(i + 64) * 0x9E3779B1U ^
                                (uint32_t)(j + 64) * 0x85EBCA77U);
      const float fray = (h & 0xFF) / 255.0f;
      const float tierJitter = ((h >> 8) & 0xFF) / 255.0f - 0.5f;
      const float accentRoll = ((h >> 16) & 0xFF) / 255.0f;
      const uint8_t pick = h >> 24;

      // Distance in the diamond's own metric: 0 at the centre, 1 on its edge.
      const float d = (abs(dx) + abs(dy)) / radius;
      if (d >= 1.0f - kFray * fray) continue;

      const float t = d + tierJitter * kTierJitter;
      const int weight = t < kHeavyUntil ? 0 : t < kMediumUntil ? 1 : 2;
      uint8_t step = 0;
      float sparkle = 1.0f;
      if (g_effect == 2) {
        // Drift: every 2.6-4.6 s, at its own moment, a mark changes shape.
        const uint32_t cycle = 2600 + (h & 0x7FF);
        step = (uint8_t)((g_effectNow + (h >> 12) % cycle) / cycle);
      } else if (g_effect == 1 && ((h >> 5) & 0x7) < 3) {
        // Twinkle: brightness on a 1.8-4.2 s sine, phase per mark.
        const float period = 1800.0f + (float)((h >> 9) & 0x3FF) * 2.35f;
        const float phase = (float)((h >> 19) & 0xFF) / 255.0f;
        const float wave = sinf(2.0f * (float)M_PI * (g_effectNow / period + phase));
        sparkle = 0.3f + 0.7f * (0.5f + 0.5f * wave);
      }
      const uint8_t sprite =
          g_byWeight[weight][(uint8_t)(pick + step) % g_weightCount[weight]];

      const bool accent =
          (i | j) != 0 &&  // The centre is always the light's own colour.
          accentRoll < kAccentAtCentre + (kAccentAtEdge - kAccentAtCentre) * d;

      float r, g, b;
      if (g_mosaicOn) {
        // Spinning: each cell samples the cover at its position turned back
        // by the spin angle, so the artwork rotates under a fixed sprite grid.
        const int si = constrain((int)lroundf(i * g_mosaicSpinCos + j * g_mosaicSpinSin),
                                 -kHalfCells, kHalfCells);
        const int sj = constrain((int)lroundf(-i * g_mosaicSpinSin + j * g_mosaicSpinCos),
                                 -kHalfCells, kHalfCells);
        const uint16_t c = g_mosaic[(sj + kHalfCells) * 23 + (si + kHalfCells)];
        const float v = (1.0f - 0.15f * d) * dim;
        r = ((c >> 11) << 3) * v;
        g = (((c >> 5) & 0x3F) << 2) * v;
        b = ((c & 0x1F) << 3) * v;
      } else if (accent) {
        const float v = (0.9f - 0.25f * d) * dim;
        r = kAccentR * v;
        g = kAccentG * v;
        b = kAccentB * v;
        if (wheel > 0.001f) {
          // Accents fade to the hue of their angle: a spectrum around the
          // field while the hue is being turned.
          float wr, wg, wb;
          hsvToRgb(atan2f((float)dy, (float)dx) * (float)RAD_TO_DEG, 0.8f,
                   v * 1.1f, &wr, &wg, &wb);
          r += (wr - r) * wheel;
          g += (wg - g) * wheel;
          b += (wb - b) * wheel;
        }
      } else {
        const float v = (1.0f - 0.3f * d) * dim;
        r = baseR * v;
        g = baseG * v;
        b = baseB * v;
      }

      Cell& c = g_cells[g_cellCount++];
      c.x = kCentreX + dx - sprites::kSize / 2;
      c.y = kCentreY + dy - sprites::kSize / 2;
      c.sprite = sprite;
      c.colour = rgb565(r * sparkle, g * sparkle, b * sparkle);
    }
  }
  appendTextCells();
}

void buildPickerDots(int index, int count) {
  g_pickerDotCount = min(count, kMaxPickerDots);
  constexpr int kSpacing = 12;
  const int startX = kCentreX - (g_pickerDotCount - 1) * kSpacing / 2;
  for (int i = 0; i < g_pickerDotCount; ++i) {
    const bool current = (i == index);
    g_pickerDots[i] = {(int16_t)(startX + i * kSpacing),
                       (int16_t)(kCentreY + 30), (uint8_t)(current ? 3 : 2),
                       current ? (uint16_t)0xFFFF : rgb565(80, 80, 80)};
  }
}

// Renders `text` into label `slot` at twice the size and box-filters it down,
// which is how a 1-bit GFX font gets anti-aliased edges. Rebuilt only when the
// text or font changes. `centreY` is where the label's middle sits.
void buildLabel(int slot, const char* text, const lgfx::IFont* font,
                int centreY, uint8_t scale = 1) {
  Label& L = g_labels[slot];
  if (L.mask && L.font == font && L.scale == scale &&
      strncmp(text, L.text, sizeof(L.text)) == 0) {
    L.y = centreY - L.h / 2;
    return;
  }
  strncpy(L.text, text, sizeof(L.text) - 1);
  L.text[sizeof(L.text) - 1] = '\0';
  L.font = font;
  L.scale = scale;
  free(L.mask);
  L.mask = nullptr;
  ++g_labelGeneration;
  if (!L.text[0]) return;

  LGFX_Sprite big;
  big.setColorDepth(8);
  big.setFont(font);
  big.setTextSize(scale);
  constexpr int kMaxW = 200;  // Usable chord of the circle near the middle.
  const int w = min(kMaxW, (int)big.textWidth(L.text) / 2 + 2);
  const int h = (int)big.fontHeight() / 2 + 2;
  if (!big.createSprite(w * 2, h * 2)) return;
  big.fillScreen(0);
  big.setTextColor(TFT_WHITE);
  big.setTextDatum(top_left);
  big.drawString(L.text, 2, 2);

  L.mask = (uint8_t*)malloc(w * h);
  if (!L.mask) {
    big.deleteSprite();
    return;
  }
  const uint8_t* src = (const uint8_t*)big.getBuffer();
  const int srcW = w * 2;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t* s = src + (y * 2) * srcW + x * 2;
      const int ink = (s[0] != 0) + (s[1] != 0) + (s[srcW] != 0) +
                      (s[srcW + 1] != 0);
      L.mask[y * w + x] = (uint8_t)(ink * 255 / 4);
    }
  }
  big.deleteSprite();
  L.w = w;
  L.h = h;
  L.x = (display::kWidth - w) / 2;
  L.y = centreY - h / 2;
}

// Lights the rim ring clockwise from 12 o'clock up to `volume`. Ring cells
// already in g_cells are recoloured; missing ones are added, so the ring
// always shows in full even past the current diamond.
// Small face text: the same 5-row font, each dot a square (`dot` px on a
// `pitch` px grid) - pixel-sharp, no smoothing needed. Lines split on '\n'
// are each centred; the block is centred on `centreY`.
void buildPixelLabel(int slot, const char* text, int centreY, int pitch = 4, int dot = 3) {
  Label& L = g_labels[slot];
  const uint8_t key = (uint8_t)(0xE0 | pitch);
  if (L.mask && L.font == nullptr && L.scale == key &&
      strncmp(text, L.text, sizeof(L.text)) == 0) {
    L.y = centreY - L.h / 2;
    return;
  }
  strncpy(L.text, text, sizeof(L.text) - 1);
  L.text[sizeof(L.text) - 1] = '\0';
  L.font = nullptr;
  L.scale = key;
  free(L.mask);
  L.mask = nullptr;
  ++g_labelGeneration;
  if (!L.text[0]) return;

  // Split into lines, measure the widest.
  std::vector<std::string> lines;
  {
    std::string cur;
    for (const char* p = text; *p; ++p) {
      if (*p == '\n') {
        lines.push_back(cur);
        cur.clear();
      } else {
        cur += *p;
      }
    }
    lines.push_back(cur);
  }
  constexpr int kLineGap = 2;  // Rows of dots between lines.
  int maxCols = 0;
  std::vector<int> widths;
  for (const auto& l : lines) {
    int c = 0;
    pixel_font::layout(l.c_str(), &c);
    widths.push_back(c);
    maxCols = max(maxCols, c);
  }
  const int w = maxCols * pitch;
  const int rowsTotal = (int)lines.size() * pixel_font::kRows + ((int)lines.size() - 1) * kLineGap;
  const int h = rowsTotal * pitch;
  if (w <= 0 || h <= 0) return;
  L.mask = (uint8_t*)calloc(w * h, 1);
  if (!L.mask) return;
  for (size_t li = 0; li < lines.size(); ++li) {
    int c = 0;
    const auto dots = pixel_font::layout(lines[li].c_str(), &c);
    const int x0 = (maxCols - c) / 2 * pitch;
    const int y0 = (int)li * (pixel_font::kRows + kLineGap) * pitch;
    for (const auto& d : dots) {
      for (int y = 0; y < dot; ++y)
        for (int x = 0; x < dot; ++x)
          L.mask[(y0 + d.row * pitch + y) * w + x0 + d.col * pitch + x] = 255;
    }
  }
  L.w = w;
  L.h = h;
  L.x = (display::kWidth - w) / 2;
  L.y = centreY - h / 2;
}

void applyRing(float volume, uint8_t r, uint8_t g, uint8_t b) {
  constexpr int kInner = 92, kOuter = kClipRadius;
  const uint16_t lit = rgb565(r, g, b);
  const uint16_t unlit = rgb565(r * 0.18f, g * 0.18f, b * 0.18f);
  for (int j = -kHalfCells; j <= kHalfCells; ++j) {
    for (int i = -kHalfCells; i <= kHalfCells; ++i) {
      const int dx = i * kPitch, dy = j * kPitch;
      const int d2 = dx * dx + dy * dy;
      if (d2 < kInner * kInner || d2 > kOuter * kOuter) continue;
      // Angle clockwise from 12 o'clock, 0..1.
      float a = atan2f((float)dx, (float)-dy) / (2.0f * (float)M_PI);
      if (a < 0) a += 1.0f;
      const int x = kCentreX + dx - sprites::kSize / 2;
      const int y = kCentreY + dy - sprites::kSize / 2;
      int found = -1;
      for (int k = 0; k < g_cellCount; ++k) {
        if (g_cells[k].x == x && g_cells[k].y == y) {
          found = k;
          break;
        }
      }
      if (found < 0) {
        if (g_cellCount >= kMaxCells) continue;
        found = g_cellCount++;
        g_cells[found].x = x;
        g_cells[found].y = y;
        g_cells[found].sprite =
            g_byWeight[2][(i * 7 + j * 3 + 64) % g_weightCount[2]];
      }
      g_cells[found].colour = a <= volume ? lit : unlit;
    }
  }
}

void applyVolumeRing(float volume) {
  applyRing(volume, kAccentR, kAccentG, kAccentB);
}

// ------------------------------------------------------------- rasterise --

void blitCell(uint16_t* band, int bandTop, const Cell& c) {
  const uint16_t* rows = g_bitmaps[c.sprite];
  for (int gy = 0; gy < sprites::kSize; ++gy) {
    const int localY = c.y + gy - bandTop;
    if (localY < 0 || localY >= kBandHeight) continue;
    const uint16_t row = rows[gy];
    if (row == 0) continue;

    uint16_t* dest = band + localY * display::kWidth;
    for (int gx = 0; gx < sprites::kSize; ++gx) {
      if (!((row >> (sprites::kSize - 1 - gx)) & 1)) continue;
      const int x = c.x + gx;
      if (x < 0 || x >= display::kWidth) continue;
      dest[x] = c.colour;
    }
  }
}

void blitDot(uint16_t* band, int bandTop, const Dot& d) {
  for (int y = d.y - d.r; y <= d.y + d.r; ++y) {
    const int localY = y - bandTop;
    if (localY < 0 || localY >= kBandHeight) continue;
    for (int x = d.x - d.r; x <= d.x + d.r; ++x) {
      const int ddx = x - d.x, ddy = y - d.y;
      if (ddx * ddx + ddy * ddy > d.r * d.r) continue;
      band[localY * display::kWidth + x] = d.colour;
    }
  }
}

void compositeLabel(const Label& L, uint16_t* band, int bandTop) {
  if (!L.mask) return;
  const int y0 = max(bandTop, L.y);
  const int y1 = min(bandTop + kBandHeight, L.y + L.h);
  for (int y = y0; y < y1; ++y) {
    const uint8_t* mask = L.mask + (y - L.y) * L.w;
    uint16_t* row = band + (y - bandTop) * display::kWidth + L.x;
    for (int x = 0; x < L.w; ++x) {
      const uint32_t a = mask[x];
      if (a == 0) continue;
      const uint16_t old = row[x];
      const uint32_t r = ((old >> 11) * (255 - a) + 0x1F * a) / 255;
      const uint32_t g = (((old >> 5) & 0x3F) * (255 - a) + 0x3F * a) / 255;
      const uint32_t b = ((old & 0x1F) * (255 - a) + 0x1F * a) / 255;
      row[x] = (r << 11) | (g << 5) | b;
    }
  }
}

void push(bool picker) {
  display::lcd.startWrite();
  for (int bandTop = 0; bandTop < display::kHeight; bandTop += kBandHeight) {
    uint16_t* band = g_band[g_activeBand];
    memset(band, 0, kBandPixels * sizeof(uint16_t));

    for (int i = 0; i < g_cellCount; ++i) {
      const Cell& c = g_cells[i];
      if (c.y + sprites::kSize <= bandTop || c.y >= bandTop + kBandHeight) {
        continue;
      }
      blitCell(band, bandTop, c);
    }
    if (picker) {
      for (int i = 0; i < g_pickerDotCount; ++i) {
        blitDot(band, bandTop, g_pickerDots[i]);
      }
    }
    for (int i = 0; i < 2; ++i) {
      if (g_labelOn[i]) compositeLabel(g_labels[i], band, bandTop);
    }

    // The band is composed in native RGB565, but the GC9A01 reads each pixel
    // high byte first, and pushImageDMA sends the buffer as it sits in memory
    // - little-endian. Without the swap every colour is scrambled: the pink of
    // the bulb came out pale yellow and the green accent a dull lavender.
    for (int i = 0; i < kBandPixels; ++i) band[i] = __builtin_bswap16(band[i]);

    display::lcd.pushImageDMA(0, bandTop, display::kWidth, kBandHeight, band);
    g_activeBand ^= 1;  // Fill the other buffer while this one is in flight.
  }
  display::lcd.waitDMA();
  display::lcd.endWrite();
}

Signature signatureNow(bool picker, int index, int count) {
  Signature s{};
  // Quantised at roughly the smallest step that changes a pixel, so an eased
  // value creeping by nothing visible does not cost a full frame.
  s.radius = lroundf(g_shown.radius * 4.0f);
  s.hue = lroundf(g_shown.hue * 4.0f);
  s.saturation = lroundf(g_shown.saturation * 500.0f);
  s.dim = lroundf(g_shown.dim * 500.0f);
  s.wheel = lroundf(g_shown.wheel * 500.0f);
  s.effectFrame = g_effect ? (int32_t)(millis() / kEffectFrameMs) : 0;
  s.picker = picker;
  s.pickIndex = picker ? index : 0;
  s.pickCount = picker ? count : 0;
  return s;
}

bool sameAsDrawn(const Signature& s) {
  return g_drawnValid && s.radius == g_drawn.radius && s.hue == g_drawn.hue &&
         s.saturation == g_drawn.saturation && s.dim == g_drawn.dim &&
         s.wheel == g_drawn.wheel && s.effectFrame == g_drawn.effectFrame &&
         s.picker == g_drawn.picker &&
         s.pickIndex == g_drawn.pickIndex && s.pickCount == g_drawn.pickCount;
}

}  // namespace

void loadPack(int pack) {
  if (pack < 0 || pack >= sprites::kPackCount) pack = 0;
  const sprites::Sprite* set = sprites::kPacks[pack];
  g_weightCount[0] = g_weightCount[1] = g_weightCount[2] = 0;
  // Text art to row bitmasks, MSB-first so column 0 is the high bit.
  for (int s = 0; s < sprites::kCount; ++s) {
    for (int y = 0; y < sprites::kSize; ++y) {
      uint16_t row = 0;
      for (int x = 0; x < sprites::kSize; ++x) {
        row = (row << 1) | (set[s].rows[y][x] == '#' ? 1 : 0);
      }
      g_bitmaps[s][y] = row;
    }
    const int w = (int)set[s].weight;
    g_byWeight[w][g_weightCount[w]++] = (uint8_t)s;
  }
  // Text dots use the pack's boldest heavy mark: the most lit pixels reads
  // best as a dot of a letter.
  uint8_t ranked[sprites::kCount];
  int lit[sprites::kCount];
  const int n = g_weightCount[0];
  for (int k = 0; k < n; ++k) {
    ranked[k] = g_byWeight[0][k];
    lit[k] = 0;
    for (int y = 0; y < sprites::kSize; ++y) lit[k] += __builtin_popcount(g_bitmaps[ranked[k]][y]);
  }
  for (int a = 0; a < n; ++a)  // Most pixels first.
    for (int b = a + 1; b < n; ++b)
      if (lit[b] > lit[a]) {
        std::swap(lit[a], lit[b]);
        std::swap(ranked[a], ranked[b]);
      }
  g_textSprite = ranked[0];
  g_goalSprite = ranked[n > 1 ? 1 : 0];
  g_hazardSprite = ranked[n > 2 ? n - 1 : 0];
}

void begin() {
  for (int i = 0; i < 2; ++i) {
    g_band[i] = (uint16_t*)heap_caps_malloc(kBandPixels * sizeof(uint16_t),
                                            MALLOC_CAP_DMA);
  }
  g_activeBand = 0;

  loadPack(0);

  g_shown.valid = false;
  g_drawnValid = false;
}

void draw(const State& s) {
  if (!g_band[0] || !g_band[1]) return;
  if (g_textActive || g_clearW || g_boardOn) clearFaceText();

  ease(s, 1.0f);
  const Signature sig = signatureNow(false, 0, 0);
  if (sameAsDrawn(sig)) return;

  g_labelOn[0] = g_labelOn[1] = false;
  buildCells();
  push(false);
  g_drawn = sig;
  g_drawnValid = true;
}

void drawPicker(const char* name, int index, int count, const State& s) {
  if (!g_band[0] || !g_band[1]) return;
  if (g_textActive || g_clearW || g_boardOn) clearFaceText();

  // The field stays visible but recedes, so the picker reads as a layer over
  // the device's normal state rather than as a different screen.
  ease(s, 0.25f);
  buildLabel(0, name, &fonts::FreeSansBold18pt7b, kCentreY - 4);
  g_labelOn[0] = true;
  g_labelOn[1] = false;

  const Signature sig = signatureNow(true, index, count);
  if (sameAsDrawn(sig)) return;

  buildCells();
  buildPickerDots(index, count);
  push(true);
  g_drawn = sig;
  g_drawnValid = true;
}

void drawMusic(const MusicScreen& m) {
  if (!g_band[0] || !g_band[1]) return;
  if (g_textActive || g_clearW || g_boardOn) clearFaceText();

  // Warm white field; palettes arrive with the sprite-pack phase.
  State s{};
  s.hue = 30.0f;
  s.saturation = 0.0f;
  s.colourMode = false;
  s.on = m.connected;
  // A cover fills the whole circle while playing; paused, its centre stays as
  // a small cluster so the knob still shows what is queued.
  if (g_mosaicOn) {
    s.value = m.playing ? 1.0f : 0.15f;
  } else {
    s.value = m.playing ? 0.55f : 0.04f;
  }

  ease(s, m.showTitle ? 0.25f : 1.0f);

  if (m.showTitle && !g_titleFromApp) {
    buildLabel(0, m.title ? m.title : "", &fonts::FreeSansBold18pt7b,
               kCentreY - 10);
    buildLabel(1, m.artist ? m.artist : "", &fonts::FreeSans12pt7b,
               kCentreY + 18);
  }
  g_labelOn[0] = m.showTitle && g_labels[0].mask;
  g_labelOn[1] = m.showTitle && !g_titleFromApp && g_labels[1].mask;

  const float spin = m.spinDegrees * (float)DEG_TO_RAD;
  g_mosaicSpinCos = cosf(spin);
  g_mosaicSpinSin = sinf(spin);

  Signature sig = signatureNow(false, 0, 0);
  // Fold the music-only inputs into the signature so a change redraws.
  sig.saturation += lroundf(fmodf(m.spinDegrees, 360.0f) * 2.0f) * 1000;
  sig.pickIndex = (int16_t)((m.showVolume ? 1 : 0) | (m.showTitle ? 2 : 0));
  sig.pickCount = (int16_t)lroundf(m.volume * 1000.0f);
  sig.hue = g_mosaicOn ? -1 - (int32_t)g_mosaicGeneration : sig.hue;
  sig.wheel += (int32_t)g_titleGeneration * 1000;
  if (sameAsDrawn(sig)) return;

  buildCells();
  if (m.showVolume) applyVolumeRing(m.volume);
  push(false);
  g_drawn = sig;
  g_drawnValid = true;
}

namespace {

// Lays `text` out on the lattice, rows `top`..`top`+4, centred. Dots already
// lit by the previous text stay as they are; new ones get staggered times
// so a changed digit builds up rather than popping in.
void setFaceText(const char* text, int top) {
  if (strncmp(text, g_textKey, sizeof(g_textKey)) == 0 && top == g_textTop) return;
  strncpy(g_textKey, text, sizeof(g_textKey) - 1);
  g_textKey[sizeof(g_textKey) - 1] = '\0';
  g_textTop = top;
  ++g_labelGeneration;

  int width = 0;
  const auto dots = pixel_font::layout(text, &width);
  bool on[kLattice * kLattice] = {};
  g_textMinI = g_textMinJ = 99;
  g_textMaxI = g_textMaxJ = -99;
  for (const auto& d : dots) {
    const int i = d.col - width / 2, j = d.row + top;
    if (i < -kHalfCells || i > kHalfCells || j < -kHalfCells || j > kHalfCells) continue;
    on[(j + kHalfCells) * kLattice + (i + kHalfCells)] = true;
    g_textMinI = min(g_textMinI, i);
    g_textMaxI = max(g_textMaxI, i);
    g_textMinJ = min(g_textMinJ, j);
    g_textMaxJ = max(g_textMaxJ, j);
  }
  g_textActive = !dots.empty();
  const uint32_t now = millis();
  for (int idx = 0; idx < kLattice * kLattice; ++idx) {
    if (!on[idx]) {
      g_textBorn[idx] = 0;
    } else if (!g_textBorn[idx]) {
      g_textBorn[idx] = (now + 1 + (hash32((uint32_t)idx * 2654435761u) % 7) * 17) | 1;
    }
  }
}

// How many text sprites are showing by now: changes while a digit builds up,
// which is what keeps the frames coming until it is done.
int visibleTextCells() {
  if (!g_textActive) return 0;
  const uint32_t now = millis();
  int n = 0;
  for (uint32_t born : g_textBorn) n += born && (int32_t)(now - born) >= 0;
  return n;
}

}  // namespace

void drawBoard(const BoardCell* cells, int count, const Face& f) {
  uint8_t next[kLattice * kLattice] = {};
  for (int k = 0; k < count; ++k) {
    const int i = cells[k].i, j = cells[k].j;
    if (i < -kHalfCells || i > kHalfCells || j < -kHalfCells || j > kHalfCells) continue;
    next[(j + kHalfCells) * kLattice + (i + kHalfCells)] = (uint8_t)cells[k].look + 1;
  }
  if (memcmp(next, g_board, sizeof next) != 0 || !g_boardOn) {
    memcpy(g_board, next, sizeof next);
    ++g_boardGeneration;
  }
  g_boardOn = true;
  g_drawingBoard = true;
  drawFace(f);
  g_drawingBoard = false;
}

void drawFace(const Face& f) {
  if (!g_band[0] || !g_band[1]) return;
  if (!g_drawingBoard && g_boardOn) {
    g_boardOn = false;  // A plain face: the last game's pieces go.
    ++g_boardGeneration;
  }

  ease(f.field, f.dim);
  const bool two = f.small && f.small[0];
  // Big text in sprites, 5 rows (55 px) tall; the small line under it in the
  // same font as little squares.
  if (f.lines && f.lines[0]) {
    // A block of small lines instead (notifications): no big text.
    setFaceText("", 0);
    buildPixelLabel(1, f.lines, kCentreY, 3, 2);
  } else {
    setFaceText(f.big ? f.big : "", two ? -3 : -2);
    buildPixelLabel(1, two ? f.small : "", kCentreY + 42);
  }
  g_labelOn[0] = false;
  g_labelOn[1] = (two || (f.lines && f.lines[0])) && g_labels[1].mask != nullptr;
  g_titleFromApp = false;
  g_clearW = 0;  // The small line sits over the field, like the big one.

  Signature sig = signatureNow(false, 0, 0);
  sig.wheel += (int32_t)g_labelGeneration * 1000;
  sig.dim += visibleTextCells() * 100000;
  sig.radius += (int32_t)(g_boardGeneration & 0xFFFF) * 100000;
  sig.pickIndex = f.ring >= 0.0f ? 1 : 0;
  // The ring moves in whole sprite steps; a finer key would redraw for
  // nothing every frame.
  sig.pickCount = f.ring >= 0.0f ? (int16_t)lroundf(f.ring * 400.0f) : 0;
  if (sameAsDrawn(sig)) return;

  buildCells();
  if (f.ring >= 0.0f) applyRing(f.ring, f.ringR, f.ringG, f.ringB);
  push(false);
  g_drawn = sig;
  g_drawnValid = true;
}

void setMosaic(const uint16_t* grid) {
  if (grid) {
    memcpy(g_mosaic, grid, sizeof(g_mosaic));
    g_mosaicOn = true;
  } else {
    g_mosaicOn = false;
  }
  ++g_mosaicGeneration;
}

void invalidate() { g_drawnValid = false; }

void setLook(const knob::KnobSettings& s) {
  loadPack(s.spritePack);
  if (s.palette == knob::kCustomPalette) {
    // The field's "white" is kept as hue and saturation (it is drawn at many
    // brightnesses), so the picked colour is reduced to those.
    const float r = s.customWhite[0] / 255.0f, g = s.customWhite[1] / 255.0f,
                b = s.customWhite[2] / 255.0f;
    const float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn;
    float h = 0.0f;
    if (d > 0.0f) {
      if (mx == r) h = 60.0f * fmodf((g - b) / d, 6.0f);
      else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
      else h = 60.0f * ((r - g) / d + 4.0f);
    }
    kWarmHue = wrap360(h);
    kWarmSaturation = mx > 0.0f ? d / mx : 0.0f;
    kAccentR = s.customAccent[0];
    kAccentG = s.customAccent[1];
    kAccentB = s.customAccent[2];
  } else {
    const sprites::Palette& p =
        sprites::kPalettes[s.palette < sprites::kPaletteCount ? s.palette : 0];
    kAccentR = p.accentR;
    kAccentG = p.accentG;
    kAccentB = p.accentB;
    kWarmHue = p.whiteHue;
    kWarmSaturation = p.whiteSaturation;
  }
  g_effect = s.effect <= 2 ? s.effect : 0;
  g_drawnValid = false;
}

void clearLabels() {
  clearFaceText();
  for (Label& L : g_labels) {
    free(L.mask);
    L.mask = nullptr;
    L.text[0] = '\0';
    L.font = nullptr;
  }
  g_labelOn[0] = g_labelOn[1] = false;
  g_titleFromApp = false;
  ++g_labelGeneration;
  g_drawnValid = false;
}

void setTitleMask(int width, int height, const uint8_t* alpha) {
  Label& L = g_labels[0];
  free(L.mask);
  L.mask = nullptr;
  L.text[0] = '\0';  // Forces the built-in font to rebuild if it is needed.
  L.font = nullptr;
  g_titleFromApp = false;
  ++g_titleGeneration;
  if (!alpha || width <= 0 || height <= 0 || width > knob::kTitleMaxWidth ||
      height > knob::kTitleMaxHeight) {
    return;
  }
  L.mask = (uint8_t*)malloc(width * height);
  if (!L.mask) return;
  memcpy(L.mask, alpha, width * height);
  L.w = width;
  L.h = height;
  L.x = (display::kWidth - width) / 2;
  L.y = kCentreY - height / 2;
  g_titleFromApp = true;
}

}  // namespace field
