// Fidget Knob — IMU bring-up test
//
// Proves the MPU-6050 talks, and previews the core interaction: a needle that
// tracks how far you have twisted the device, plus shake detection. If the
// needle follows your wrist and holds still when you do, the gesture layer will
// work.
//
// Run this AFTER LcdTest passes.
//
// ---------------------------------------------------------------------------
// CONFIGURED PIN ASSIGNMENTS — physical MPU wiring not yet confirmed
// See docs/wiring.md before treating this as a wiring reference.
// ---------------------------------------------------------------------------
//
//   MPU VCC -> 3V3        MPU GND -> GND
//   MPU SDA -> D8         MPU SCL -> D9
//   MPU INT -> D1         (only if your module breaks INT out — see below)
//
// D8/D9 are strapping pins that must read HIGH at reset. I2C is safe there
// precisely because the module's pull-up resistors hold both lines high.
//
// If your module has no INT pin, everything here still works. INT is only
// needed later, for waking from deep sleep on motion.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>

// --- pins (GPIO numbers, not Dn labels) ---
static constexpr int PIN_CS   = 21;  // D6
static constexpr int PIN_DC   = 6;   // D4
static constexpr int PIN_RST  = 7;   // D5
static constexpr int PIN_MOSI = 10;  // D10
static constexpr int PIN_SCK  = 4;   // D2
static constexpr int PIN_BL   = 5;   // D3
static constexpr int PIN_SDA  = 8;   // D8
static constexpr int PIN_SCL  = 9;   // D9

static constexpr int SPI_HZ = 20000000;

// --- MPU-6050 registers ---
static constexpr uint8_t MPU_ADDR = 0x68;
static constexpr uint8_t REG_SMPLRT_DIV   = 0x19;
static constexpr uint8_t REG_CONFIG       = 0x1A;
static constexpr uint8_t REG_GYRO_CONFIG  = 0x1B;
static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
static constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
static constexpr uint8_t REG_WHO_AM_I     = 0x75;

static constexpr float GYRO_LSB_PER_DPS = 65.5f;   // +/-500 dps
static constexpr float ACCEL_LSB_PER_G  = 16384.0f;  // +/-2 g

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = SPI_HZ;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_SCK;
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_CS;
      cfg.pin_rst = PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};

Panel lcd;

float gyroBiasZ = 0.0f;
float twistDegrees = 0.0f;
uint32_t lastSampleUs = 0;

void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// Reads accel and gyro in one burst. The MPU latches all sensor registers
// together, so a single transaction is guaranteed to be one coherent moment.
bool readMotion(float* gz, float* ax, float* ay, float* az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

  int16_t raw[7];
  for (int i = 0; i < 7; ++i) {
    // Sequenced explicitly: the evaluation order of the operands of `|` is
    // unspecified in C++, so the compiler is free to swap these two bytes.
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    raw[i] = (int16_t)((high << 8) | low);
  }

  *ax = raw[0] / ACCEL_LSB_PER_G;
  *ay = raw[1] / ACCEL_LSB_PER_G;
  *az = raw[2] / ACCEL_LSB_PER_G;
  // raw[3] is temperature, unused.
  *gz = raw[6] / GYRO_LSB_PER_DPS;
  return true;
}

void calibrateGyro() {
  Serial.println("calibrating gyro — hold the device still...");
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("HOLD STILL", 120, 120);

  float sum = 0;
  const int samples = 200;
  for (int i = 0; i < samples; ++i) {
    float gz, ax, ay, az;
    if (readMotion(&gz, &ax, &ay, &az)) sum += gz;
    delay(5);
  }
  gyroBiasZ = sum / samples;
  Serial.printf("gyro Z bias: %.3f dps\n", gyroBiasZ);
}

