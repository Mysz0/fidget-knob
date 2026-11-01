#include <unity.h>

#include "music_controller.h"

using music::Controller;
using music::Gestures;

void setUp() {}
void tearDown() {}

static ams::Update update(ams::Entity e, uint8_t attr, const char* value) {
  return ams::Update{e, attr, false, value};
}

static int drain(Controller& c, uint32_t now, ams::Command* first) {
  int n = 0;
  ams::Command cmd;
  // Advance time past the rate limit between pops.
  for (uint32_t t = now; c.nextCommand(t, &cmd); t += Controller::kMinCommandGapMs) {
    if (n == 0 && first) *first = cmd;
    ++n;
  }
  return n;
}

void test_disconnected_gestures_do_nothing() {
  Controller c;
  c.onGestures(Gestures{40, true, true}, 0);
  ams::Command cmd;
  TEST_ASSERT_FALSE(c.nextCommand(1000, &cmd));
}

void test_reconnect_does_not_replay_old_commands() {
  Controller c;
  c.setConnected(true, 0);
  c.onGestures(Gestures{0, true, false}, 0);
  c.setConnected(false, 10);
  c.setConnected(true, 20);
  ams::Command cmd;
  TEST_ASSERT_FALSE(c.nextCommand(1000, &cmd));
}

void test_tap_toggles_and_shake_skips() {
  Controller c;
  c.setConnected(true, 0);
  auto fb = c.onGestures(Gestures{0, true, false}, 0);
  TEST_ASSERT_TRUE(fb.toggled);
  ams::Command cmd;
  TEST_ASSERT_TRUE(c.nextCommand(0, &cmd));
  TEST_ASSERT_EQUAL(ams::Command::TogglePlayPause, cmd);

  fb = c.onGestures(Gestures{0, false, true}, 100);
  TEST_ASSERT_TRUE(fb.skipped);
  TEST_ASSERT_TRUE(c.nextCommand(200, &cmd));
  TEST_ASSERT_EQUAL(ams::Command::NextTrack, cmd);
}

void test_one_notch_of_steps_makes_one_volume_command() {
  Controller c;
  c.setConnected(true, 0);
  constexpr int kN = Controller::kStepsPerVolumeCommand;
  auto fb = c.onGestures(Gestures{kN - 1, false, false}, 0);
  TEST_ASSERT_FALSE(fb.volumeStep);
  fb = c.onGestures(Gestures{1, false, false}, 5);
  TEST_ASSERT_TRUE(fb.volumeStep);
  ams::Command first;
  TEST_ASSERT_EQUAL(1, drain(c, 5, &first));
  TEST_ASSERT_EQUAL(ams::Command::VolumeUp, first);
}

void test_direction_change_restarts_accumulator() {
  Controller c;
  c.setConnected(true, 0);
  constexpr int kN = Controller::kStepsPerVolumeCommand;
  c.onGestures(Gestures{kN - 1, false, false}, 0);
  c.onGestures(Gestures{-(kN - 1), false, false}, 5);  // Not enough downward yet.
  ams::Command cmd;
  TEST_ASSERT_FALSE(c.nextCommand(10, &cmd));
  c.onGestures(Gestures{-1, false, false}, 10);
  TEST_ASSERT_TRUE(c.nextCommand(10, &cmd));
  TEST_ASSERT_EQUAL(ams::Command::VolumeDown, cmd);
}

void test_commands_are_rate_limited() {
  Controller c;
  c.setConnected(true, 0);
  c.onGestures(Gestures{2 * Controller::kStepsPerVolumeCommand, false, false}, 0);
  ams::Command cmd;
  TEST_ASSERT_TRUE(c.nextCommand(0, &cmd));
  TEST_ASSERT_FALSE(c.nextCommand(Controller::kMinCommandGapMs - 1, &cmd));
  TEST_ASSERT_TRUE(c.nextCommand(Controller::kMinCommandGapMs, &cmd));
}

