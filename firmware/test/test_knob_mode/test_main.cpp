#include <unity.h>

#include "knob_mode.h"

using knob::Mode;

void setUp() {}
void tearDown() {}

void test_stored_values() {
  TEST_ASSERT_EQUAL(Mode::Lights, knob::modeFromStored(0));
  TEST_ASSERT_EQUAL(Mode::Music, knob::modeFromStored(1));
  TEST_ASSERT_EQUAL(Mode::Timer, knob::modeFromStored(2));
  TEST_ASSERT_EQUAL(Mode::Clock, knob::modeFromStored(3));
  TEST_ASSERT_EQUAL(Mode::Dice, knob::modeFromStored(4));
  TEST_ASSERT_EQUAL(Mode::Photo, knob::modeFromStored(6));
  TEST_ASSERT_EQUAL(Mode::Games, knob::modeFromStored(8));
  TEST_ASSERT_EQUAL(Mode::Lights, knob::modeFromStored(9));   // Garbage.
  TEST_ASSERT_EQUAL(Mode::Lights, knob::modeFromStored(-1));  // Never written.
}

void test_serial_commands() {
  Mode m = Mode::Lights;
  TEST_ASSERT_TRUE(knob::parseModeCommand("mode music", &m));
  TEST_ASSERT_EQUAL(Mode::Music, m);
  TEST_ASSERT_TRUE(knob::parseModeCommand("  MODE Lights\r", &m));
  TEST_ASSERT_EQUAL(Mode::Lights, m);
  TEST_ASSERT_TRUE(knob::parseModeCommand("mode timer", &m));
  TEST_ASSERT_EQUAL(Mode::Timer, m);
  TEST_ASSERT_TRUE(knob::parseModeCommand("mode dice", &m));
  TEST_ASSERT_EQUAL(Mode::Dice, m);
  TEST_ASSERT_FALSE(knob::parseModeCommand("mode disco", &m));
  TEST_ASSERT_FALSE(knob::parseModeCommand("music", &m));
  TEST_ASSERT_FALSE(knob::parseModeCommand("", &m));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("lights", knob::modeName(Mode::Lights));
  TEST_ASSERT_EQUAL_STRING("music", knob::modeName(Mode::Music));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stored_values);
  RUN_TEST(test_serial_commands);
  RUN_TEST(test_names);
  return UNITY_END();
}
