#pragma once

#include <stdint.h>

#include <string>

// Dice / decide: what can be rolled and how a result reads on the screen.
namespace dice {

enum class Kind : uint8_t { D6 = 0, D20 = 1, Coin = 2, YesNo = 3, D100 = 4 };
constexpr int kKinds = 5;

Kind kindFromByte(uint8_t raw);   // Unknown values become D6.
Kind nextKind(Kind k, int step);  // Wraps both ways.
int faces(Kind k);                // Number of outcomes.
const char* kindName(Kind k);     // "d6", "coin", "yes / no"...
const char* kindShort(Kind k);    // "D6", "COIN", "Y/N" - fits the big text.
// `result` is 1..faces(k). Numbers for dice, words for the rest.
std::string resultText(Kind k, int result);

}  // namespace dice
