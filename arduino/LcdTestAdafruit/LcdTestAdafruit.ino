// Fidget Knob — display bring-up test (Adafruit library version)
//
// Same purpose as ../LcdTest, written against Adafruit GC9A01A + GFX + BusIO
// instead of LovyanGFX. Use whichever you already have installed.
//
// The real firmware needs LovyanGFX, because it renders in bands pushed by DMA
// and Adafruit GFX has no asynchronous path. This sketch is only for proving
// the panel and the wiring are alive.
//
// ---------------------------------------------------------------------------
// ARDUINO IDE SETUP
// ---------------------------------------------------------------------------
//
// Tools > Board            : "XIAO_ESP32C3"
// Tools > USB CDC On Boot  : "Enabled"      <- REQUIRED
// Tools > Port             : the /dev/cu.usbmodem* that appears when plugged in
//
// USB CDC On Boot must be Enabled because this design uses GPIO20 and GPIO21
// (UART0 RX/TX) for the display clock and chip select. With CDC disabled,
// Serial drives those same pins and fights the panel.
//
// ---------------------------------------------------------------------------
// WIRING  (Xiao silkscreen -> panel)
// ---------------------------------------------------------------------------
//
//   3V3  -> VCC      GND  -> GND
//   D10  -> DIN      D7   -> CLK
//   D6   -> CS       D4   -> DC
//   D5   -> RST      D3   -> BL

#include <Adafruit_GC9A01A.h>
#include <Adafruit_GFX.h>
#include <SPI.h>

// GPIO numbers, not the Dn silkscreen labels.
static constexpr int PIN_CS   = 21;  // D6
static constexpr int PIN_DC   = 6;   // D4
static constexpr int PIN_RST  = 7;   // D5
static constexpr int PIN_MOSI = 10;  // D10
static constexpr int PIN_SCK  = 20;  // D7
static constexpr int PIN_BL   = 5;   // D3

// Hardware SPI. The pins are remapped in setup() before this is initialised —
// see the note there, it is the part that catches people out.
Adafruit_GC9A01A tft(PIN_CS, PIN_DC, PIN_RST);

// Adafruit GFX has no centred-text helper, so measure and place manually.
void drawCentred(const char* text, int cx, int cy, uint16_t fg, uint16_t bg,
                 uint8_t size) {
  tft.setTextSize(size);
  tft.setTextColor(fg, bg);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(cx - w / 2, cy - h / 2);
  tft.print(text);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  // Remap SPI before the display library touches it.
  //
  // The Xiao's default hardware-SPI pins are SCK=D8, MISO=D9, MOSI=D10, but
  // this design needs D8/D9 for I2C and puts the clock on D7. Adafruit_SPITFT
  // calls SPI.begin() with no arguments inside tft.begin(), which would take
  // the defaults — however the ESP32 core's SPIClass::begin() returns early if
  // the bus is already initialised. Claiming it first with the pins we want is
  // therefore what makes the remap stick.
  SPI.begin(PIN_SCK, -1 /* no MISO, write-only */, PIN_MOSI, PIN_CS);

  tft.begin();
  tft.setRotation(0);

  // Solid colour sweep with matching labels. If the panel is alive but the
  // colour order is wrong, the label will disagree with the fill and you will
  // see it instantly.
  const uint16_t colours[] = {GC9A01A_RED, GC9A01A_GREEN, GC9A01A_BLUE,
                              GC9A01A_WHITE};
  const char* names[] = {"RED", "GREEN", "BLUE", "WHITE"};

  for (int i = 0; i < 4; ++i) {
    tft.fillScreen(colours[i]);
    drawCentred(names[i], 120, 120, GC9A01A_BLACK, colours[i], 3);
    delay(700);
  }

  tft.fillScreen(GC9A01A_BLACK);

  // Concentric rings, to check the round panel is centred and nothing is
  // clipped at the edge.
  for (int r = 118; r > 0; r -= 12) {
    tft.drawCircle(120, 120, r, tft.color565(0, 255 - r, 120));
  }

  drawCentred("OK", 120, 120, GC9A01A_WHITE, GC9A01A_BLACK, 2);

  Serial.println("display init complete");
}

void loop() {
  // Sweep the backlight, so you can confirm D3 actually controls brightness.
  // A screen at constant brightness means BL is not connected to D3.
  static int level = 0;
  static int step = 5;

  level += step;
  if (level >= 255 || level <= 20) step = -step;

  analogWrite(PIN_BL, level);
  delay(15);
}
