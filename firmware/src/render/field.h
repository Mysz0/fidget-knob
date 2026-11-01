#pragma once

#include <stdint.h>

#include "knob_protocol.h"

// The sprite field.
//
// A grid of 9x9 pixel-art sprites (render/sprites.h) filling a diamond. The
// diamond's size is the value: a dim light is a few marks at the centre, a
// bright one fills the screen. Heavy marks pack the middle and light ones fray
// the edge, so the shape reads as one glowing thing that dissolves outward.
//
// Two colours: the light's own colour, and a fixed green accent scattered
// through it - denser toward the edge. In colour mode the accents take the hue
// of their angle instead, so a spectrum rings the field while the hue is being
// turned.
//
// Every cell's mark and colour come from a fixed hash of its position and its
// distance from the centre, never from time. The picture only changes when the
// value does - nothing twinkles on its own, which is what read as flicker.
//
// Motion is eased here rather than in the app: draw() takes where the picture
// should end up and glides toward it, and pixels are only pushed while
// something is actually moving.
//
// Rendering is done in horizontal bands pushed by DMA, never as a whole frame.
// A 240x240x16bpp framebuffer is 115 KB against ~400 KB of SRAM, most of which
// the WiFi and TLS stacks want. Two 240x24 band buffers cost ~23 KB total.

namespace field {

struct State {
  float value;       // 0..1. Drives the diamond's size.
  float hue;         // 0..360 degrees.
  float saturation;  // 0..1. Zero renders as warm white, matching a bulb at
                     // minimum saturation rather than a dead grey.
  bool on;           // Off shows a single dim mark, distinct from on-and-dim.
  bool colourMode;   // True when twisting adjusts hue instead of brightness.
};

void begin();

// Eases toward `s` and redraws if the picture changed. Call once per frame.
// Blocking while it draws (~25 ms at 40 MHz SPI), free when nothing moved.
void draw(const State& s);

// The target picker: the target's name over a dimmed field, with one dot per
// target underneath. Composited into the same bands as the field, so the text
// is never erased and redrawn.
void drawPicker(const char* name, int index, int count, const State& s);

// Music mode. The diamond breathes with playback (a resting cluster when
// paused); a ring of accent sprites shows the volume for a moment after it
// changes; title and artist sit over a dimmed field after a track change.
struct MusicScreen {
  bool connected;
  bool playing;
  bool showVolume;
  float volume;        // 0..1
  bool showTitle;
  const char* title;   // ASCII, may be empty.
  const char* artist;  // ASCII, may be empty.
  float spinDegrees;   // Mosaic cover rotation (0 = upright).
};

void drawMusic(const MusicScreen& m);

// A big line (a time, a dice result) and an optional small one under it, over
// a dimmed field, with an optional progress ring at the rim. Used by the
// Timer, Clock, Dice and Photos modes. Labels are rebuilt only when their text
// changes, so a ticking clock costs one render a second.
struct Face {
  State field;
  float dim;          // Field brightness under the text, 0..1.
  const char* big;
  const char* small;  // May be empty.
  float ring;         // 0..1 lit clockwise from 12 o'clock; < 0 hides it.
  uint8_t ringR, ringG, ringB;
  // Optional: lines of small text ('\n' between) shown instead of big/small.
  const char* lines;
};
void drawFace(const Face& f);

// A game board: pieces drawn as sprites at lattice spots (i, j from -11 to
// 11, 0 at the centre), the field carrying on, dimmed, around them, and the
// face's text over it all. Stays on until another kind of draw.
enum class BoardLook : uint8_t { Player, Goal, Hazard, Dim, Lit };
struct BoardCell {
  int8_t i, j;
  BoardLook look;
};
void drawBoard(const BoardCell* cells, int count, const Face& f);

// The current album cover as a 23x23 grid of native RGB565 colours (one per
// sprite cell, row-major from the top-left), or nullptr to go back to the
// plain field. Copied.
void setMosaic(const uint16_t* grid);

// Forget what is on the panel, so the next draw pushes a full frame. Needed
// after something else - a photo cover - has drawn over it.
void invalidate();

// The look chosen in the app: sprite pack, palette (the accent and the field's
// "white"), and effect (0 still, 1 twinkle, 2 drift). Indices out of range
// fall back to the first.
void setLook(const knob::KnobSettings& s);
void loadPack(int pack);

// Frees the text labels. Called on a mode switch: their memory is better
// spent on a TLS handshake than on text the new mode will not show.
void clearLabels();

// The track title as the app rendered it: an alpha mask, width x height, one
// byte of coverage per pixel. Shown instead of the built-in font while the
// title is on screen. (0, 0, nullptr) clears it.
void setTitleMask(int width, int height, const uint8_t* alpha);

}  // namespace field
