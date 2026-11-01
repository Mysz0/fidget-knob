#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <math.h>
#include <stdio.h>

#include <vector>

#include "app/desk_modes.h"
#include "haptics/haptics.h"
#include "render/display.h"
#include "render/field.h"

// Two slow games on the sprite lattice, both steered by tilting the knob.
//   Tilt:   a marble rolls cell by cell; collect the accent dots, avoid the
//           red marks (one more every three points).
//   Memory: directions light up in a growing sequence; repeat them by
//           tipping the knob that way.
// The screen only changes when something moves, so neither needs a fast
// refresh.
namespace games_mode {
namespace {

using field::BoardCell;
using field::BoardLook;

enum class Game : uint8_t { Tilt = 0, Memory = 1 };
enum class Phase : uint8_t { Menu, Play, Show, Input, Over };

Phase g_phase = Phase::Menu;
Game g_game = Game::Tilt;
int g_twist = 0;
uint32_t g_lastFrameMs = 0;

// ------------------------------------------------------------ directions --

// Screen up and right in the sensor's x/y plane, measured on this knob held
// upright and readable: gravity read (0, -1). A first-play calibration asked
// to "tip this way" used to set these, but tipping towards the arrow and
// lifting it give opposite answers - and mirrored the controls.
constexpr float g_upX = 0.0f, g_upY = -1.0f, g_rightX = 1.0f, g_rightY = 0.0f;

// Which way a marble would roll, in screen terms: x right, y up, in g. The
// accelerometer reads towards the raised side, so rolling is the opposite.
void roll(float* x, float* y) {
  float g[3];
  gestures::gravityFast(g);
  *x = -(g[0] * g_rightX + g[1] * g_rightY);
  *y = -(g[0] * g_upX + g[1] * g_upY);
}

// --------------------------------------------------------------- scores --

int g_best[2] = {0, 0};
int g_score = 0;

void loadBest() {
  Preferences p;
  if (!p.begin("games", true)) return;
  g_best[0] = p.getInt("bestT", 0);
  g_best[1] = p.getInt("bestM", 0);
  p.end();
}

void saveBest() {
  Preferences p;
  if (!p.begin("games", false)) return;
  p.putInt("bestT", g_best[0]);
  p.putInt("bestM", g_best[1]);
  p.end();
}

void gameOver() {
  int& best = g_best[(int)g_game];
  if (g_score > best) {
    best = g_score;
    saveBest();
  }
  g_phase = Phase::Over;
  haptics::play(haptics::Pattern::Limit);
}

// ------------------------------------------------------------------ tilt --

constexpr int kArena = 9;  // Cells from the centre to the arena's edge.
struct Spot {
  int8_t i, j;
};
Spot g_marble{0, 0};
std::vector<Spot> g_goals, g_hazards;
uint32_t g_nextMoveMs = 0;
bool g_againstWall = false;

bool inArena(int i, int j) { return i * i + j * j <= kArena * kArena; }

bool occupied(int i, int j) {
  if (g_marble.i == i && g_marble.j == j) return true;
  for (auto& s : g_goals)
    if (s.i == i && s.j == j) return true;
  for (auto& s : g_hazards)
    if (s.i == i && s.j == j) return true;
  return false;
}

// A free spot, not right next to the marble (no unavoidable hazards).
Spot freeSpot() {
  for (int tries = 0; tries < 200; ++tries) {
    const int i = (int)(esp_random() % (2 * kArena + 1)) - kArena;
    const int j = (int)(esp_random() % (2 * kArena + 1)) - kArena;
    if (!inArena(i, j) || occupied(i, j)) continue;
    if (abs(i - g_marble.i) <= 2 && abs(j - g_marble.j) <= 2) continue;
    return Spot{(int8_t)i, (int8_t)j};
  }
  return Spot{(int8_t)kArena, 0};
}

void startTilt() {
  g_marble = {0, 0};
  g_goals.clear();
  g_hazards.clear();
  g_goals.push_back(freeSpot());
  g_goals.push_back(freeSpot());
  g_score = 0;
  g_nextMoveMs = millis() + 600;
  g_phase = Phase::Play;
}

void stepTilt(uint32_t now) {
  if ((int32_t)(now - g_nextMoveMs) < 0) return;
  float x, y;
  roll(&x, &y);
  const float m = sqrtf(x * x + y * y);
  if (m < 0.12f) {
    g_nextMoveMs = now + 60;
    return;
  }
  // Steeper tilt, faster roll: one cell every 400 ms down to every 110 ms.
  g_nextMoveMs = now + (uint32_t)constrain(400.0f - 700.0f * (m - 0.12f), 110.0f, 400.0f);
  int di = fabsf(x) > 0.38f * m ? (x > 0 ? 1 : -1) : 0;
  int dj = fabsf(y) > 0.38f * m ? (y > 0 ? -1 : 1) : 0;  // Lattice j grows downward.
  const int ni = g_marble.i + di, nj = g_marble.j + dj;
  if (!inArena(ni, nj)) {
    if (!g_againstWall) haptics::play(haptics::Pattern::Detent);
    g_againstWall = true;
    return;
  }
  g_againstWall = false;
  g_marble = {(int8_t)ni, (int8_t)nj};
  for (auto& h : g_hazards) {
    if (h.i == ni && h.j == nj) {
      gameOver();
      return;
    }
  }
  for (auto& gl : g_goals) {
    if (gl.i == ni && gl.j == nj) {
      ++g_score;
      haptics::play(haptics::Pattern::Confirm);
      gl = freeSpot();
      if (g_score % 3 == 0) g_hazards.push_back(freeSpot());
      break;
    }
  }
}

// ---------------------------------------------------------------- memory --

std::vector<uint8_t> g_sequence;  // 0 up, 1 right, 2 down, 3 left.
size_t g_step = 0;
uint32_t g_phaseSinceMs = 0;
int g_lit = -1;          // Direction lit right now, or -1.
bool g_needLevel = false;  // A move was just taken; waiting for the next.
int g_lastDir = -1;          // Direction of that move.
float g_peak = 0.0f;         // How far that tip went.
// Tips are measured from how the knob is being held, not from flat: in the
// hand it rests tipped towards you, which read as a permanent tip and the
// game never took a move. Holding still for a moment sets the rest pose.
float g_restX = 0, g_restY = 0;
float g_lastX = 0, g_lastY = 0;
uint32_t g_lastSampleMs = 0;
uint32_t g_steadySinceMs = 0;

void nextRound() {
  g_sequence.push_back((uint8_t)(esp_random() % 4));
  g_step = 0;
  g_lit = -1;
  g_phase = Phase::Show;
  g_phaseSinceMs = millis() + 500;  // A beat before it starts.
}

void startMemory() {
  g_sequence.clear();
  g_score = 0;
  nextRound();
}

void stepMemory(uint32_t now) {
  constexpr uint32_t kOnMs = 520, kGapMs = 220;
  if (g_phase == Phase::Show) {
    if ((int32_t)(now - g_phaseSinceMs) < 0) return;
    const uint32_t t = now - g_phaseSinceMs;
    const size_t i = t / (kOnMs + kGapMs);
    if (i >= g_sequence.size()) {
      g_lit = -1;
      g_phase = Phase::Input;
      g_step = 0;
      roll(&g_restX, &g_restY);  // However it is held now is "level".
      g_lastX = g_restX;
      g_lastY = g_restY;
      g_lastSampleMs = now;
      g_steadySinceMs = now;
      g_needLevel = false;
      return;
    }
    const bool on = (t % (kOnMs + kGapMs)) < kOnMs;
    const int lit = on ? g_sequence[i] : -1;
    if (lit != g_lit && lit >= 0) haptics::play(haptics::Pattern::Detent);
    g_lit = lit;
    return;
  }
  // Input: tip towards a direction (from the rest pose), come back, next.
  float rx, ry;
  roll(&rx, &ry);
  // Held still for 0.6 s: that is the rest pose now, wherever it is.
  // Movement is judged over 100 ms steps: this runs every loop (~1 ms), and
  // gravity hardly changes in a millisecond - per call, everything looked
  // still, the rest pose crept along with every tip, and no tip counted.
  if (now - g_lastSampleMs >= 100) {
    const float moved = fabsf(rx - g_lastX) + fabsf(ry - g_lastY);
    g_lastX = rx;
    g_lastY = ry;
    g_lastSampleMs = now;
    if (moved > 0.03f) g_steadySinceMs = now;
  }
  const bool steady = now - g_steadySinceMs > 600;

  const float x = rx - g_restX, y = ry - g_restY;
  const float m = sqrtf(x * x + y * y);
  // Measured on a recorded game: deliberate tips peak at 0.42-0.81, the
  // swing back past rest after one at ~0.33. A move needs 0.38 from rest.
  constexpr float kTip = 0.38f;
  // Straight from one tip into another (left, then up, without stopping at
  // rest) - the natural way to play. The old rule waited for rest first, so
  // the second tip was ignored and the swing back counted instead.
  constexpr float kDirectTip = 0.45f;
  const int dirNow = fabsf(y) >= fabsf(x) ? (y > 0 ? 0 : 2) : (x > 0 ? 1 : 3);
  const bool newDirection = g_needLevel && m >= kDirectTip && dirNow != g_lastDir;
  if (g_needLevel && !newDirection) {
    // Back near rest - or held at a new pose long enough (1.2 s) that it is
    // the new rest rather than a tip still being held.
    g_peak = max(g_peak, m);
    const bool settled = now - g_steadySinceMs > 1000;
    // Back is "most of the way back": under 45% of how far the tip went, or
    // under 0.2 - the old 0.14 from the exact start pose kept a knob that
    // was plainly back in the "still tipped" state.
    if (m < 0.2f || m < 0.45f * g_peak || settled) {
      g_needLevel = false;
      g_lit = -1;
      if (settled) {
        g_restX = rx;
        g_restY = ry;
      }
    }
    return;
  }
  if (!newDirection && steady && m < 0.25f) {  // Drifted slowly: follow it.
    g_restX = rx;
    g_restY = ry;
    return;
  }
  if (!newDirection && m < kTip) return;
  const int dir = dirNow;
  g_lastDir = dir;
  g_peak = m;

  g_lit = dir;
  g_needLevel = true;
  if (dir != g_sequence[g_step]) {
    g_score = (int)g_sequence.size() - 1;
    gameOver();
    return;
  }
  haptics::play(haptics::Pattern::Detent);
  if (++g_step == g_sequence.size()) {
    g_score = (int)g_sequence.size();
    haptics::play(haptics::Pattern::Confirm);
    nextRound();
    g_phaseSinceMs = millis() + 700;
  }
}

// The four arcs of the memory board: lattice cells near the rim, within 35
// degrees of each direction.
void addArcs(std::vector<BoardCell>* cells) {
  for (int j = -11; j <= 11; ++j) {
    for (int i = -11; i <= 11; ++i) {
      const float r = sqrtf((float)(i * i + j * j));
      if (r < 6.5f || r > 9.6f) continue;
      const float a = atan2f((float)i, (float)-j) * 180.0f / (float)M_PI;  // 0 = up, 90 = right.
      int dir = -1;
      for (int d = 0; d < 4; ++d) {
        float diff = fabsf(fmodf(a - d * 90.0f + 540.0f, 360.0f) - 180.0f);
        if (diff <= 35.0f) dir = d;
      }
      if (dir < 0) continue;
      cells->push_back(BoardCell{(int8_t)i, (int8_t)j, dir == g_lit ? BoardLook::Lit : BoardLook::Dim});
    }
  }
}

// ------------------------------------------------------------------ draw --

void draw() {
  std::vector<BoardCell> cells;
  field::Face f{};
  f.field.on = true;
  f.field.saturation = 0.0f;
  f.field.value = 0.35f;
  f.dim = 0.14f;
  f.ring = -1.0f;
  char big[12] = "", small[24] = "";
  switch (g_phase) {
    case Phase::Menu:
      snprintf(big, sizeof big, "%s", g_game == Game::Tilt ? "TILT" : "MEMO");
      snprintf(small, sizeof small, "BEST %d", g_best[(int)g_game]);
      break;
    case Phase::Play:
      for (auto& h : g_hazards) cells.push_back(BoardCell{h.i, h.j, BoardLook::Hazard});
      for (auto& gl : g_goals) cells.push_back(BoardCell{gl.i, gl.j, BoardLook::Goal});
      cells.push_back(BoardCell{g_marble.i, g_marble.j, BoardLook::Player});
      break;
    case Phase::Show:
    case Phase::Input:
      addArcs(&cells);
      snprintf(big, sizeof big, "%d", (int)g_sequence.size());
      break;
    case Phase::Over:
      snprintf(big, sizeof big, "%d", g_score);
      snprintf(small, sizeof small, g_score >= g_best[(int)g_game] && g_score > 0 ? "NEW BEST" : "BEST %d",
               g_best[(int)g_game]);
      break;
  }
  f.big = big;
  f.small = small;
  field::drawBoard(cells.data(), (int)cells.size(), f);
}

}  // namespace

void begin() {
  loadBest();
  g_phase = Phase::Menu;
}

void onGestures(const gestures::Events& g, const button::Events& b) {
  const bool tap = g.tap || b.shortPress;
  switch (g_phase) {
    case Phase::Menu:
      if (g.twistSteps) {
        g_twist += g.twistSteps;
        if (abs(g_twist) >= 30) {
          g_twist = 0;
          g_game = g_game == Game::Tilt ? Game::Memory : Game::Tilt;
          haptics::play(haptics::Pattern::Detent);
        }
      }
      if (tap) {
        haptics::play(haptics::Pattern::Confirm);
        if (g_game == Game::Tilt) startTilt();
        else startMemory();
      }
      break;
    case Phase::Over:
      if (tap) g_phase = Phase::Menu;
      break;
    default:
      if (g.shake) g_phase = Phase::Menu;  // Quit the game.
      break;
  }
}

bool tick() {
  const uint32_t now = millis();
  switch (g_phase) {
    case Phase::Play: stepTilt(now); break;
    case Phase::Show:
    case Phase::Input: stepMemory(now); break;
    default: break;
  }
  if (now - g_lastFrameMs >= 50) {
    g_lastFrameMs = now;
    if (display::backlightLevel() > 0) draw();
  }
  return false;
}

}  // namespace games_mode
