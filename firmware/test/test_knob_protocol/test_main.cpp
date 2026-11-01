#include <unity.h>

#include "knob_protocol.h"

using knob::ArtworkAssembler;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> begin(uint8_t kind, uint16_t len) {
  return {0x01, kind, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
}
static std::vector<uint8_t> data(uint16_t off, std::vector<uint8_t> bytes) {
  std::vector<uint8_t> f = {0x02, (uint8_t)(off & 0xFF), (uint8_t)(off >> 8)};
  f.insert(f.end(), bytes.begin(), bytes.end());
  return f;
}
static std::vector<uint8_t> end(uint32_t crc) {
  return {0x03, (uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16),
          (uint8_t)(crc >> 24)};
}
static ArtworkAssembler::Result feed(ArtworkAssembler& a,
                                     const std::vector<uint8_t>& f) {
  return a.onFrame(f.data(), f.size());
}

void test_crc32_known_value() {
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, knob::crc32(s, sizeof(s)));
}

void test_now_playing_encoding() {
  TEST_ASSERT_EQUAL_STRING("A\x1F" "B\x1F" "C\x1F" "1",
                           knob::encodeNowPlaying("A", "B", "C", true).c_str());
  TEST_ASSERT_EQUAL_STRING("A\x1F" "B\x1F" "C\x1F" "0",
                           knob::encodeNowPlaying("A", "B", "C", false).c_str());
}

void test_settings_defaults_round_trip() {
  const knob::KnobSettings d;
  const std::vector<uint8_t> bytes = knob::encodeSettings(d);
  knob::KnobSettings back;
  back.screenOffSeconds = 99;
  TEST_ASSERT_TRUE(knob::parseSettings(bytes.data(), bytes.size(), &back));
  TEST_ASSERT_EQUAL(12, back.screenOffSeconds);
  TEST_ASSERT_EQUAL(2, back.sleepMinutes);
  TEST_ASSERT_EQUAL((int)knob::Action::PlayPause, (int)back.musicTap);
  TEST_ASSERT_EQUAL((int)knob::Action::Next, (int)back.musicShake);
  TEST_ASSERT_TRUE(back.overlayOn);
  TEST_ASSERT_EQUAL(3, back.overlaySeconds);
  TEST_ASSERT_TRUE(back.ratchet);
  // A nine-byte blob from before the ratchet keeps it on.
  knob::KnobSettings old;
  TEST_ASSERT_TRUE(knob::parseSettings(bytes.data(), 9, &old));
  TEST_ASSERT_TRUE(old.ratchet);
  const uint8_t off[] = {1, 12, 2, 1, 2, 1, 2, 1, 3, 0};
  TEST_ASSERT_TRUE(knob::parseSettings(off, sizeof(off), &old));
  TEST_ASSERT_FALSE(old.ratchet);
}

void test_settings_look() {
  knob::KnobSettings d;
  d.spritePack = 3;
  d.palette = 5;
  d.effect = 2;
  const std::vector<uint8_t> bytes = knob::encodeSettings(d);
  TEST_ASSERT_EQUAL(28, bytes.size());
  knob::KnobSettings back;
  TEST_ASSERT_TRUE(knob::parseSettings(bytes.data(), bytes.size(), &back));
  TEST_ASSERT_EQUAL(3, back.spritePack);
  TEST_ASSERT_EQUAL(5, back.palette);
  TEST_ASSERT_EQUAL(2, back.effect);
  // An older app's eleven bytes: the original look.
  knob::KnobSettings old;
  old.effect = 2;
  TEST_ASSERT_TRUE(knob::parseSettings(bytes.data(), 11, &old));
  TEST_ASSERT_EQUAL(0, old.spritePack);
  TEST_ASSERT_EQUAL(0, old.effect);
  // An unknown effect is refused back to still.
  std::vector<uint8_t> bad = bytes;
  bad[13] = 9;
  TEST_ASSERT_TRUE(knob::parseSettings(bad.data(), bad.size(), &old));
  TEST_ASSERT_EQUAL(0, old.effect);
}

void test_settings_screen_and_custom_colours() {
  knob::KnobSettings d;
  d.palette = knob::kCustomPalette;
  d.customWhite[0] = 10;
  d.customAccent[2] = 200;
  d.screenOffSeconds = 0;  // Never off.
  d.dimAfterSeconds = 30;
  d.dimPercent = 40;
  d.brightness = 70;
  auto b = knob::encodeSettings(d);
  knob::KnobSettings back;
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &back));
  TEST_ASSERT_EQUAL(knob::kCustomPalette, back.palette);
  TEST_ASSERT_EQUAL(10, back.customWhite[0]);
  TEST_ASSERT_EQUAL(200, back.customAccent[2]);
  TEST_ASSERT_EQUAL(0, back.screenOffSeconds);
  TEST_ASSERT_EQUAL(30, back.dimAfterSeconds);
  TEST_ASSERT_EQUAL(40, back.dimPercent);
  TEST_ASSERT_EQUAL(70, back.brightness);
  // An older app (18 bytes): dims at half the off time, full brightness.
  knob::KnobSettings old;
  b[1] = 20;
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), 18, &old));
  TEST_ASSERT_EQUAL(10, old.dimAfterSeconds);
  TEST_ASSERT_EQUAL(100, old.brightness);
  TEST_ASSERT_FALSE(back.mediaRemote);
  d.mediaRemote = true;
  auto r = knob::encodeSettings(d);
  TEST_ASSERT_TRUE(knob::parseSettings(r.data(), r.size(), &back));
  TEST_ASSERT_TRUE(back.mediaRemote);
  b[26] = 0;  // Too dark to see: clamped.
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &old));
  TEST_ASSERT_EQUAL(10, old.brightness);
}

