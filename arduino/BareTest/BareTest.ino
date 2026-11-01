// Absolute minimum test. No pins, no logic, just proves Serial and the chip
// itself are alive. If this prints nothing, the problem is not in any of the
// other sketches - it's the board or the connection.

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("alive");
  delay(500);
}