void test_notch_is_twenty_four_degrees() {
  // One iOS volume notch per 24 one-degree steps: the full 16-notch range is
  // about one full turn. 8 and then 16 were both reported as too sensitive.
  TEST_ASSERT_EQUAL(24, Controller::kStepsPerVolumeCommand);
}

void test_queue_is_capped_but_tap_survives() {
  Controller c;
  c.setConnected(true, 0);
  c.onGestures(Gestures{Controller::kStepsPerVolumeCommand * 30, false, false}, 0);  // A wild spin.
  c.onGestures(Gestures{0, true, false}, 1);
  int volume = 0, toggles = 0;
  ams::Command cmd;
  for (uint32_t t = 1; c.nextCommand(t, &cmd); t += Controller::kMinCommandGapMs) {
    if (cmd == ams::Command::TogglePlayPause) ++toggles; else ++volume;
  }
  TEST_ASSERT_EQUAL(1, toggles);
  TEST_ASSERT_TRUE(volume + toggles <= (int)Controller::kMaxPending);
}

void test_volume_limit_at_top() {
  Controller c;
  c.setConnected(true, 0);
  c.onUpdate(update(ams::Entity::Player, ams::kPlayerVolume, "1.0"), 0);
  auto fb = c.onGestures(Gestures{Controller::kStepsPerVolumeCommand, false, false}, 10);
  TEST_ASSERT_TRUE(fb.limit);
  ams::Command cmd;
  TEST_ASSERT_FALSE(c.nextCommand(10, &cmd));
}

void test_twist_tracks_mode() {
  Controller c;
  c.setConnected(true, 0);
  c.setTwist(Controller::Twist::Tracks, 24);
  // A volume notch's worth does nothing; three notches skip one track.
  auto fb = c.onGestures(Gestures{24, false, false}, 0);
  TEST_ASSERT_FALSE(fb.skipped);
  fb = c.onGestures(Gestures{48, false, false}, 10);
  TEST_ASSERT_TRUE(fb.skipped);
  ams::Command cmd;
  TEST_ASSERT_TRUE(c.nextCommand(10, &cmd));
  TEST_ASSERT_EQUAL((int)ams::Command::NextTrack, (int)cmd);
  // A huge spin still skips only one.
  c.onGestures(Gestures{-24 * 30, false, false}, 100);
  TEST_ASSERT_TRUE(c.nextCommand(200, &cmd));
  TEST_ASSERT_EQUAL((int)ams::Command::PreviousTrack, (int)cmd);
  TEST_ASSERT_FALSE(c.nextCommand(400, &cmd));
}

void test_twist_none_and_sensitivity() {
  Controller c;
  c.setConnected(true, 0);
  c.setTwist(Controller::Twist::None, 24);
  ams::Command cmd;
  c.onGestures(Gestures{200, false, false}, 0);
  TEST_ASSERT_FALSE(c.nextCommand(0, &cmd));
  c.setTwist(Controller::Twist::Volume, 16);  // High sensitivity.
  auto fb = c.onGestures(Gestures{16, false, false}, 10);
  TEST_ASSERT_TRUE(fb.volumeStep);
}

void test_title_shows_for_three_seconds_on_change() {
  Controller c;
  c.setConnected(true, 0);
  c.onUpdate(update(ams::Entity::Track, ams::kTrackTitle, "Żółć"), 1000);
  c.onUpdate(update(ams::Entity::Track, ams::kTrackArtist, "Artist"), 1000);
  auto v = c.view(1500);
  TEST_ASSERT_TRUE(v.showTitle);
  TEST_ASSERT_EQUAL_STRING("Zolc", v.title.c_str());
  TEST_ASSERT_FALSE(c.view(1000 + Controller::kTitleShowMs + 1).showTitle);
  // The same title re-sent (iOS does this) must not pop it up again.
  c.onUpdate(update(ams::Entity::Track, ams::kTrackTitle, "Żółć"), 9000);
  TEST_ASSERT_FALSE(c.view(9001).showTitle);
}

