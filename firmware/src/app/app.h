#pragma once

// The state machine that wires input events to targets and drives the display.
//
// This is the only module that knows about all the others. Everything below it
// is independently testable and has no idea the rest exists.

namespace app {

enum class Mode {
  Brightness,  // Twist adjusts level.
  Colour,      // Twist adjusts hue. Only reachable on colour-capable targets.
  Picker,      // Button held: twist scrolls targets.
};

void begin();

// One pass of the main loop.
void loop();

}  // namespace app
