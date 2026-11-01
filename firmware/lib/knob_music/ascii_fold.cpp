#include "ascii_fold.h"

#include <stdint.h>

namespace knob {
namespace {

// Folds a code point to ASCII. Returns nullptr to drop it.
const char* fold(uint32_t cp) {
  if (cp < 0x80) {
    static char one[2];
    one[0] = (char)cp;
    one[1] = '\0';
    return one;
  }
  switch (cp) {
    // Polish.
    case 0x104: return "A"; case 0x105: return "a";
    case 0x106: return "C"; case 0x107: return "c";
    case 0x118: return "E"; case 0x119: return "e";
    case 0x141: return "L"; case 0x142: return "l";
    case 0x143: return "N"; case 0x144: return "n";
    case 0x15A: return "S"; case 0x15B: return "s";
    case 0x179: return "Z"; case 0x17A: return "z";
    case 0x17B: return "Z"; case 0x17C: return "z";
    // Latin-1.
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
    case 0xC7: return "C"; case 0xE7: return "c";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
    case 0xD1: return "N"; case 0xF1: return "n";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: return "O";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
    case 0xDD: return "Y"; case 0xFD: case 0xFF: return "y";
    case 0xDF: return "ss";
    // Typographic punctuation that titles use constantly.
    case 0x2018: case 0x2019: return "'";
    case 0x201C: case 0x201D: return "\"";
    case 0x2013: case 0x2014: return "-";
    case 0x2026: return "...";
    case 0xA0: return " ";
    default: return nullptr;
  }
}

}  // namespace

std::string foldToAscii(const std::string& utf8, size_t maxChars) {
  std::string out;
  const auto* p = reinterpret_cast<const uint8_t*>(utf8.data());
  const auto* end = p + utf8.size();

  while (p < end) {
    uint32_t cp;
    int extra;
    if (*p < 0x80) { cp = *p; extra = 0; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
    else { ++p; continue; }  // Stray continuation byte.

    if (end - p <= extra) break;  // Sequence cut off at the end.
    bool valid = true;
    for (int i = 1; i <= extra; ++i) {
      if ((p[i] & 0xC0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    p += valid ? extra + 1 : 1;
    if (!valid) continue;

    const char* folded = fold(cp);
    if (!folded) continue;
    for (const char* c = folded; *c; ++c) {
      const char ch = (*c == '\t' || *c == '\n' || *c == '\r') ? ' ' : *c;
      if ((unsigned char)ch < 0x20) continue;
      if (ch == ' ' && (out.empty() || out.back() == ' ')) continue;
      out += ch;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();

  if (out.size() > maxChars && maxChars >= 4) {
    out.resize(maxChars - 3);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    out += "...";
  }
  return out;
}

}  // namespace knob