void test_volume_ring_after_volume_change() {
  Controller c;
  c.setConnected(true, 0);
  c.onUpdate(update(ams::Entity::Player, ams::kPlayerVolume, "0.5"), 0);
  c.onGestures(Gestures{Controller::kStepsPerVolumeCommand, false, false}, 100);
  auto v = c.view(200);
  TEST_ASSERT_TRUE(v.showVolume);
  TEST_ASSERT_TRUE(v.volumeKnown);
  TEST_ASSERT_FALSE(c.view(100 + Controller::kVolumeShowMs + 1).showVolume);
}

void test_playing_state() {
  Controller c;
  c.setConnected(true, 0);
  c.onUpdate(update(ams::Entity::Player, ams::kPlayerPlaybackInfo, "1,1.0,3.0"), 0);
  TEST_ASSERT_TRUE(c.view(1).playing);
  c.onUpdate(update(ams::Entity::Player, ams::kPlayerPlaybackInfo, "0,0.0,3.0"), 2);
  TEST_ASSERT_FALSE(c.view(3).playing);
}

void test_gestures_can_be_remapped() {
  Controller c;
  c.setConnected(true, 0);
  c.setGestureCommands(music::kTapNone, music::kTapNone);
  auto fb = c.onGestures(Gestures{0, true, true}, 0);
  TEST_ASSERT_FALSE(fb.toggled);
  TEST_ASSERT_FALSE(fb.skipped);
  ams::Command cmd;
  TEST_ASSERT_FALSE(c.nextCommand(0, &cmd));

  c.setGestureCommands((int)ams::Command::NextTrack, (int)ams::Command::PreviousTrack);
  c.onGestures(Gestures{0, true, false}, 100);
  TEST_ASSERT_TRUE(c.nextCommand(100, &cmd));
  TEST_ASSERT_EQUAL(ams::Command::NextTrack, cmd);
  c.onGestures(Gestures{0, false, true}, 200);
  TEST_ASSERT_TRUE(c.nextCommand(200, &cmd));
  TEST_ASSERT_EQUAL(ams::Command::PreviousTrack, cmd);
}

void test_title_time_and_off() {
  Controller c;
  c.setConnected(true, 0);
  c.setTitleShowMs(8000);
  c.onUpdate(update(ams::Entity::Track, ams::kTrackTitle, "A"), 0);
  TEST_ASSERT_TRUE(c.view(7000).showTitle);
  c.setTitleShowMs(0);  // Overlay switched off in the app.
  c.onUpdate(update(ams::Entity::Track, ams::kTrackTitle, "B"), 9000);
  TEST_ASSERT_FALSE(c.view(9001).showTitle);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disconnected_gestures_do_nothing);
  RUN_TEST(test_reconnect_does_not_replay_old_commands);
  RUN_TEST(test_tap_toggles_and_shake_skips);
  RUN_TEST(test_one_notch_of_steps_makes_one_volume_command);
  RUN_TEST(test_direction_change_restarts_accumulator);
  RUN_TEST(test_commands_are_rate_limited);
  RUN_TEST(test_notch_is_twenty_four_degrees);
  RUN_TEST(test_queue_is_capped_but_tap_survives);
  RUN_TEST(test_volume_limit_at_top);
  RUN_TEST(test_twist_tracks_mode);
  RUN_TEST(test_twist_none_and_sensitivity);
  RUN_TEST(test_title_shows_for_three_seconds_on_change);
  RUN_TEST(test_volume_ring_after_volume_change);
  RUN_TEST(test_playing_state);
  RUN_TEST(test_gestures_can_be_remapped);
  RUN_TEST(test_title_time_and_off);
  return UNITY_END();
}
