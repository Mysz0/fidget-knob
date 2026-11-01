#include <unity.h>

#include "knob_protocol.h"

void setUp() {}
void tearDown() {}

void test_lamp_state_parse() {
  // on, level 40, hue 300 (LE), sat 70, id "abc"
  const uint8_t raw[] = {1, 40, 0x2C, 0x01, 70, 'a', 'b', 'c'};
  knob::LampState s;
  TEST_ASSERT_TRUE(knob::parseLampState(raw, sizeof(raw), &s));
  TEST_ASSERT_TRUE(s.on);
  TEST_ASSERT_EQUAL(40, s.level);
  TEST_ASSERT_EQUAL(300, s.hue);
  TEST_ASSERT_EQUAL(70, s.saturation);
  TEST_ASSERT_EQUAL_STRING("abc", s.lampId.c_str());
  const uint8_t shortRaw[] = {1, 40, 0};
  TEST_ASSERT_FALSE(knob::parseLampState(shortRaw, sizeof(shortRaw), &s));
}

void test_status_encoding() {
  knob::KnobStatus st;
  st.mode = 0;
  st.wifi = knob::WifiState::Connected;
  st.rssi = -61;
  st.hubConnected = true;
  st.ssid = "Home";
  const auto b = knob::encodeStatus(st);
  // [ver][mode][wifi][rssi][hub][len][ssid...]
  const uint8_t expect[] = {1, 0, 2, (uint8_t)-61, 1, 4, 'H', 'o', 'm', 'e'};
  TEST_ASSERT_EQUAL(sizeof(expect), b.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, b.data(), sizeof(expect));
}

void test_scan_list_encoding() {
  std::vector<knob::ScanEntry> nets = {{"Home", -50, true}, {"Cafe", -80, false}};
  const auto s = knob::encodeScanList(nets, 512);
  TEST_ASSERT_EQUAL_STRING("Home\x1F-50\x1F" "1\x1E" "Cafe\x1F-80\x1F" "0", s.c_str());
}

void test_scan_list_respects_budget_and_dedupes() {
  std::vector<knob::ScanEntry> nets = {{"Home", -50, true}, {"Home", -70, true},
                                       {"Long network name", -60, true}};
  // Budget fits only the first entry.
  const auto s = knob::encodeScanList(nets, 12);
  TEST_ASSERT_EQUAL_STRING("Home\x1F-50\x1F" "1", s.c_str());
}

void test_commands() {
  knob::Command c;
  const uint8_t scan[] = {1};
  TEST_ASSERT_TRUE(knob::parseCommand(scan, 1, &c));
  TEST_ASSERT_EQUAL((int)knob::CommandType::ScanWifi, (int)c.type);
  // set time: 1760000000 LE, offset +120 min LE
  const uint8_t time[] = {2, 0x00, 0x00, 0xE6, 0x68, 0x78, 0x00};
  TEST_ASSERT_TRUE(knob::parseCommand(time, sizeof(time), &c));
  TEST_ASSERT_EQUAL((int)knob::CommandType::SetTime, (int)c.type);
  TEST_ASSERT_EQUAL_UINT32(0x68E60000u, c.unixTime);
  TEST_ASSERT_EQUAL(120, c.utcOffsetMinutes);
  const uint8_t bad[] = {9};
  TEST_ASSERT_FALSE(knob::parseCommand(bad, 1, &c));
  const uint8_t shortTime[] = {2, 1, 2};
  TEST_ASSERT_FALSE(knob::parseCommand(shortTime, sizeof(shortTime), &c));
}

void test_settings_spin_cover_byte() {
  knob::KnobSettings s;
  TEST_ASSERT_FALSE(s.spinCover);
  s.spinCover = true;
  const auto b = knob::encodeSettings(s);
  TEST_ASSERT_EQUAL(28, b.size());  // Spin cover at [10], look, twist, screen.
  knob::KnobSettings back;
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &back));
  TEST_ASSERT_TRUE(back.spinCover);
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), 10, &back));
  TEST_ASSERT_FALSE(back.spinCover);  // Absent: off.
}

void test_home_config_lamp_cycle() {
  knob::HomeConfig c;
  c.lampCycle = "a,b";
  const auto b = knob::encodeHomeConfig(c);
  knob::HomeConfig back;
  TEST_ASSERT_TRUE(knob::parseHomeConfig(b.data(), b.size(), &back));
  TEST_ASSERT_EQUAL_STRING("a,b", back.lampCycle.c_str());
  TEST_ASSERT_TRUE(back.has(knob::HomeConfig::kLampCycle));
  TEST_ASSERT_TRUE(knob::lampInCycle("a,b", "b"));
  TEST_ASSERT_FALSE(knob::lampInCycle("a,b", "c"));
  TEST_ASSERT_FALSE(knob::lampInCycle("ab,c", "a"));
  TEST_ASSERT_TRUE(knob::lampInCycle("", "anything"));  // Empty = all lamps.
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lamp_state_parse);
  RUN_TEST(test_status_encoding);
  RUN_TEST(test_scan_list_encoding);
  RUN_TEST(test_scan_list_respects_budget_and_dedupes);
  RUN_TEST(test_commands);
  RUN_TEST(test_settings_spin_cover_byte);
  RUN_TEST(test_home_config_lamp_cycle);
  return UNITY_END();
}
