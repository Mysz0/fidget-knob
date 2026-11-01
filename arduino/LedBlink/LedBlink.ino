// Tests if the chip runs code at all, without needing Serial/USB-CDC to work.
// The XIAO's onboard LED blinks - watch the board, not the screen.

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
}
