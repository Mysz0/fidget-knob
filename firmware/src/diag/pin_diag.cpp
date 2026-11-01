// Wiring self-test, built only by the `pindiag` environment:
//
//   pio run -e pindiag -t upload && pio device monitor
//
// Checks every signal pin for a short to ground, a short to 3V3, and a short
// to any other signal pin - the faults a soldering slip leaves behind. It
// cannot see an open circuit (a cracked joint or a wire off the panel): the
// panel's inputs draw no current, so a connected input and a dangling one read
// the same. Those need a continuity meter.

#ifdef PIN_DIAG

#include <Arduino.h>

#include "pins.h"

namespace {

struct Pin {
  int gpio;
  const char* label;
};

constexpr Pin kPins[] = {
    {pins::BUTTON, "D0  button"},     {pins::IMU_INT, "D1  IMU INT"},
    {pins::LCD_SCK, "D2  LCD CLK"},   {pins::LCD_BL, "D3  LCD BL"},
    {pins::LCD_DC, "D4  LCD DC"},     {pins::LCD_RST, "D5  LCD RST"},
    {pins::LCD_CS, "D6  LCD CS"},     {pins::MOTOR, "D7  motor"},
    {pins::I2C_SDA, "D8  IMU SDA"},   {pins::I2C_SCL, "D9  IMU SCL"},
    {pins::LCD_MOSI, "D10 LCD DIN"},
};
constexpr int kCount = sizeof(kPins) / sizeof(kPins[0]);

void allFloat(int mode) {
  for (const Pin& p : kPins) pinMode(p.gpio, mode);
  delay(2);
}

void run() {
  Serial.println("\n=== pin self-test ===");

  // Alone: what each pin reads when only the weak internal pull holds it.
  // Pull-up reading 0 = something pulls it to ground hard.
  // Pull-down reading 1 = something pulls it to 3V3 (the I2C lines and the
  // button are pulled up on purpose; everything else should not be).
  int up[kCount], down[kCount];
  allFloat(INPUT_PULLUP);
  for (int i = 0; i < kCount; ++i) up[i] = digitalRead(kPins[i].gpio);
  allFloat(INPUT_PULLDOWN);
  for (int i = 0; i < kCount; ++i) down[i] = digitalRead(kPins[i].gpio);

  int problems = 0;
  for (int i = 0; i < kCount; ++i) {
    const char* verdict = "ok";
    if (up[i] == 0) {
      verdict = "SHORT TO GND (reads 0 even with pull-up)";
      ++problems;
    } else if (down[i] == 1) {
      verdict = "held high externally (normal for SDA/SCL/button)";
    }
    Serial.printf("  %-14s pullup=%d pulldown=%d  %s\n", kPins[i].label, up[i],
                  down[i], verdict);
  }

  // Pairs: drive one pin, and see whether another follows it against its own
  // weak pull in the opposite direction. Only a real connection can win.
  Serial.println("\n  pin-to-pin shorts:");
  int shorts = 0;
  for (int a = 0; a < kCount; ++a) {
    for (int b = 0; b < kCount; ++b) {
      if (a == b) continue;
      allFloat(INPUT_PULLDOWN);
      pinMode(kPins[a].gpio, OUTPUT);
      digitalWrite(kPins[a].gpio, HIGH);
      delay(1);
      const bool followsHigh = digitalRead(kPins[b].gpio) == 1 && down[b] == 0;

      allFloat(INPUT_PULLUP);
      pinMode(kPins[a].gpio, OUTPUT);
      digitalWrite(kPins[a].gpio, LOW);
      delay(1);
      const bool followsLow = digitalRead(kPins[b].gpio) == 0 && up[b] == 1;

      if ((followsHigh || followsLow) && a < b) {
        Serial.printf("    SHORT: %s <-> %s\n", kPins[a].label,
                      kPins[b].label);
        ++shorts;
      }
    }
  }
  allFloat(INPUT);
  if (shorts == 0) Serial.println("    none");

  Serial.printf("\n=== %d pin(s) shorted to GND, %d pin-to-pin short(s) ===\n",
                problems, shorts);
  Serial.println("Opens (broken wires) are invisible here - use a meter.");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);  // Let the USB serial come up so the report is not lost.
}

void loop() {
  run();
  delay(5000);
}

#endif  // PIN_DIAG
