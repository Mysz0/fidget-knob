#include "pixel_font.h"

#include <ctype.h>
#include <string.h>

namespace pixel_font {
namespace {

struct Glyph {
  char c;
  const char* rows[kRows];
};

// clang-format off
const Glyph kGlyphs[] = {
    {'0', {"###", "#.#", "#.#", "#.#", "###"}},
    {'1', {".#.", "##.", ".#.", ".#.", "###"}},
    {'2', {"###", "..#", "###", "#..", "###"}},
    {'3', {"###", "..#", ".##", "..#", "###"}},
    {'4', {"#.#", "#.#", "###", "..#", "..#"}},
    {'5', {"###", "#..", "###", "..#", "###"}},
    {'6', {"###", "#..", "###", "#.#", "###"}},
    {'7', {"###", "..#", ".#.", ".#.", ".#."}},
    {'8', {"###", "#.#", "###", "#.#", "###"}},
    {'9', {"###", "#.#", "###", "..#", "###"}},
    {'A', {".#.", "#.#", "###", "#.#", "#.#"}},
    {'B', {"##.", "#.#", "##.", "#.#", "##."}},
    {'C', {".##", "#..", "#..", "#..", ".##"}},
    {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
    {'E', {"###", "#..", "##.", "#..", "###"}},
    {'F', {"###", "#..", "##.", "#..", "#.."}},
    {'G', {".##", "#..", "#.#", "#.#", ".##"}},
    {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
    {'I', {"###", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..#", "..#", "..#", "#.#", ".#."}},
    {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}},
    {'L', {"#..", "#..", "#..", "#..", "###"}},
    {'M', {"#...#", "##.##", "#.#.#", "#...#", "#...#"}},
    {'N', {"#..#", "##.#", "#.##", "#..#", "#..#"}},
    {'O', {".#.", "#.#", "#.#", "#.#", ".#."}},
    {'P', {"##.", "#.#", "##.", "#..", "#.."}},
    {'Q', {".#.", "#.#", "#.#", "##.", ".##"}},
    {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
    {'S', {".##", "#..", ".#.", "..#", "##."}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
    {'U', {"#.#", "#.#", "#.#", "#.#", "###"}},
    {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
    {'W', {"#...#", "#...#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}},
    {'Z', {"###", "..#", ".#.", "#..", "###"}},
    {':', {".", "#", ".", "#", "."}},
    {'.', {".", ".", ".", ".", "#"}},
    {'!', {"#", "#", "#", ".", "#"}},
    {'/', {"..#", "..#", ".#.", "#..", "#.."}},
    {'-', {"...", "...", "###", "...", "..."}},
    {'+', {"...", ".#.", "###", ".#.", "..."}},
    {'?', {"##.", "..#", ".#.", "...", ".#."}},
    {' ', {"..", "..", "..", "..", ".."}},
    {',', {".", ".", ".", "#", "#"}},
    {'\'', {"#", "#", ".", ".", "."}},
    {'"', {"#.#", "#.#", "...", "...", "..."}},
    {'(', {".#", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", "#."}},
    {'&', {".#.", "#.#", ".#.", "#.#", ".##"}},
    {'@', {"###", "#.#", "#.#", "#..", "###"}},
    {'#', {"#.#", "###", "#.#", "###", "#.#"}},
    {'%', {"#.#", "..#", ".#.", "#..", "#.#"}},
    {'*', {"#.#", ".#.", "#.#", "...", "..."}},
    {'=', {"...", "###", "...", "###", "..."}},
    {'_', {"...", "...", "...", "...", "###"}},
    {';', {".", "#", ".", "#", "#"}},
    {'<', {"..#", ".#.", "#..", ".#.", "..#"}},
    {'>', {"#..", ".#.", "..#", ".#.", "#.."}},
};
// clang-format on

const Glyph* find(char c) {
  const char up = (char)toupper((unsigned char)c);
  for (const Glyph& g : kGlyphs) {
    if (g.c == up) return &g;
  }
  return nullptr;
}

}  // namespace

std::vector<Dot> layout(const char* text, int* width) {
  std::vector<Dot> dots;
  int x = 0;
  bool first = true;
  for (const char* p = text; p && *p; ++p) {
    const Glyph* g = find(*p);
    if (!g) continue;
    if (!first) ++x;  // One column between characters.
    first = false;
    const int w = (int)strlen(g->rows[0]);
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < w; ++c) {
        if (g->rows[r][c] == '#') dots.push_back({(int8_t)(x + c), (int8_t)r});
      }
    }
    x += w;
  }
  if (width) *width = x;
  return dots;
}

std::vector<std::string> wrap(const std::string& text, size_t maxChars, size_t maxLines) {
  std::vector<std::string> lines;
  if (maxChars == 0 || maxLines == 0) return lines;
  // Words, with whitespace (newlines included) as the separator.
  std::vector<std::string> words;
  std::string cur;
  for (char c : text) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      if (!cur.empty()) words.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) words.push_back(cur);

  std::string line;
  size_t w = 0;
  bool overflow = false;
  while (w < words.size()) {
    std::string word = words[w];
    if (word.size() > maxChars) {  // Too long for any line: split it.
      words[w] = word.substr(maxChars);
      word = word.substr(0, maxChars);
    } else {
      ++w;
    }
    const size_t need = line.empty() ? word.size() : line.size() + 1 + word.size();
    if (need <= maxChars) {
      line = line.empty() ? word : line + " " + word;
      continue;
    }
    lines.push_back(line);
    if (lines.size() == maxLines) {
      overflow = true;
      break;
    }
    line = word;
  }
  if (!overflow && !line.empty()) lines.push_back(line);
  if (overflow || lines.size() > maxLines) {
    lines.resize(maxLines);
    std::string& last = lines.back();
    if (last.size() + 3 > maxChars) last.resize(maxChars > 3 ? maxChars - 3 : 0);
    last += "...";
  }
  return lines;
}

}  // namespace pixel_font