void test_settings_twist() {
  knob::KnobSettings d;
  TEST_ASSERT_EQUAL((int)knob::MusicTwist::Volume, (int)d.musicTwist);
  d.musicTwist = knob::MusicTwist::Tracks;
  d.lightsTwist = knob::LightsTwist::Lamps;
  d.twistSensitivity = 2;
  d.twistReverse = true;
  auto b = knob::encodeSettings(d);
  knob::KnobSettings back;
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &back));
  TEST_ASSERT_EQUAL((int)knob::MusicTwist::Tracks, (int)back.musicTwist);
  TEST_ASSERT_EQUAL((int)knob::LightsTwist::Lamps, (int)back.lightsTwist);
  TEST_ASSERT_EQUAL(2, back.twistSensitivity);
  TEST_ASSERT_TRUE(back.twistReverse);
  // Older apps (14 bytes) keep the original twist.
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), 14, &back));
  TEST_ASSERT_EQUAL((int)knob::MusicTwist::Volume, (int)back.musicTwist);
  TEST_ASSERT_EQUAL((int)knob::LightsTwist::Brightness, (int)back.lightsTwist);
  TEST_ASSERT_EQUAL(1, back.twistSensitivity);
  TEST_ASSERT_FALSE(back.twistReverse);
  b[15] = 2;  // The dropped "always colour": brightness/colour again.
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &back));
  TEST_ASSERT_EQUAL((int)knob::LightsTwist::Brightness, (int)back.lightsTwist);
  b[14] = 7;  // Unknown: default.
  TEST_ASSERT_TRUE(knob::parseSettings(b.data(), b.size(), &back));
  TEST_ASSERT_EQUAL((int)knob::MusicTwist::Volume, (int)back.musicTwist);
}

void test_settings_are_clamped() {
  // version, screen 1 s, sleep 200 min, actions 9 (unknown), overlay 1, 99 s
  const uint8_t raw[] = {1, 1, 200, 9, 9, 9, 9, 1, 99};
  knob::KnobSettings s;
  TEST_ASSERT_TRUE(knob::parseSettings(raw, sizeof(raw), &s));
  TEST_ASSERT_EQUAL(5, s.screenOffSeconds);
  TEST_ASSERT_EQUAL(60, s.sleepMinutes);
  TEST_ASSERT_EQUAL((int)knob::Action::None, (int)s.musicTap);
  TEST_ASSERT_EQUAL(10, s.overlaySeconds);
}

void test_settings_reject_garbage() {
  knob::KnobSettings s;
  const uint8_t wrongVersion[] = {7, 12, 2, 1, 2, 1, 2, 1, 3};
  const uint8_t tooShort[] = {1, 12, 2};
  TEST_ASSERT_FALSE(knob::parseSettings(wrongVersion, sizeof(wrongVersion), &s));
  TEST_ASSERT_FALSE(knob::parseSettings(tooShort, sizeof(tooShort), &s));
  TEST_ASSERT_FALSE(knob::parseSettings(nullptr, 0, &s));
}

void test_title_overlay_kind_accepted() {
  ArtworkAssembler a;
  const std::vector<uint8_t> payload = {2, 1, 255, 128};  // 2x1 mask.
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Pending,
                    feed(a, begin(knob::kArtworkTitle, 4)));
  feed(a, data(0, payload));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Complete,
                    feed(a, end(knob::crc32(payload.data(), payload.size()))));
}

void test_complete_transfer() {
  ArtworkAssembler a;
  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Pending, feed(a, begin(2, 5)));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Pending, feed(a, data(0, {1, 2, 3})));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Pending, feed(a, data(3, {4, 5})));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Complete,
                    feed(a, end(knob::crc32(payload.data(), payload.size()))));
  TEST_ASSERT_EQUAL(2, a.kind());
  TEST_ASSERT_EQUAL(5, (int)a.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), a.data(), 5);
}

void test_bad_crc_is_rejected() {
  ArtworkAssembler a;
  feed(a, begin(1, 2));
  feed(a, data(0, {9, 9}));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, feed(a, end(0x12345678)));
}

void test_missing_bytes_are_rejected() {
  ArtworkAssembler a;
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  feed(a, begin(2, 4));
  feed(a, data(0, {1, 2}));  // Second half never arrives.
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected,
                    feed(a, end(knob::crc32(payload.data(), payload.size()))));
}

