// Fidget Knob — display bring-up test
//
// The smallest thing that proves the panel, the wiring and the SPI bus all
// work. Run this BEFORE anything else. If this does not show colour, nothing
// further is worth debugging.
//
// ---------------------------------------------------------------------------
// ARDUINO IDE SETUP
// ---------------------------------------------------------------------------
//
// Tools > Board            : "XIAO_ESP32C3"   (ESP32 core by Espressif)
// Tools > USB CDC On Boot  : "Enabled"        <- REQUIRED, see note below
// Tools > Upload Speed     : 921600
// Tools > Port             : the /dev/cu.usbmodem* that appears when plugged in
//
// Sketch > Include Library > Manage Libraries... > install "LovyanGFX"
//
// USB CDC On Boot must be Enabled because this design uses GPIO20 and GPIO21
// (UART0 RX/TX) for the display clock and chip select. With CDC disabled,
// Serial would try to drive those same pins and fight the panel.
//
// ---------------------------------------------------------------------------
// WIRING  (Xiao silkscreen -> panel)
// ---------------------------------------------------------------------------
//
//   3V3  -> VCC      GND  -> GND
//   D10  -> DIN      D2   -> CLK
//   D6   -> CS       D4   -> DC
//   D5   -> RST      D3   -> BL
//
// CLK goes to D2. Not D8 (a strapping pin an idle-low clock would hold low at
// reset, breaking boot) and not D7 (UART0 RX, which the IDF console holds).

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Pin numbers here are GPIO numbers, not the Dn silkscreen labels.
static constexpr int PIN_CS   = 21;  // D6
static constexpr int PIN_DC   = 6;   // D4
static constexpr int PIN_RST  = 7;   // D5
static constexpr int PIN_MOSI = 10;  // D10
static constexpr int PIN_SCK  = 4;   // D2  <- was D7/GPIO20, see note below
static constexpr int PIN_BL   = 5;   // D3

// Why the clock is on D2 and not D7.
//
// GPIO20 is UART0 RX. Even with USB CDC selected for Serial, the ESP-IDF
// console underneath Arduino still claims UART0 on GPIO20/21, and reassigning
// the pin through the GPIO matrix is a fight the display library does not
// reliably win. The clock is the worst signal to lose that fight on: without
// it nothing clocks in at all and the panel stays lit but blank, at any speed.
//
// GPIO4 is an ordinary GPIO with no alternate function competing for it.

// SPI clock speed.
//
// The ESP32-C3's SPI peripheral maxes out at 80 MHz, so anything above that is
// silently clamped rather than honoured - asking for 100 MHz does not fail
// loudly, it just gives you a bus running far faster than loose dupont leads
// can carry.
//
// The failure mode is subtle and worth recognising: the panel still works, but
// individual bytes in the pixel stream get corrupted, so scattered pixels are
// written the wrong colour and hold it until something overwrites them. It
// looks like a single "lagging" or "stuck" pixel, which reads as a hardware
// defect rather than a bus problem.
//
// 10 MHz is safe on jumper wires. On the final soldered build, with short
// traces, 40 MHz is comfortable. Raise it until artifacts appear, then halve.
//
// Do not go far BELOW 10 MHz either. A full 240x240 frame is 921,600 bits, so
// the clock sets the frame time directly:
//
//    40 MHz -> 23 ms/frame     usable, what the real firmware needs
//    10 MHz -> 92 ms/frame     fine for testing
//   100 kHz -> 9.2 s/frame     you can watch it paint
//    10 kHz -> 92 s/frame      a minute and a half per screen
//
// At the low end every pixel visibly lags, because you are literally watching
// the frame being written one row at a time. That is not a fault.
static constexpr int SPI_HZ = 10000000;

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host   = SPI2_HOST;  // The C3's only general-purpose SPI host.
      cfg.spi_mode   = 0;
      cfg.freq_write = SPI_HZ;
      cfg.freq_read  = 8000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_SCK;
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = -1;           // Write-only.
      cfg.pin_dc   = PIN_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs   = PIN_CS;
      cfg.pin_rst  = PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable = false;
      // GC9A01 panels ship inverted. If your colours come out as negatives,
      // this is the line to flip.
      cfg.invert = true;
      // If red and blue are swapped, flip this instead.
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};

Panel lcd;

