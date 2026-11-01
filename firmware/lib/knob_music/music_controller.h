#pragma once

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <string>

#include "ams_codec.h"

// Music mode's brain, with no Bluetooth and no screen in it: gestures and AMS
// updates go in; a rate-limited queue of AMS commands and a view of what to
// draw come out. Time is passed in, so it is fully testable on the host.

namespace music {

// A gesture mapped to nothing.
constexpr int kTapNone = -1;

struct Gestures {
  int twistSteps;  // Positive = the direction that raises brightness in Lights.
  bool tap;
  bool shake;
};

// What the gesture just did, for haptics.
struct Feedback {
  bool volumeStep;  // A volume command was queued.
  bool limit;       // Turned past the end of the volume range.
  bool toggled;     // Play/pause queued.
  bool skipped;     // Next track queued.
};

struct View {
  std::string title;   // ASCII-folded, length-capped.
  std::string artist;
  bool playing;
  bool volumeKnown;
  float volume;        // 0..1
  bool connected;
  bool showTitle;      // Within kTitleShowMs of a track change.
  bool showVolume;     // Within kVolumeShowMs of a volume change.
};

class Controller {
 public:
  // iOS moves system volume in 16 notches. 24 one-degree twist steps per
  // notch puts the whole range in about one full turn; 8 and then 16 per
  // notch were both too sensitive in the hand.
  static constexpr int kStepsPerVolumeCommand = 24;
  static constexpr uint32_t kTitleShowMs = 3000;
  static constexpr uint32_t kVolumeShowMs = 1500;
  // AMS commands sent back to back get dropped by some players; 60 ms apart
  // still keeps up with a brisk twist.
  static constexpr uint32_t kMinCommandGapMs = 60;
  static constexpr size_t kMaxPending = 8;
  static constexpr size_t kTitleChars = 22;
  static constexpr size_t kArtistChars = 26;

  void setConnected(bool connected, uint32_t nowMs);

  // What tap and shake send (an ams::Command value, or kTapNone). Defaults:
  // tap = play/pause, shake = next track. Set from the app's settings.
  void setGestureCommands(int tapCommand, int shakeCommand);

  // What twisting does. Tracks: a turn of kTrackNotchFactor volume notches
  // skips one track. `stepsPerNotch` is the sensitivity (default 24).
  enum class Twist : uint8_t { None, Volume, Tracks };
  static constexpr int kTrackNotchFactor = 3;
  void setTwist(Twist twist, int stepsPerNotch) {
    twist_ = twist;
    stepsPerNotch_ = stepsPerNotch > 0 ? stepsPerNotch : kStepsPerVolumeCommand;
    stepAccumulator_ = 0;
  }

  // How long the title shows after a track change; 0 turns it off.
  void setTitleShowMs(uint32_t ms) { titleShowMs_ = ms; }
  void onUpdate(const ams::Update& update, uint32_t nowMs);
  Feedback onGestures(const Gestures& g, uint32_t nowMs);

  // Pops the next command if one is due. False when idle or rate-limited.
  bool nextCommand(uint32_t nowMs, ams::Command* out);

  View view(uint32_t nowMs) const;

 private:
  void enqueue(ams::Command cmd);

  bool connected_ = false;
  int tapCommand_ = (int)ams::Command::TogglePlayPause;
  int shakeCommand_ = (int)ams::Command::NextTrack;
  uint32_t titleShowMs_ = kTitleShowMs;
  std::deque<ams::Command> pending_;
  uint32_t lastSentMs_ = 0;
  bool sentAny_ = false;
  int stepAccumulator_ = 0;
  Twist twist_ = Twist::Volume;
  int stepsPerNotch_ = kStepsPerVolumeCommand;

  std::string rawTitle_;
  std::string title_;
  std::string artist_;
  bool playing_ = false;
  bool volumeKnown_ = false;
  float volume_ = 0.0f;
  bool titleShown_ = false;
  uint32_t titleSinceMs_ = 0;
  bool volumeShown_ = false;
  uint32_t volumeSinceMs_ = 0;
};

}  // namespace music