void scanI2C() {
  Serial.println("\n--- I2C scan ---");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      ++found;
    }
  }
  if (found == 0) {
    Serial.println("  NOTHING FOUND — check SDA on D8, SCL on D9, and power");
  }
  Serial.println("--- scan done ---\n");
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) delay(10);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  scanI2C();

  const uint8_t who = readReg(REG_WHO_AM_I);
  Serial.printf("WHO_AM_I = 0x%02X (expect 0x68)\n", who);

  if (who != 0x68) {
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.drawString("NO IMU", 120, 110);
    lcd.setTextSize(1);
    lcd.drawString("check D8/D9 + power", 120, 140);
    Serial.println("IMU not responding — stopping.");
    while (true) delay(1000);
  }

  writeReg(REG_PWR_MGMT_1, 0x80);  // Reset.
  delay(100);
  writeReg(REG_PWR_MGMT_1, 0x01);  // Wake, clock from gyro X PLL.
  delay(10);
  writeReg(REG_SMPLRT_DIV, 0x04);   // 200 Hz.
  writeReg(REG_CONFIG, 0x03);       // DLPF 44 Hz.
  writeReg(REG_GYRO_CONFIG, 0x08);  // +/-500 dps.
  writeReg(REG_ACCEL_CONFIG, 0x00); // +/-2 g.
  delay(50);

  calibrateGyro();

  lcd.fillScreen(TFT_BLACK);
  lastSampleUs = micros();
  Serial.println("ready — twist the device");
}

void drawNeedle(float degrees, uint16_t colour) {
  const float rad = degrees * DEG_TO_RAD;
  const int x = 120 + (int)(88 * sinf(rad));
  const int y = 120 - (int)(88 * cosf(rad));
  lcd.drawLine(120, 120, x, y, colour);
  lcd.fillCircle(x, y, 6, colour);
}

void loop() {
  static float lastDrawn = 0;
  static uint32_t lastPrint = 0;
  static uint32_t shakeFlashUntil = 0;
  static int8_t shakeSign = 0;
  static uint8_t shakeHits = 0;
  static uint32_t shakeWindowStart = 0;

  float gz, ax, ay, az;
  if (!readMotion(&gz, &ax, &ay, &az)) return;

  const uint32_t now = micros();
  const float dt = (now - lastSampleUs) / 1000000.0f;
  lastSampleUs = now;
  if (dt <= 0 || dt > 0.2f) return;

  gz -= gyroBiasZ;

  // Deadband. Without it, residual bias slowly walks the needle while the
  // device sits untouched on the desk.
  if (fabsf(gz) > 4.0f) twistDegrees += gz * dt;

  // Shake: excursions away from 1 g that reverse sign several times quickly.
  const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  const float excursion = magnitude - 1.0f;
  if (shakeHits > 0 && millis() - shakeWindowStart > 600) {
    shakeHits = 0;
    shakeSign = 0;
  }
  if (fabsf(excursion) > 0.55f) {
    const int8_t sign = excursion > 0 ? 1 : -1;
    if (sign != shakeSign) {
      if (shakeHits == 0) shakeWindowStart = millis();
      shakeSign = sign;
      if (++shakeHits >= 3) {
        shakeHits = 0;
        shakeSign = 0;
        shakeFlashUntil = millis() + 400;
        Serial.println("SHAKE");
      }
    }
  }

  // Redraw only when the needle actually moved, erasing the old one rather
  // than clearing the screen — a full clear at this rate would flicker badly.
  if (fabsf(twistDegrees - lastDrawn) > 1.0f) {
    drawNeedle(lastDrawn, TFT_BLACK);
    drawNeedle(twistDegrees, TFT_CYAN);
    lastDrawn = twistDegrees;
  }

  const bool shaking = millis() < shakeFlashUntil;
  lcd.fillCircle(120, 120, 30, shaking ? TFT_ORANGE : TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.drawString(shaking ? "SHAKE" : String((int)twistDegrees).c_str(), 120, 120);

  if (millis() - lastPrint > 200) {
    lastPrint = millis();
    Serial.printf("gyroZ %7.1f dps | twist %8.1f deg | accel %.2f g\n",
                  gz, twistDegrees, magnitude);
  }
}
