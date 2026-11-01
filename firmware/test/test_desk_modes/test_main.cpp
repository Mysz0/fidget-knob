#include <unity.h>

#include "dice.h"
#include "knob_protocol.h"
#include "countdown.h"
#include "pixel_font.h"

using timer::State;

void setUp() {}
void tearDown() {}

void test_idle_shows_length() {
  timer::Countdown t;
  t.setLength(300);
  TEST_ASSERT_EQUAL(State::Idle, t.state());
  TEST_ASSERT_EQUAL_UINT32(300, t.remaining(1000));
}

void test_run_pause_resume_done() {
  timer::Countdown t;
  t.setLength(120);
  t.toggle(1000);
  TEST_ASSERT_EQUAL(State::Running, t.state());
  TEST_ASSERT_EQUAL_UINT32(100, t.remaining(1020));
  t.toggle(1020);  // Pause.
  TEST_ASSERT_EQUAL_UINT32(100, t.remaining(5000));
  t.setLength(999);  // Ignored while paused.
  TEST_ASSERT_EQUAL_UINT32(120, t.length());
  t.toggle(5000);
  TEST_ASSERT_FALSE(t.tick(5099));
  TEST_ASSERT_TRUE(t.tick(5100));
  TEST_ASSERT_FALSE(t.tick(5200));  // Only once.
  TEST_ASSERT_EQUAL(State::Done, t.state());
  TEST_ASSERT_EQUAL_UINT32(0, t.remaining(5200));
  t.toggle(6000);  // Done -> runs again from the full length.
  TEST_ASSERT_EQUAL_UINT32(120, t.remaining(6000));
}

void test_clamp_and_steps() {
  timer::Countdown t;
  t.setLength(1);
  TEST_ASSERT_EQUAL_UINT32(timer::kMinSeconds, t.length());
  t.setLength(999999);
  TEST_ASSERT_EQUAL_UINT32(timer::kMaxSeconds, t.length());
  TEST_ASSERT_EQUAL_UINT32(15, timer::stepFor(60, 1));
  TEST_ASSERT_EQUAL_UINT32(15, timer::stepFor(300, -1));  // 5:00 -> 4:45
  TEST_ASSERT_EQUAL_UINT32(60, timer::stepFor(300, 1));   // 5:00 -> 6:00
  TEST_ASSERT_EQUAL_UINT32(300, timer::stepFor(1800, 1));
  TEST_ASSERT_EQUAL_UINT32(600, timer::stepFor(3600, 1));
}

void test_shift_and_restore() {
  timer::Countdown t;
  t.setLength(600);
  t.toggle(1000);
  t.shift(1000000);
  TEST_ASSERT_EQUAL_UINT32(600, t.remaining(1001000));
  timer::Countdown u;
  u.restore(t.save());
  TEST_ASSERT_EQUAL(State::Running, u.state());
  TEST_ASSERT_EQUAL_UINT32(600, u.remaining(1001000));
  timer::Countdown::Saved junk{9, {0, 0, 0}, 0, 0, 0};
  u.restore(junk);
  TEST_ASSERT_EQUAL(State::Idle, u.state());
}

