#include "target.h"

namespace target {

void Registry::add(Target* t) {
  if (!t || count_ >= kMaxTargets) return;
  targets_[count_++] = t;
}

Target* Registry::at(int index) const {
  if (index < 0 || index >= count_) return nullptr;
  return targets_[index];
}

Target* Registry::current() const { return at(current_); }

void Registry::select(int index) {
  if (index < 0 || index >= count_) return;
  current_ = index;
}

void Registry::step(int delta) {
  if (count_ == 0) return;
  // Wrapping arithmetic that stays correct for negative deltas larger than the
  // list, which a fast spin in the picker can produce.
  int next = (current_ + delta) % count_;
  if (next < 0) next += count_;
  current_ = next;
}

}  // namespace target
