#pragma once

#include <stdint.h>

// The single centre button.
//
// Press and release are reported separately rather than as one "click" event,
// because holding is a live mode: while the button is down the target picker is
// open and twisting scrolls it. The app needs to know the moment the hold
// begins, not just after the fact.

namespace button {

struct Events {
  bool shortPress;  // Released before the hold threshold. Toggles power.
  bool holdBegan;   // Held past the threshold. Opens the target picker.
  bool holdEnded;   // Released after a hold. Commits the picker selection.
};

void begin();

// Poll from the main loop.
Events update();

// True while the button is physically down.
bool isDown();

}  // namespace button
