// Proves the chip executes code, checkable with a multimeter, no Serial or LED
// needed. Sets D0 high forever and does nothing else.
//
// After uploading, measure with the multimeter on DC volts:
//   red probe  -> D0 (row where D0 sits, or the pin directly)
//   black probe -> GND
//
// Should read close to 3.3V. If it reads 0V, either the chip isn't reaching
// this line of code, or D0 itself is not the pin you're measuring.

void setup() {
  pinMode(2, OUTPUT);  // D0 = GPIO2
  digitalWrite(2, HIGH);
}

void loop() {
  // Nothing - the pin just stays high.
}