// Wiggles each SPI line on its own, slowly, before the display is initialised.
//
// A blank-but-lit panel cannot tell you WHICH signal is wrong, so this makes
// each one individually observable: with a multimeter on the panel-side header
// pin you should see it swing between 0 V and 3.3 V while its name is printed.
// A pin that never moves is not connected to the pin you think it is.
void pinWiggleTest() {
  const struct {
    const char* name;
    int pin;
  } lines[] = {
      {"CS   (D6, panel CS)", PIN_CS},
      {"DC   (D4, panel DC)", PIN_DC},
      {"RST  (D5, panel RST)", PIN_RST},
      {"MOSI (D10, panel DIN)", PIN_MOSI},
      {"SCK  (D2, panel CLK)", PIN_SCK},
  };

  Serial.println("\n--- pin wiggle test ---");
  Serial.println("Probe each panel header pin as its name is printed.");

  for (const auto& line : lines) {
    pinMode(line.pin, OUTPUT);
    Serial.printf("  %-24s GPIO%d  toggling 2s\n", line.name, line.pin);
    for (int i = 0; i < 10; ++i) {
      digitalWrite(line.pin, HIGH);
      delay(100);
      digitalWrite(line.pin, LOW);
      delay(100);
    }
  }
  Serial.println("--- wiggle test done ---\n");
}

void setup() {
  Serial.begin(115200);

  // Native USB takes a moment to enumerate after reset. Without this the first
  // few prints vanish and it looks like the sketch never ran.
  const uint32_t serialDeadline = millis() + 2000;
  while (!Serial && millis() < serialDeadline) delay(10);

  Serial.println();
  Serial.println("=== Fidget Knob display test ===");
  Serial.printf("CS=GPIO%d DC=GPIO%d RST=GPIO%d MOSI=GPIO%d SCK=GPIO%d BL=GPIO%d\n",
                PIN_CS, PIN_DC, PIN_RST, PIN_MOSI, PIN_SCK, PIN_BL);
  Serial.printf("SPI clock: %d Hz\n", SPI_HZ);

  // Backlight full on. Driven directly rather than via PWM so this test has
  // one less thing that can be wrong.
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  Serial.println("backlight on — if the panel is not glowing, BL or power is wrong");

  // Set WIGGLE_TEST to true if the panel stays blank, to find the bad wire.
  const bool WIGGLE_TEST = false;
  if (WIGGLE_TEST) pinWiggleTest();

  Serial.println("initialising panel...");
  lcd.init();
  Serial.println("lcd.init() returned");
  lcd.setRotation(0);

  // Solid colour sweep. If the panel is alive but the colour order is wrong,
  // this makes it obvious immediately: the labels will not match the fill.
  const uint16_t colours[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char* names[] = {"RED", "GREEN", "BLUE", "WHITE"};

  for (int i = 0; i < 4; ++i) {
    lcd.fillScreen(colours[i]);
    lcd.setTextColor(TFT_BLACK, colours[i]);
    lcd.setTextDatum(middle_center);
    lcd.setTextSize(3);
    lcd.drawString(names[i], 120, 120);
    delay(700);
  }

  // Colour check: pure green next to pure yellow.
  //
  // Cheap panels often have a weak green primary, so pure green reads as
  // yellow-green. That is gamut, and harmless. What is NOT harmless is the
  // two bars looking identical - that would mean the red channel is being set
  // when it should be off, i.e. a pixel-format or bit-order fault.
  //
  // Green is 0x07E0 (red bits all zero). Yellow is 0xFFE0 (red bits all one).
  // If those two produce the same colour on screen, the red bits are not
  // reaching the panel correctly.
  lcd.fillScreen(TFT_BLACK);
  const uint16_t swatches[] = {TFT_RED, TFT_GREEN, TFT_YELLOW, TFT_BLUE};
  const char* swatchNames[] = {"R", "G", "Y", "B"};
  for (int i = 0; i < 4; ++i) {
    lcd.fillRect(30 + i * 45, 70, 45, 100, swatches[i]);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.setTextSize(2);
    lcd.drawString(swatchNames[i], 52 + i * 45, 190);
  }
  delay(4000);

  lcd.fillScreen(TFT_BLACK);

  // Concentric rings, to check the round panel is centred and nothing is
  // clipped at the edges. If the circle is off-centre or cut off, the panel
  // offsets are wrong.
  for (int r = 118; r > 0; r -= 12) {
    lcd.drawCircle(120, 120, r, lcd.color565(0, 255 - r, 120));
  }

  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(2);
  lcd.drawString("OK", 120, 120);

  Serial.println("display init complete");
}

void loop() {
  // A slow heartbeat on the backlight, and a matching serial tick.
  //
  // These two together separate "the sketch crashed" from "the sketch runs but
  // the panel is not responding": if the light breathes and the ticks keep
  // printing, the code is fine and the problem is the SPI wiring.
  static int level = 40;
  static int step = 4;
  static uint32_t lastTick = 0;

  level += step;
  if (level >= 255 || level <= 40) step = -step;
  analogWrite(PIN_BL, level);

  if (millis() - lastTick > 2000) {
    lastTick = millis();
    Serial.printf("alive, uptime %lus\n", (unsigned long)(millis() / 1000));
  }

  delay(15);
}
