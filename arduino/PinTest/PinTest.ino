// Fidget Knob — solder joint tester
//
// Finds bad header joints without a multimeter.
//
// The trick: drive one pin low, put every other pin on an internal pull-up, and
// see which one follows. If two pins are joined by a jumper wire and BOTH their
// solder joints are good, the sketch sees them linked. If either joint is cold,
// it sees nothing — which is exactly the information you want.
//
// ---------------------------------------------------------------------------
// HOW TO USE
// ---------------------------------------------------------------------------
//
// 1. UNPLUG THE LCD. Every wire off. The scan needs the pins floating, and a
//    connected panel will confuse the readings.
//
// 2. Upload this, open Serial Monitor at 115200.
//
// 3. Put ONE male-male jumper between two breadboard rows - say D0 and D1.
//
// 4. The sketch prints which pins it sees linked, twice a second.
//
//      "LINK: D0 <-> D1"   both joints good
//      "no links found"    at least one of the two is cold - reflow it
//
// 5. Move one end of the jumper down the row and repeat until every pin has
//    been in a working link at least once.
//
// A fast way to cover everything: leave one end parked on a pin you have
// already proved good, and walk the other end along all the remaining pins.

#include <Arduino.h>

struct Pin {
  const char* name;
  uint8_t gpio;
};

// Every broken-out pin on the XIAO except 3V3, GND and VUSB, which are power
// rails rather than GPIOs and cannot be tested this way.
const Pin PINS[] = {
    {"D0",  2},
    {"D1",  3},
    {"D2",  4},
    {"D3",  5},
    {"D4",  6},
    {"D5",  7},
    {"D6",  21},
    {"D7",  20},
    {"D8",  8},
    {"D9",  9},
    {"D10", 10},
};
constexpr int PIN_COUNT = sizeof(PINS) / sizeof(PINS[0]);

void allInputs() {
  for (int i = 0; i < PIN_COUNT; ++i) {
    pinMode(PINS[i].gpio, INPUT_PULLUP);
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) delay(10);

  Serial.println();
  Serial.println("=== solder joint tester ===");
  Serial.println("UNPLUG THE LCD first - all wires off.");
  Serial.println("Put one jumper between two rows, watch for LINK lines.");
  Serial.println();

  allInputs();
}

void loop() {
  bool foundAny = false;

  for (int i = 0; i < PIN_COUNT; ++i) {
    // Everything on a pull-up, so an unconnected pin reads HIGH.
    allInputs();

    // SAFETY CHECK - do not skip this.
    //
    // If this pin is wired to 3V3, driving it LOW shorts the power rail
    // straight through the GPIO's output transistor and destroys it. The pin
    // dies silently: flashing still works, the chip still reports its MAC, but
    // application code stops behaving and it looks like a mysterious brick.
    //
    // An internal pull-DOWN is far weaker than a real 3V3 connection, so if the
    // pin still reads HIGH while pulled down, something external is holding it
    // up - a power rail. Leave that pin alone.
    pinMode(PINS[i].gpio, INPUT_PULLDOWN);
    delayMicroseconds(300);
    if (digitalRead(PINS[i].gpio) == HIGH) {
      Serial.printf("SKIP %s - tied to a power rail, not driving it\n",
                    PINS[i].name);
      continue;
    }

    // Safe: drive it low. Anything wired to it gets dragged low too.
    pinMode(PINS[i].gpio, OUTPUT);
    digitalWrite(PINS[i].gpio, LOW);
    delayMicroseconds(300);  // Let the line settle before reading.

    for (int j = i + 1; j < PIN_COUNT; ++j) {
      if (digitalRead(PINS[j].gpio) == LOW) {
        Serial.printf("LINK: %s <-> %s\n", PINS[i].name, PINS[j].name);
        foundAny = true;
      }
    }
  }

  allInputs();

  if (!foundAny) {
    // Either no jumper is fitted, or one of the two joints is not conducting.
    // From the chip's point of view those look identical, which is why the
    // instruction is to always have a jumper in place while watching this.
    Serial.println("no links found");
  }
  Serial.println("---");

  delay(500);
}
