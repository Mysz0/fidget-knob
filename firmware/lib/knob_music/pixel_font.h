#pragma once

#include <stdint.h>

#include <string>
#include <vector>

// A blocky 5-row font for the knob's big text. Each lit dot becomes one
// sprite on the field's lattice (or, for small text, a little square), so
// text is as crisp as the sprites themselves - no smoothing, no blur.
//
// Digits, A-Z (lower case is drawn as upper case), and : / - ? . ! + space.
// Most glyphs are 3 dots wide; M and W are 5, N 4, the colon and full stop 1.
namespace pixel_font {

constexpr int kRows = 5;

struct Dot {
  int8_t col, row;  // From the text's top-left.
};

// Dots of `text`, one column of space between characters; unknown characters
// are skipped. `width` gets the text's width in dots.
std::vector<Dot> layout(const char* text, int* width);

// Word-wraps `text` into at most `maxLines` lines of `maxChars` characters,
// breaking long words; "..." ends the last line when text was left over.
std::vector<std::string> wrap(const std::string& text, size_t maxChars, size_t maxLines);

}  // namespace pixel_font
