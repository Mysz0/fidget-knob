#include <unity.h>

#include "ams_codec.h"

void setUp() {}
void tearDown() {}

void test_parses_track_title() {
  const uint8_t raw[] = {2, 2, 0, 'H', 'i'};
  ams::Update u;
  TEST_ASSERT_TRUE(ams::parseUpdate(raw, sizeof(raw), &u));
  TEST_ASSERT_EQUAL(ams::Entity::Track, u.entity);
  TEST_ASSERT_EQUAL(ams::kTrackTitle, u.attribute);
  TEST_ASSERT_FALSE(u.truncated);
  TEST_ASSERT_EQUAL_STRING("Hi", u.value.c_str());
}

void test_truncated_flag_is_reported() {
  const uint8_t raw[] = {2, 2, 1, 'L', 'o', 'n'};
  ams::Update u;
  TEST_ASSERT_TRUE(ams::parseUpdate(raw, sizeof(raw), &u));
  TEST_ASSERT_TRUE(u.truncated);
}

void test_empty_value_is_valid() {
  const uint8_t raw[] = {2, 0, 0};
  ams::Update u;
  TEST_ASSERT_TRUE(ams::parseUpdate(raw, sizeof(raw), &u));
  TEST_ASSERT_EQUAL_STRING("", u.value.c_str());
}

void test_rejects_short_and_unknown() {
  const uint8_t shortRaw[] = {2, 2};
  const uint8_t badEntity[] = {9, 0, 0, 'x'};
  ams::Update u;
  TEST_ASSERT_FALSE(ams::parseUpdate(shortRaw, sizeof(shortRaw), &u));
  TEST_ASSERT_FALSE(ams::parseUpdate(badEntity, sizeof(badEntity), &u));
  TEST_ASSERT_FALSE(ams::parseUpdate(nullptr, 0, &u));
}

void test_subscriptions() {
  const std::vector<uint8_t> player = {0, 1, 2};
  const std::vector<uint8_t> track = {2, 0, 1, 2};
  TEST_ASSERT_TRUE(ams::playerSubscription() == player);
  TEST_ASSERT_TRUE(ams::trackSubscription() == track);
}

void test_playback_info() {
  bool playing = false;
  TEST_ASSERT_TRUE(ams::parsePlaying("1,1.0,12.5", &playing));
  TEST_ASSERT_TRUE(playing);
  TEST_ASSERT_TRUE(ams::parsePlaying("0,0.0,12.5", &playing));
  TEST_ASSERT_FALSE(playing);
  TEST_ASSERT_FALSE(ams::parsePlaying("", &playing));
}

void test_volume() {
  float v = -1;
  TEST_ASSERT_TRUE(ams::parseVolume("0.6875", &v));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6875f, v);
  TEST_ASSERT_TRUE(ams::parseVolume("1.5", &v));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, v);  // Clamped.
  TEST_ASSERT_FALSE(ams::parseVolume("abc", &v));
}

void test_command_bytes() {
  TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)ams::Command::TogglePlayPause);
  TEST_ASSERT_EQUAL_UINT8(3, (uint8_t)ams::Command::NextTrack);
  TEST_ASSERT_EQUAL_UINT8(5, (uint8_t)ams::Command::VolumeUp);
  TEST_ASSERT_EQUAL_UINT8(6, (uint8_t)ams::Command::VolumeDown);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_track_title);
  RUN_TEST(test_truncated_flag_is_reported);
  RUN_TEST(test_empty_value_is_valid);
  RUN_TEST(test_rejects_short_and_unknown);
  RUN_TEST(test_subscriptions);
  RUN_TEST(test_playback_info);
  RUN_TEST(test_volume);
  RUN_TEST(test_command_bytes);
  return UNITY_END();
}
