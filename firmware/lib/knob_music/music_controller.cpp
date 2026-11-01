#include "music_controller.h"

#include "ascii_fold.h"

namespace music {

void Controller::setConnected(bool connected, uint32_t nowMs) {
  (void)nowMs;
  if (connected == connected_) return;
  connected_ = connected;
  // Whatever was queued belonged to a link that is gone. Replaying it on
  // reconnect would pause or skip music the user has since moved on from.
  pending_.clear();
  stepAccumulator_ = 0;
}

void Controller::setGestureCommands(int tapCommand, int shakeCommand) {
  tapCommand_ = tapCommand;
  shakeCommand_ = shakeCommand;
}

void Controller::onUpdate(const ams::Update& u, uint32_t nowMs) {
  if (u.entity == ams::Entity::Track) {
    if (u.attribute == ams::kTrackTitle) {
      // iOS re-sends the current title on reconnect and on some state changes;
      // only a different title is a track change worth announcing.
      if (u.value != rawTitle_) {
        rawTitle_ = u.value;
        title_ = knob::foldToAscii(u.value, kTitleChars);
        titleShown_ = !title_.empty();
        titleSinceMs_ = nowMs;
      }
    } else if (u.attribute == ams::kTrackArtist) {
      artist_ = knob::foldToAscii(u.value, kArtistChars);
    }
  } else if (u.entity == ams::Entity::Player) {
    if (u.attribute == ams::kPlayerPlaybackInfo) {
      bool playing;
      if (ams::parsePlaying(u.value, &playing)) playing_ = playing;
    } else if (u.attribute == ams::kPlayerVolume) {
      float v;
      if (ams::parseVolume(u.value, &v)) {
        volume_ = v;
        volumeKnown_ = true;
      }
    }
  }
}

void Controller::enqueue(ams::Command cmd) {
  const bool isVolume =
      cmd == ams::Command::VolumeUp || cmd == ams::Command::VolumeDown;
  if (pending_.size() >= kMaxPending) {
    // A wild spin can outrun the radio. Extra volume steps are dropped; a tap
    // or shake makes room by evicting the oldest volume step, because losing
    // a play/pause the user felt would be far worse than losing one notch.
    if (isVolume) return;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (*it == ams::Command::VolumeUp || *it == ams::Command::VolumeDown) {
        pending_.erase(it);
        break;
      }
    }
    if (pending_.size() >= kMaxPending) pending_.pop_front();
  }
  pending_.push_back(cmd);
}

Feedback Controller::onGestures(const Gestures& g, uint32_t nowMs) {
  Feedback fb{false, false, false, false};
  if (!connected_) return fb;

  if (g.twistSteps != 0 && twist_ == Twist::Tracks) {
    if ((stepAccumulator_ > 0 && g.twistSteps < 0) ||
        (stepAccumulator_ < 0 && g.twistSteps > 0)) {
      stepAccumulator_ = 0;
    }
    stepAccumulator_ += g.twistSteps;
    const int notch = stepsPerNotch_ * kTrackNotchFactor;
    if (stepAccumulator_ >= notch || stepAccumulator_ <= -notch) {
      const bool forward = stepAccumulator_ > 0;
      stepAccumulator_ = 0;  // One track per turn, however fast.
      enqueue(forward ? ams::Command::NextTrack : ams::Command::PreviousTrack);
      fb.skipped = true;
    }
  } else if (g.twistSteps != 0 && twist_ == Twist::Volume) {
    // Turning back the other way starts a fresh notch rather than first
    // unwinding the half-notch already turned.
    if ((stepAccumulator_ > 0 && g.twistSteps < 0) ||
        (stepAccumulator_ < 0 && g.twistSteps > 0)) {
      stepAccumulator_ = 0;
    }
    stepAccumulator_ += g.twistSteps;

    while (stepAccumulator_ >= stepsPerNotch_ ||
           stepAccumulator_ <= -stepsPerNotch_) {
      const bool up = stepAccumulator_ > 0;
      stepAccumulator_ += up ? -stepsPerNotch_ : stepsPerNotch_;

      if (volumeKnown_ && ((up && volume_ >= 0.999f) || (!up && volume_ <= 0.001f))) {
        fb.limit = true;
        stepAccumulator_ = 0;
        break;
      }
      enqueue(up ? ams::Command::VolumeUp : ams::Command::VolumeDown);
      fb.volumeStep = true;
      volumeShown_ = true;
      volumeSinceMs_ = nowMs;
    }
  }

  if (g.tap && tapCommand_ != kTapNone) {
    enqueue((ams::Command)tapCommand_);
    fb.toggled = true;
  }
  if (g.shake && shakeCommand_ != kTapNone) {
    enqueue((ams::Command)shakeCommand_);
    fb.skipped = true;
  }
  return fb;
}

bool Controller::nextCommand(uint32_t nowMs, ams::Command* out) {
  if (!connected_ || pending_.empty()) return false;
  if (sentAny_ && nowMs - lastSentMs_ < kMinCommandGapMs) return false;
  *out = pending_.front();
  pending_.pop_front();
  lastSentMs_ = nowMs;
  sentAny_ = true;
  return true;
}

View Controller::view(uint32_t nowMs) const {
  View v;
  v.title = title_;
  v.artist = artist_;
  v.playing = playing_;
  v.volumeKnown = volumeKnown_;
  v.volume = volume_;
  v.connected = connected_;
  v.showTitle = titleShown_ && titleShowMs_ > 0 &&
                nowMs - titleSinceMs_ <= titleShowMs_;
  v.showVolume = volumeShown_ && nowMs - volumeSinceMs_ <= kVolumeShowMs;
  return v;
}

}  // namespace music
