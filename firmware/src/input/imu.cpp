#include "imu.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace imu {
namespace {

constexpr uint8_t kAddress = 0x68;

// Register map (MPU-6000/6050 Register Map rev 4.2).
constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_FIFO_EN = 0x23;
constexpr uint8_t REG_MOT_THR = 0x1F;
constexpr uint8_t REG_MOT_DUR = 0x20;
constexpr uint8_t REG_INT_PIN_CFG = 0x37;
constexpr uint8_t REG_INT_ENABLE = 0x38;
constexpr uint8_t REG_INT_STATUS = 0x3A;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_MOT_DETECT_CTRL = 0x69;
constexpr uint8_t REG_USER_CTRL = 0x6A;
constexpr uint8_t REG_FIFO_COUNT_H = 0x72;
constexpr uint8_t REG_FIFO_R_W = 0x74;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_PWR_MGMT_2 = 0x6C;
constexpr uint8_t REG_WHO_AM_I = 0x75;

// Scale factors for the ranges selected in begin().
constexpr float kGyroLsbPerDps = 65.5f;    // +/-500 dps
// +/-8 g. At +/-2 g, gravity alone uses half the range on whichever axis
// points down, so a shake along that axis clipped at under 1 g one way and the
// shake detector - which needs a strong swing in both directions - never saw
// an up-and-down shake at all. Taps clipped too.
constexpr float kAccelLsbPerG = 4096.0f;

// MOT_THR is 32 mg per LSB.
constexpr uint8_t kMotionThresholdLsbPerMg = 32;

float g_gyroBiasZ = 0.0f;

// FIFO layout, in register order: accel X/Y/Z then gyro X/Y/Z, 12 bytes a
// sample (temperature left out). Gyro X/Y tell a twist - rotation about the
// screen axis - from a swinging arm, which rotates the puck about other axes.
// The 1 KB FIFO holds ~425 ms.
constexpr int kFifoSampleBytes = 12;
constexpr int kFifoSize = 1024;
constexpr uint32_t kSamplePeriodMs = 5;  // 200 Hz, set by SMPLRT_DIV.

// Wire's receive buffer is 128 bytes; stay under it in whole samples.
constexpr int kSamplesPerRead = 10;

uint32_t g_sampleClockMs = 0;
bool g_sampleClockValid = false;

void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(kAddress, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

// Reads the six accel and six gyro registers in one burst. Doing it as a single
// transaction matters: the MPU latches all sensor registers together, so a
// burst read is guaranteed to be one coherent moment in time.
bool readBurst(int16_t* accel, int16_t* gyro) {
  Wire.beginTransmission(kAddress);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  // 6 accel + 2 temperature + 6 gyro.
  if (Wire.requestFrom(kAddress, (uint8_t)14) != 14) return false;

  // The two reads are sequenced explicitly. Writing this as
  // `(Wire.read() << 8) | Wire.read()` looks natural but the order in which
  // the operands of `|` are evaluated is unspecified in C++, so the compiler
  // is free to swap the high and low bytes.
  for (int i = 0; i < 3; ++i) {
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    accel[i] = (int16_t)((high << 8) | low);
  }
  Wire.read();  // Temperature high byte, unused.
  Wire.read();  // Temperature low byte, unused.
  for (int i = 0; i < 3; ++i) {
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    gyro[i] = (int16_t)((high << 8) | low);
  }
  return true;
}

void startFifo() {
  writeReg(REG_USER_CTRL, 0x00);  // FIFO off.
  writeReg(REG_USER_CTRL, 0x04);  // FIFO_RESET, self-clearing.
  writeReg(REG_FIFO_EN, 0x78);    // ACCEL_FIFO_EN | XG | YG | ZG.
  writeReg(REG_USER_CTRL, 0x40);  // FIFO_EN.
  g_sampleClockValid = false;
}

uint16_t fifoCount() {
  Wire.beginTransmission(kAddress);
  Wire.write(REG_FIFO_COUNT_H);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(kAddress, (uint8_t)2) != 2) return 0;
  const uint8_t high = Wire.read();
  const uint8_t low = Wire.read();
  return (uint16_t)((high << 8) | low);
}

int16_t readWord() {
  const uint8_t high = Wire.read();
  const uint8_t low = Wire.read();
  return (int16_t)((high << 8) | low);
}

// Standard I2C bus recovery: up to nine clocks with SDA released, enough for
// any slave to finish shifting out the byte it was stuck in, then a STOP.
void recoverBus() {
  pinMode(pins::I2C_SDA, INPUT_PULLUP);
  pinMode(pins::I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(pins::I2C_SCL, HIGH);
  delayMicroseconds(5);
  for (int i = 0; i < 9 && digitalRead(pins::I2C_SDA) == LOW; ++i) {
    digitalWrite(pins::I2C_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(pins::I2C_SCL, HIGH);
    delayMicroseconds(5);
  }
  // STOP: SDA rises while SCL is high.
  pinMode(pins::I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(pins::I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(pins::I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(pins::I2C_SDA, HIGH);
  delayMicroseconds(5);
}

}  // namespace

bool begin() {
  // The ESP32 can reset in the middle of an I2C read - a firmware upload
  // does exactly that, and the FIFO is read continuously. The MPU runs on
  // regardless (it shares the battery, so nothing resets it) and keeps
  // driving SDA low partway through the byte it was sending, waiting for
  // clocks that never come. Every transaction then fails and the device
  // boots to the red "IMU" screen. Clocking the stuck byte out frees the bus.
  bool found = false;
  for (int attempt = 0; attempt < 5 && !found; ++attempt) {
    recoverBus();
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
    Wire.setClock(400000);
    found = readReg(REG_WHO_AM_I) == 0x68;
    if (!found) {
      Wire.end();
      delay(20);
    }
  }
  if (!found) {
    // Tells a stuck sensor from a broken wire. With the module unpowered
    // (VCC or GND off) its protection diodes drag both lines low; with SDA or
    // SCL off, that line floats or reads its own pull-up. Released and
    // pulled up, a healthy idle bus reads 1/1.
    Wire.end();
    pinMode(pins::I2C_SDA, INPUT_PULLUP);
    pinMode(pins::I2C_SCL, INPUT_PULLUP);
    delay(2);
    Serial.printf("imu: no answer; idle bus reads SDA=%d SCL=%d\n",
                  digitalRead(pins::I2C_SDA), digitalRead(pins::I2C_SCL));
    pinMode(pins::I2C_SDA, INPUT);
    pinMode(pins::I2C_SCL, INPUT);
    delay(2);
    Serial.printf("imu: without internal pull-ups SDA=%d SCL=%d\n",
                  digitalRead(pins::I2C_SDA), digitalRead(pins::I2C_SCL));
    return false;
  }

  writeReg(REG_PWR_MGMT_1, 0x80);  // Device reset.
  delay(100);
  writeReg(REG_PWR_MGMT_1, 0x01);  // Wake, clock from gyro X PLL (more stable
                                   // than the internal oscillator).
  delay(10);

  writeReg(REG_PWR_MGMT_2, 0x00);  // All axes on.
  writeReg(REG_SMPLRT_DIV, 0x04);  // 1 kHz / (1 + 4) = 200 Hz sample rate.

  // DLPF at 44 Hz. Fast enough to catch a shake, slow enough to reject the
  // vibration motor's own buzz - which matters, because the motor sits on the
  // same PCB stack and would otherwise feed its own detent pulses back in as
  // phantom shakes.
  writeReg(REG_CONFIG, 0x03);

  writeReg(REG_GYRO_CONFIG, 0x08);   // +/-500 dps.
  writeReg(REG_ACCEL_CONFIG, 0x10);  // +/-8 g, high-pass filter off.

  delay(50);
  calibrateGyroBias();
  startFifo();
  return true;
}

int readAll(Sample* out, int maxSamples) {
  const uint16_t count = fifoCount();

  // A full FIFO has overflowed and dropped bytes from the front, so it no
  // longer starts on a sample boundary. Nothing in it can be trusted to parse;
  // start over. This happens after anything that blocks for over 640 ms, such
  // as connecting to WiFi, and costs nothing but the stale samples.
  if (count >= kFifoSize - kFifoSampleBytes || count % kFifoSampleBytes != 0) {
    startFifo();
    return 0;
  }

  const int available = count / kFifoSampleBytes;
  const int n = min(available, maxSamples);
  if (n == 0) return 0;

  // The newest sample in the FIFO was taken about now, and each older one a
  // period earlier. Advancing a private clock by exactly one period per sample
  // keeps timestamps evenly spaced, and it is re-anchored to millis() only if
  // it drifts far, since the MPU's oscillator is not quite the ESP32's.
  const uint32_t now = millis();
  const uint32_t firstIdeal = now - (uint32_t)(available - 1) * kSamplePeriodMs;
  const int32_t drift =
      (int32_t)(g_sampleClockMs + kSamplePeriodMs - firstIdeal);
  if (!g_sampleClockValid || drift > 50 || drift < -50) {
    g_sampleClockMs = firstIdeal - kSamplePeriodMs;
    g_sampleClockValid = true;
  }

  int done = 0;
  while (done < n) {
    const int chunk = min(kSamplesPerRead, n - done);
    const uint8_t bytes = (uint8_t)(chunk * kFifoSampleBytes);

    Wire.beginTransmission(kAddress);
    Wire.write(REG_FIFO_R_W);
    if (Wire.endTransmission(false) != 0) break;
    if (Wire.requestFrom(kAddress, bytes) != bytes) {
      // A short read leaves the FIFO mid-sample. Resynchronise.
      startFifo();
      break;
    }

    for (int i = 0; i < chunk; ++i) {
      // Sequenced explicitly for the same reason as readBurst().
      const int16_t ax = readWord();
      const int16_t ay = readWord();
      const int16_t az = readWord();
      const int16_t gx = readWord();
      const int16_t gy = readWord();
      const int16_t gz = readWord();

      g_sampleClockMs += kSamplePeriodMs;
      Sample& s = out[done + i];
      s.timestampMs = g_sampleClockMs;
      s.accelX = ax / kAccelLsbPerG;
      s.accelY = ay / kAccelLsbPerG;
      s.accelZ = az / kAccelLsbPerG;
      s.gyroZ = (gz / kGyroLsbPerDps) - g_gyroBiasZ;
      // Uncalibrated: only used to judge which axis a rotation is about,
      // where a few degrees per second of bias do not matter.
      s.gyroX = gx / kGyroLsbPerDps;
      s.gyroY = gy / kGyroLsbPerDps;
    }
    done += chunk;
  }
  return done;
}

bool calibrateGyroBias() {
  constexpr int kSamples = 64;
  // A resting MPU-6050's gyro noise spans well under a degree per second. A
  // wider spread means the device was moving, and averaging motion into the
  // bias makes every later reading wrong by the speed it was turning at.
  constexpr float kMaxSpreadDps = 2.5f;

  float sum = 0.0f;
  float lo = 1e9f, hi = -1e9f;
  int got = 0;

  for (int i = 0; i < kSamples; ++i) {
    int16_t accel[3], gyro[3];
    if (readBurst(accel, gyro)) {
      const float dps = gyro[2] / kGyroLsbPerDps;
      sum += dps;
      lo = min(lo, dps);
      hi = max(hi, dps);
      ++got;
    }
    delay(3);
  }

  if (got < kSamples / 2 || hi - lo > kMaxSpreadDps) return false;
  g_gyroBiasZ = sum / got;
  return true;
}

void adjustGyroBias(float residualDps) { g_gyroBiasZ += residualDps; }

void enableWakeOnMotion(uint16_t thresholdMg, uint8_t samples, bool fast) {
  // The order here is prescribed by the register map's motion-detection
  // sequence; the high-pass filter must be enabled before the threshold is set
  // or the detector compares against absolute acceleration and gravity alone
  // trips it.
  writeReg(REG_PWR_MGMT_1, 0x01);
  writeReg(REG_ACCEL_CONFIG, 0x01);  // +/-2 g with the 5 Hz high-pass filter on.

  writeReg(REG_MOT_THR, (uint8_t)min<uint16_t>(255, thresholdMg / kMotionThresholdLsbPerMg));
  writeReg(REG_MOT_DUR, samples);

  // Accelerometer power-on delay of 3 ms, and hold the detector's decrement
  // rate at maximum so brief motion still latches.
  writeReg(REG_MOT_DETECT_CTRL, 0x15);

  // Active high, push-pull, latched until cleared. Latching is essential: a
  // pulsed interrupt could fire and clear while the ESP32 is still entering
  // sleep, and the device would never wake.
  writeReg(REG_INT_PIN_CFG, 0x20);
  writeReg(REG_INT_ENABLE, 0x40);  // MOT_EN.

  clearInterrupt();

  // Gyro off, accelerometer cycling at 5 Hz. This is what drops the IMU to
  // tens of microamps while it keeps watch.
  // LP_WAKE_CTRL = 5 Hz (or 20 Hz), gyro axes disabled.
  writeReg(REG_PWR_MGMT_2, fast ? 0x87 : 0x47);
  writeReg(REG_PWR_MGMT_1, 0x21);  // CYCLE = 1.
}

void resumeFullPower() {
  writeReg(REG_PWR_MGMT_1, 0x01);
  writeReg(REG_PWR_MGMT_2, 0x00);
  writeReg(REG_INT_ENABLE, 0x00);
  writeReg(REG_ACCEL_CONFIG, 0x10);  // Back to +/-8 g, high-pass filter off.
  clearInterrupt();
  delay(10);
  calibrateGyroBias();
  startFifo();
}

void clearInterrupt() { (void)readReg(REG_INT_STATUS); }

#ifdef IMU_TRACE
void traceDebug() {
  int16_t accel[3], gyro[3];
  readBurst(accel, gyro);
  Serial.printf("D,bias=%.2f,burst_gz=%d(%.1fdps),burst_az=%d,fifo_count=%u\n",
                g_gyroBiasZ, gyro[2], gyro[2] / kGyroLsbPerDps, accel[2],
                fifoCount());
}
#endif

}  // namespace imu