void test_out_of_bounds_data_rejected() {
  ArtworkAssembler a;
  feed(a, begin(2, 4));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, feed(a, data(3, {1, 2})));
}

void test_oversized_begin_rejected() {
  ArtworkAssembler a;
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected,
                    feed(a, begin(2, ArtworkAssembler::kMaxBytes + 1)));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, feed(a, begin(7, 10)));
}

void test_data_without_begin_rejected() {
  ArtworkAssembler a;
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, feed(a, data(0, {1})));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, feed(a, end(0)));
}

void test_new_begin_discards_partial() {
  ArtworkAssembler a;
  feed(a, begin(2, 4));
  feed(a, data(0, {1, 2}));
  const std::vector<uint8_t> payload = {7, 8};
  feed(a, begin(1, 2));  // A new track started mid-transfer.
  feed(a, data(0, {7, 8}));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Complete,
                    feed(a, end(knob::crc32(payload.data(), payload.size()))));
  TEST_ASSERT_EQUAL(1, a.kind());
}

void test_garbage_frames_rejected() {
  ArtworkAssembler a;
  const uint8_t junk[] = {0x09};
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, a.onFrame(junk, 1));
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, a.onFrame(nullptr, 0));
  const uint8_t shortBegin[] = {0x01, 2};
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Rejected, a.onFrame(shortBegin, 2));
}

void test_home_config_round_trip() {
  knob::HomeConfig c;
  c.ssid = "MyWifi";
  c.password = "secret pass";
  c.hubHost = "192.168.1.50";
  c.hubToken = "tok";
  c.hubCertPem = "-----BEGIN CERTIFICATE-----\nAA\n-----END CERTIFICATE-----\n";
  c.lampId = "abc-123";
  const auto bytes = knob::encodeHomeConfig(c);
  knob::HomeConfig back;
  TEST_ASSERT_TRUE(knob::parseHomeConfig(bytes.data(), bytes.size(), &back));
  TEST_ASSERT_EQUAL_STRING("MyWifi", back.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret pass", back.password.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", back.hubHost.c_str());
  TEST_ASSERT_EQUAL_STRING(c.hubCertPem.c_str(), back.hubCertPem.c_str());
  TEST_ASSERT_EQUAL_STRING("abc-123", back.lampId.c_str());
  TEST_ASSERT_TRUE(back.has(knob::HomeConfig::kSsid));
}

void test_home_config_partial_lamp_only() {
  // TLV: key 6, length 3, "xyz".
  const uint8_t raw[] = {6, 3, 0, 'x', 'y', 'z'};
  knob::HomeConfig c;
  TEST_ASSERT_TRUE(knob::parseHomeConfig(raw, sizeof(raw), &c));
  TEST_ASSERT_EQUAL_STRING("xyz", c.lampId.c_str());
  TEST_ASSERT_TRUE(c.has(knob::HomeConfig::kLampId));
  TEST_ASSERT_FALSE(c.has(knob::HomeConfig::kSsid));
  TEST_ASSERT_TRUE(c.onlyLamp());
}

void test_home_config_rejects_overrun() {
  const uint8_t raw[] = {1, 50, 0, 'a'};  // Claims 50 bytes, has 1.
  knob::HomeConfig c;
  TEST_ASSERT_FALSE(knob::parseHomeConfig(raw, sizeof(raw), &c));
  const uint8_t unknownKey[] = {99, 1, 0, 'a'};
  TEST_ASSERT_FALSE(knob::parseHomeConfig(unknownKey, sizeof(unknownKey), &c));
}

void test_home_config_kind_accepted() {
  ArtworkAssembler a;
  TEST_ASSERT_EQUAL(ArtworkAssembler::Result::Pending,
                    feed(a, begin(knob::kArtworkHomeConfig, 6)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_known_value);
  RUN_TEST(test_now_playing_encoding);
  RUN_TEST(test_settings_defaults_round_trip);
  RUN_TEST(test_settings_look);
  RUN_TEST(test_settings_twist);
  RUN_TEST(test_settings_screen_and_custom_colours);
  RUN_TEST(test_settings_are_clamped);
  RUN_TEST(test_settings_reject_garbage);
  RUN_TEST(test_title_overlay_kind_accepted);
  RUN_TEST(test_home_config_round_trip);
  RUN_TEST(test_home_config_partial_lamp_only);
  RUN_TEST(test_home_config_rejects_overrun);
  RUN_TEST(test_home_config_kind_accepted);
  RUN_TEST(test_complete_transfer);
  RUN_TEST(test_bad_crc_is_rejected);
  RUN_TEST(test_missing_bytes_are_rejected);
  RUN_TEST(test_out_of_bounds_data_rejected);
  RUN_TEST(test_oversized_begin_rejected);
  RUN_TEST(test_data_without_begin_rejected);
  RUN_TEST(test_new_begin_discards_partial);
  RUN_TEST(test_garbage_frames_rejected);
  return UNITY_END();
}
