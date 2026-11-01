#include "dice.h"

namespace dice {

Kind kindFromByte(uint8_t raw) {
  return raw < kKinds ? (Kind)raw : Kind::D6;
}

Kind nextKind(Kind k, int step) {
  int i = ((int)k + step) % kKinds;
  if (i < 0) i += kKinds;
  return (Kind)i;
}

int faces(Kind k) {
  switch (k) {
    case Kind::D20: return 20;
    case Kind::Coin: return 2;
    case Kind::YesNo: return 2;
    case Kind::D100: return 100;
    case Kind::D6:
    default: return 6;
  }
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::D20: return "d20";
    case Kind::Coin: return "coin";
    case Kind::YesNo: return "yes / no";
    case Kind::D100: return "d100";
    case Kind::D6:
    default: return "d6";
  }
}

const char* kindShort(Kind k) {
  switch (k) {
    case Kind::D20: return "D20";
    case Kind::Coin: return "COIN";
    case Kind::YesNo: return "Y/N";
    case Kind::D100: return "D100";
    case Kind::D6:
    default: return "D6";
  }
}

std::string resultText(Kind k, int result) {
  if (result < 1 || result > faces(k)) return "?";
  if (k == Kind::Coin) return result == 1 ? "HEADS" : "TAILS";
  if (k == Kind::YesNo) return result == 1 ? "YES" : "NO";
  return std::to_string(result);
}

}  // namespace dice
