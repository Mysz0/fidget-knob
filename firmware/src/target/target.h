#pragma once

#include <stdint.h>

// What the knob can control.
//
// Everything above this interface - gestures, haptics, the sprite field - is
// written against `Target` and knows nothing about DIRIGERA, HTTP or JSON.
// Adding PC volume later means writing one more implementation of this
// interface and registering it; no other module changes.
//
// Values are normalised (0..1 for level and saturation, degrees for hue) so
// backends with different native ranges do not leak their units upward.

namespace target {

class Target {
 public:
  virtual ~Target() = default;

  virtual const char* name() const = 0;

  // Not every backend has colour. The app hides colour mode for those rather
  // than offering a control that silently does nothing.
  virtual bool supportsColour() const = 0;

  virtual bool isOn() const = 0;
  virtual float level() const = 0;       // 0..1
  virtual float hue() const = 0;         // 0..360
  virtual float saturation() const = 0;  // 0..1

  // Setters update the local cache immediately and schedule a network write.
  // The display therefore tracks the user's hand rather than the round trip,
  // which is the difference between the knob feeling direct and feeling remote.
  virtual void setLevel(float value) = 0;
  virtual void setColour(float hue, float saturation) = 0;
  virtual void toggle() = 0;

  // Flushes any scheduled write. Called from the main loop.
  virtual void tick() = 0;
};

// The set of things the picker can cycle through.
class Registry {
 public:
  static constexpr int kMaxTargets = 12;

  void add(Target* t);
  int count() const { return count_; }

  Target* at(int index) const;
  Target* current() const;

  int currentIndex() const { return current_; }
  void select(int index);

  // Steps through the list, wrapping. Used by the picker while twisting.
  void step(int delta);

 private:
  Target* targets_[kMaxTargets] = {nullptr};
  int count_ = 0;
  int current_ = 0;
};

}  // namespace target