void test_dice() {
  TEST_ASSERT_EQUAL(dice::Kind::D6, dice::kindFromByte(99));
  TEST_ASSERT_EQUAL(dice::Kind::D100, dice::nextKind(dice::Kind::D6, -1));
  TEST_ASSERT_EQUAL(dice::Kind::D20, dice::nextKind(dice::Kind::D6, 1));
  TEST_ASSERT_EQUAL_STRING("HEADS", dice::resultText(dice::Kind::Coin, 1).c_str());
  TEST_ASSERT_EQUAL_STRING("NO", dice::resultText(dice::Kind::YesNo, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("17", dice::resultText(dice::Kind::D20, 17).c_str());
  TEST_ASSERT_EQUAL_STRING("?", dice::resultText(dice::Kind::D6, 7).c_str());
  TEST_ASSERT_EQUAL_STRING("Y/N", dice::kindShort(dice::Kind::YesNo));
}

void test_protocol() {
  knob::Command c;
  const uint8_t len[] = {3, 1, 0x2C, 0x01};
  TEST_ASSERT_TRUE(knob::parseCommand(len, sizeof len, &c));
  TEST_ASSERT_EQUAL(knob::CommandType::Timer, c.type);
  TEST_ASSERT_EQUAL_UINT16(300, c.args[0] | (c.args[1] << 8));
  const uint8_t shortLen[] = {3, 1, 5};
  TEST_ASSERT_FALSE(knob::parseCommand(shortLen, sizeof shortLen, &c));
  const uint8_t roll[] = {4, 2};
  TEST_ASSERT_TRUE(knob::parseCommand(roll, sizeof roll, &c));
  TEST_ASSERT_EQUAL(knob::CommandType::Dice, c.type);
  const uint8_t bad[] = {4, 9};
  TEST_ASSERT_FALSE(knob::parseCommand(bad, sizeof bad, &c));

  knob::TimerState s;
  s.state = 1;
  s.left = 0x0102;
  s.length = 300;
  auto b = knob::encodeTimerState(s);
  TEST_ASSERT_EQUAL(6, b.size());
  TEST_ASSERT_EQUAL_UINT8(2, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x02, b[2]);
  TEST_ASSERT_EQUAL_UINT8(0x2C, b[4]);
  knob::DiceState d;
  d.kind = 1;
  d.result = 300;
  auto e = knob::encodeDiceState(d);
  TEST_ASSERT_EQUAL(5, e.size());
  TEST_ASSERT_EQUAL_UINT8(300 & 0xFF, e[2]);
}

void test_photo_protocol() {
  knob::Command c;
  const uint8_t del[] = {5, 1, 3};
  TEST_ASSERT_TRUE(knob::parseCommand(del, sizeof del, &c));
  TEST_ASSERT_EQUAL(knob::CommandType::Photo, c.type);
  TEST_ASSERT_EQUAL_UINT8(3, c.args[0]);
  const uint8_t badSlot[] = {5, 2, 5};
  TEST_ASSERT_FALSE(knob::parseCommand(badSlot, sizeof badSlot, &c));
  const uint8_t off[] = {6};
  TEST_ASSERT_TRUE(knob::parseCommand(off, sizeof off, &c));
  TEST_ASSERT_EQUAL(knob::CommandType::PowerOff, c.type);
  const uint8_t toggle[] = {5, 3};
  TEST_ASSERT_TRUE(knob::parseCommand(toggle, sizeof toggle, &c));
  knob::PhotoState s;
  s.present = 0b10101;
  s.current = 2;
  s.mosaic = true;
  auto b = knob::encodePhotoState(s);
  TEST_ASSERT_EQUAL(5, b.size());
  TEST_ASSERT_EQUAL_UINT8(6, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0b10101, b[1]);
  // The assembler takes the photo kinds and still refuses unknown ones.
  knob::ArtworkAssembler a;
  const uint8_t photoBegin[] = {1, 5, 10, 0};
  TEST_ASSERT_EQUAL((int)knob::ArtworkAssembler::Result::Pending, (int)a.onFrame(photoBegin, 4));
  const uint8_t unknown[] = {1, 7, 10, 0};
  TEST_ASSERT_EQUAL((int)knob::ArtworkAssembler::Result::Rejected, (int)a.onFrame(unknown, 4));
}

void test_pixel_font() {
  int w = 0;
  auto dots = pixel_font::layout("1", &w);
  TEST_ASSERT_EQUAL(3, w);
  TEST_ASSERT_EQUAL(8, (int)dots.size());
  pixel_font::layout("25:00", &w);
  TEST_ASSERT_EQUAL(3 + 1 + 3 + 1 + 1 + 1 + 3 + 1 + 3, w);  // 17
  pixel_font::layout("heads", &w);  // Lower case drawn as upper.
  TEST_ASSERT_EQUAL(19, w);
  pixel_font::layout("M", &w);
  TEST_ASSERT_EQUAL(5, w);
  pixel_font::layout("", &w);
  TEST_ASSERT_EQUAL(0, w);
  auto none = pixel_font::layout("~", &w);  // Unknown: skipped.
  TEST_ASSERT_EQUAL(0, (int)none.size());
}

void test_wrap() {
  auto l = pixel_font::wrap("see you at the station at nine", 12, 3);
  TEST_ASSERT_EQUAL(3, (int)l.size());
  TEST_ASSERT_EQUAL_STRING("see you at", l[0].c_str());
  TEST_ASSERT_EQUAL_STRING("the station", l[1].c_str());
  TEST_ASSERT_EQUAL_STRING("at nine", l[2].c_str());
  auto cut = pixel_font::wrap("one two three four five six seven", 9, 2);
  TEST_ASSERT_EQUAL(2, (int)cut.size());
  TEST_ASSERT_EQUAL_STRING("one two", cut[0].c_str());
  TEST_ASSERT_EQUAL_STRING("three...", cut[1].c_str());
  auto longWord = pixel_font::wrap("supercalifragilistic", 8, 3);
  TEST_ASSERT_EQUAL_STRING("supercal", longWord[0].c_str());
  TEST_ASSERT_EQUAL(0, (int)pixel_font::wrap("", 8, 3).size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_shows_length);
  RUN_TEST(test_run_pause_resume_done);
  RUN_TEST(test_clamp_and_steps);
  RUN_TEST(test_shift_and_restore);
  RUN_TEST(test_dice);
  RUN_TEST(test_protocol);
  RUN_TEST(test_photo_protocol);
  RUN_TEST(test_pixel_font);
  RUN_TEST(test_wrap);
  return UNITY_END();
}
