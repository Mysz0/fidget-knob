#pragma once

#include <stddef.h>

#include <string>

namespace knob {

// Track titles arrive as UTF-8 from any app, but the screen's fonts only have
// ASCII. Latin letters with accents fold to their base letter (so Polish and
// German titles stay readable); anything else - emoji, CJK - is dropped rather
// than drawn as garbage. Runs of whitespace collapse to one space, and text
// longer than maxChars is cut and ends in "...".
std::string foldToAscii(const std::string& utf8, size_t maxChars);

}  // namespace knob
