#include "app.h"

#include <Arduino.h>

#include <algorithm>
#include <sys/time.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <time.h>

#include "app/desk_modes.h"
#include "app/mode_store.h"
#include "app/home_store.h"
#include "app/music_mode.h"
#include "app/settings_store.h"
#include "ble/ams_client.h"
#include "ble/knob_service.h"
#include "config.h"
#include "haptics/haptics.h"
#include "input/button.h"
#include "input/gestures.h"
#include "input/imu.h"
#include "net/wifi_fast.h"
#include "pins.h"
#include "pwm_compat.h"
#include "render/display.h"
#include "render/field.h"
#include "target/dirigera.h"
#include "target/target.h"

namespace app {
namespace {

constexpr uint8_t kBacklightOff = 0;

// Brightness and the dimmed level, both chosen in the app (percent).
uint8_t backlightActive() {
  return (uint8_t)(255 * settings_store::current().brightness / 100);
}
uint8_t backlightDim() {
  const knob::KnobSettings& s = settings_store::current();
  const int dim = max(3, 255 * s.dimPercent / 100);
  return (uint8_t)min<int>(dim, backlightActive());
}

// Frame pacing. A full frame takes ~25 ms to push over SPI, so 30 fps is close
// to the ceiling. Frames where nothing moved are skipped by the renderer.
constexpr uint32_t kFrameIntervalMs = 33;

// IMU draining. Faster than the frame rate because a detent crossing that lands
// between frames still needs its haptic tick on time. Nothing is lost at any
// rate: the samples queue in the IMU's FIFO until read.
constexpr uint32_t kImuIntervalMs = 5;
constexpr int kMaxSamplesPerPass = 32;

// Wake-on-motion sensitivity. Low enough that picking the device up always
// wakes it, high enough that a lorry going past does not.
constexpr uint16_t kWakeThresholdMg = 96;
constexpr uint8_t kWakeSamples = 5;
// After sleeping because it was carried (moving, but nobody using it), only a
// firm, sustained shake - or the button - wakes it: a backpack's bounce must not.
constexpr uint16_t kRestlessWakeThresholdMg = 450;
constexpr uint8_t kRestlessWakeSamples = 2;
// Turned off from the app (or a dying battery): only a real, firm shake wakes
// it - no button needed, and carrying it around will not.
// Measured on the knob (awake, with "waketest <mg> <samples>" over serial):
// 700 mg x2 woke on shaking but also on a few handling blips; 1200 mg x4
// missed a firm shake; 700 mg x3 then woke on a swing of the arm, and
// 950 mg x3 on the jolts of walking. A footstep is one short jolt, a shake
// keeps going, so the shake now has to last longer (x5, ~0.25 s at 20 Hz)
// rather than be harder. A false wake turns itself off again after a minute.
constexpr uint16_t kOffWakeThresholdMg = 950;
constexpr uint8_t kOffWakeSamples = 5;
RTC_DATA_ATTR bool g_offSleep = false;  // Woken from "off", not yet used.

// Moving but untouched this long: it is being carried, so it sleeps anyway.
constexpr uint32_t kCarriedSleepMs = 10UL * 60 * 1000;
// Woken from a carried sleep and still not used: back to sleep sooner.
constexpr uint32_t kCarriedAgainMs = 60UL * 1000;
RTC_DATA_ATTR bool g_carriedSleep = false;

// Rapid resets (a dying battery browning out, a crash loop) blink the screen
// faster and faster as the voltage sags. Counted across resets in memory that
// survives them; three in a row and the knob stays dark until the button.
RTC_NOINIT_ATTR uint32_t g_resetMagic;
RTC_NOINIT_ATTR uint32_t g_badResets;
constexpr uint32_t kResetMagic = 0x6B6E6F62;

Mode g_mode = Mode::Brightness;
knob::Mode g_knobMode = knob::Mode::Lights;
target::Registry g_registry;
dirigera::Hub g_hub;

uint32_t g_lastFrameMs = 0;
// A desk mode asked to be seen (the timer ran out): the screen lights as
// if the knob had been picked up.
uint32_t g_pokeMs = 0;
uint32_t g_lastInteractionMs = 0;  // A tap, the button, the app, a real twist.
int g_twistBurst = 0;
uint32_t g_twistBurstMs = 0;

void noteInteraction() {
  g_lastInteractionMs = millis();
  g_carriedSleep = false;
  g_offSleep = false;
}
// The knob's own motor shakes the IMU: taps and shakes are ignored until
// this time after a long buzz (a mode switch, the timer alarm).
uint32_t g_quietUntilMs = 0;
// After the next-lamp gesture the new lamp's name shows for a moment - an
// off lamp otherwise looks like the knob just went dark.
uint32_t g_lampNameUntilMs = 0;
// Twist carry for the sensitivity scale, and the lamp-stepping accumulator.
float g_twistCarry = 0.0f;
int g_lampTwist = 0;
constexpr int kStepsPerLamp = 30;

// Twisting changes hue rather than brightness (switched by the shake).
bool twistIsColour() { return g_mode == Mode::Colour; }

bool isDeskMode(knob::Mode m) {
  return m == knob::Mode::Timer || m == knob::Mode::Clock ||
         m == knob::Mode::Dice || m == knob::Mode::Ambient ||
         m == knob::Mode::Photo || m == knob::Mode::Notify ||
         m == knob::Mode::Games;
}
uint32_t g_lastImuMs = 0;
int g_pickerStartIndex = 0;

// ------------------------------------------------------------------------

#ifdef IMU_TRACE
// One CSV line per sample: t,ax,ay,az,gz,event. Recorded with a real hand
// doing real taps and shakes, this is what the gesture thresholds are tuned
// against.
void traceSample(const imu::Sample& s, const gestures::Events& e) {
  const char* event = e.shake ? "shake" : e.tap ? "tap" : "";
  Serial.printf("%lu,%.3f,%.3f,%.3f,%.1f,%s,%.1f,%.1f\n",
                (unsigned long)s.timestampMs, s.accelX, s.accelY, s.accelZ,
                s.gyroZ, event, s.gyroX, s.gyroY);
  static uint32_t lastDebugMs = 0;
  if (millis() - lastDebugMs > 1000) {
    lastDebugMs = millis();
    imu::traceDebug();
  }
}
#endif

field::State currentFieldState() {
  field::State s{};
  target::Target* t = g_registry.current();

  if (!t) {
    // No hub, or no lights on it: a small warm cluster, so the device still
    // looks alive rather than broken.
    s.value = 0.05f;
    s.hue = 30.0f;
    s.saturation = 0.0f;
    s.on = true;
    s.colourMode = false;
    return s;
  }

  s.value = t->level();
  s.hue = t->hue();
  s.saturation = t->saturation();
  s.on = t->isOn();
  s.colourMode = twistIsColour();
  return s;
}

// Haptic clicks are felt every 5% of the range rather than on each step. The
// steps are fine enough (DETENTS_FULL_SCALE) that the value is effectively
// continuous, and a pulse per step would blur into a buzz; a click every 5%
// keeps each one distinct while the value moves smoothly underneath.
constexpr float kHapticClickFraction = 0.05f;

bool crossedClick(float before, float after, float fullScale) {
  const float per = fullScale * kHapticClickFraction;
  return floorf(before / per) != floorf(after / per);
}

// Undo for twist a shake drags in. A sideways flick is a real rotation - the
// recorded ones turned the puck by over 100 degrees - and it happens before the
// shake can be recognised, so it lands as twist first. The value from before
// the current burst of twisting is kept, and a shake puts it back: the flick
// changes the mode and nothing else.
constexpr uint32_t kTwistBurstGapMs = 400;   // A pause this long ends a burst.
constexpr uint32_t kShakeUndoWindowMs = 1000;
uint32_t g_lastTwistMs = 0;
uint32_t g_burstStartMs = 0;
float g_burstLevel = 0.0f;
float g_burstHue = 0.0f;
float g_burstSaturation = 0.0f;

void noteTwistBurst(target::Target* t) {
  const uint32_t now = millis();
  if (now - g_lastTwistMs > kTwistBurstGapMs) {
    g_burstStartMs = now;
    g_burstLevel = t->level();
    g_burstHue = t->hue();
    g_burstSaturation = t->saturation();
  }
  g_lastTwistMs = now;
}

void undoShakeTwist(target::Target* t) {
  const uint32_t now = millis();
  if (now - g_lastTwistMs > kTwistBurstGapMs ||
      now - g_burstStartMs > kShakeUndoWindowMs) {
    return;  // No twist in flight, or a long deliberate one: leave it be.
  }
  if (twistIsColour()) {
    t->setColour(g_burstHue, g_burstSaturation);
  } else {
    t->setLevel(g_burstLevel);
  }
  g_lastTwistMs = 0;
}

void applyTwist(int8_t steps) {
  target::Target* t = g_registry.current();
  if (!t || steps == 0) return;

  // An off light is only woken by a tap. Twisting it used to count as "raise
  // the level", which turns a light on - so merely tilting the puck after
  // tapping the light off switched it straight back on.
  if (!t->isOn()) return;

  noteTwistBurst(t);

  if (twistIsColour()) {
    // A full sweep of DETENTS_FULL_SCALE steps covers the entire hue circle.
    const float degreesPerStep = 360.0f / DETENTS_FULL_SCALE;
    // Twisting hue implies wanting colour, so lift saturation off zero.
    const float saturation = max(0.65f, t->saturation());
    const float before = t->hue();
    const float after = before + steps * degreesPerStep;
    t->setColour(after, saturation);
    if (crossedClick(before, after, 360.0f)) {
      haptics::play(haptics::Pattern::Detent);
    }
    return;
  }

  const float before = t->level();
  const float after =
      constrain(before + steps * (1.0f / DETENTS_FULL_SCALE), 0.0f, 1.0f);

  t->setLevel(after);

  // At the end of travel the detent would otherwise keep ticking as though the
  // value were still changing, which feels broken. A distinct pattern says
  // "this is as far as it goes" without any on-screen message.
  if (after == before) {
    haptics::play(haptics::Pattern::Limit);
  } else if (crossedClick(before, after, 1.0f) || after == 0.0f ||
             after == 1.0f) {
    haptics::play(haptics::Pattern::Detent);
  }
}

void handleShake() {
  target::Target* t = g_registry.current();
  // Like twisting, switching what a twist adjusts waits for the light to be on.
  if (!t || !t->isOn()) return;

  // Undone before the mode flips, so it restores the quantity the flick
  // actually disturbed.
  undoShakeTwist(t);

  // Offering colour mode on a white bulb would be a control that silently does
  // nothing, so the shake is refused instead - with the limit pattern, which
  // already means "that direction is not available".
  if (!t->supportsColour()) {
    haptics::play(haptics::Pattern::Limit);
    return;
  }

  g_mode = (g_mode == Mode::Brightness) ? Mode::Colour : Mode::Brightness;
  gestures::resetTwist();
  haptics::play(haptics::Pattern::ModeChange);
}

// Screen fully dark for deep sleep: panel asleep and backlight held off.
//
// A deep-sleeping chip lets its pins float, and the panel module pulls its
// backlight line up - so a released pin leaves the screen lit all night.
// Hold it low through the sleep. The PWM has to be detached first: while the
// pin is still routed to the LEDC, writing it does nothing, and the LEDC stops
// in deep sleep and lets go of the line anyway. display::begin() releases the
// hold on the next boot.
void darkenForSleep() {
  display::setBacklight(kBacklightOff);
  display::lcd.sleep();
  pwmDetach(pins::LCD_BL);
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, LOW);
  gpio_hold_en((gpio_num_t)pins::LCD_BL);
  gpio_deep_sleep_hold_en();
}

// Deep sleep with the button as the only wake source, for when the IMU - and so
// wake-on-motion - is unavailable.
void sleepUntilButton() {
  darkenForSleep();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pins::BUTTON,
                                    ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void enterDeepSleep(bool carried = false, bool off = false) {
  g_carriedSleep = carried;
  g_offSleep = off;
  if (g_knobMode == knob::Mode::Music) music_mode::beforeSleep();
  if (isDeskMode(g_knobMode)) ams_client::shutdown();
  // Never sleep through the end of a timer: wake for it and ring.
  if (g_knobMode == knob::Mode::Timer) {
    const uint32_t s = timer_mode::wakeInSeconds();
    if (s > 0) esp_sleep_enable_timer_wakeup((uint64_t)s * 1000000ULL);
  }
  haptics::stop();
  darkenForSleep();

  imu::enableWakeOnMotion(off ? kOffWakeThresholdMg
                          : carried ? kRestlessWakeThresholdMg : kWakeThresholdMg,
                          off ? kOffWakeSamples
                          : carried ? kRestlessWakeSamples : kWakeSamples,
                          /*fast=*/off || carried);

  // Both wake sources are RTC pins, which is why the button and the IMU
  // interrupt claim two of the C3's three usable ones.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pins::IMU_INT,
                                    ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pins::BUTTON,
                                    ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}

// Automatic light sleep would let the CPU sleep between beacons while staying
// associated, which is what the original power plan assumed.
//
// The stock Arduino core cannot do it. Its precompiled sdkconfig has
// CONFIG_PM_ENABLE off for every chip, so esp_pm_configure() returns
// ESP_ERR_NOT_SUPPORTED and there is no runtime workaround - it is a build-time
// option of a binary we do not compile. The device falls back to staying awake
// at ~25 mA while in use, and relies on deep sleep plus the fast-reconnect path
// in net/wifi_fast for battery life.
//
// The call is still made, because it costs nothing and starts working the day
// this is built against a core that enables PM (an ESP-IDF build, or Arduino as
// an IDF component with a custom sdkconfig).
void enableAutoLightSleepIfSupported() {
#ifdef CONFIG_PM_ENABLE
  // The type was renamed when Arduino moved from IDF 4.x to 5.x.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_pm_config_t pm{};
#else
  esp_pm_config_esp32c3_t pm{};
#endif
  pm.max_freq_mhz = 160;
  pm.min_freq_mhz = 40;
  pm.light_sleep_enable = true;

  const esp_err_t result = esp_pm_configure(&pm);
  if (result != ESP_OK) {
    Serial.printf("light sleep refused: %s\n", esp_err_to_name(result));
  }
#else
  Serial.println(
      "light sleep unavailable: core built without CONFIG_PM_ENABLE");
#endif
}

// Draws the field while connecting, so the boot wait looks like the device
// doing something rather than like a hang.
void connectWithFeedback() {
  const uint32_t started = millis();
  constexpr uint32_t kTimeoutMs = 15000;

  // The connect call blocks, so frames are drawn either side of it rather than
  // during. A fast reconnect is quick enough that this is a single frame.
  field::draw(currentFieldState());
  display::tickBacklight();

  wifi_fast::connect(home_store::ssid(), home_store::password(), kTimeoutMs);

  while (WiFi.status() != WL_CONNECTED && millis() - started < kTimeoutMs) {
    field::draw(currentFieldState());
    display::tickBacklight();
    delay(kFrameIntervalMs);
  }
}

// Syncs the clock over NTP before any TLS connection to the hub.
//
// The ESP32 boots with its clock at the Unix epoch (1 Jan 1970) - there is no
// battery-backed RTC on this board. The hub's certificate is only valid from
// 2021 onward, so mbedTLS correctly refuses it as "not yet valid" if the
// handshake happens before the clock is set. That surfaces as a generic
// "signature check failed" error that gives no hint the real problem is the
// date, which is what makes this worth a comment.
//
// Any NTP server works; only getting a plausible date matters; the exact time
// does not - nothing in this firmware schedules by wall clock.
void syncTimeForTls() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  const uint32_t deadline = millis() + 5000;
  time_t now = time(nullptr);
  // Before NTP replies, time() reads back near zero (the epoch). Once it has
  // jumped past a date safely inside the certificate's validity window, the
  // sync is done.
  while (now < 1600000000 && millis() < deadline) {  // 1600000000 = Sep 2020.
    delay(50);
    now = time(nullptr);
  }

  if (now < 1600000000) {
    Serial.println("NTP sync failed - hub TLS handshake will likely fail");
  } else {
    Serial.printf("time synced: %s", ctime(&now));
  }
}

void updateIdleBehaviour() {
  const uint32_t still = min(gestures::stillForMs(), millis() - g_pokeMs);
  // Chosen in the app: when it dims, when the screen goes off (either can be
  // "never"), and when it sleeps - which still happens, so "never off"
  // cannot drain the battery overnight.
  const knob::KnobSettings& cfg = settings_store::current();
  const uint32_t offMs = cfg.screenOffSeconds ? cfg.screenOffSeconds * 1000UL : UINT32_MAX;
  const uint32_t dimMs = cfg.dimAfterSeconds ? cfg.dimAfterSeconds * 1000UL : UINT32_MAX;
  const uint32_t sleepMs = max(cfg.screenOffSeconds * 1000UL, cfg.sleepMinutes * 60000UL);

#ifndef IMU_TRACE  // A recording must not end with the device asleep.
  // Still: the usual timeouts. Moving but untouched for a long time (in a
  // bag): asleep regardless, or it never sleeps on a walk.
  const uint32_t untouched = millis() - g_lastInteractionMs;
  if (g_offSleep && untouched > kCarriedAgainMs) {
    Serial.println("sleep: woken from off but not used, off again");
    enterDeepSleep(/*carried=*/false, /*off=*/true);
  } else if (untouched > (g_carriedSleep ? kCarriedAgainMs : kCarriedSleepMs) &&
      !(g_knobMode == knob::Mode::Timer && timer_mode::wakeInSeconds() > 0 &&
        still < sleepMs)) {
    Serial.println("sleep: carried, not used");
    enterDeepSleep(/*carried=*/true);
  } else if (still > sleepMs) {
    enterDeepSleep();
  } else
#endif
  if (still > offMs) {
    display::setBacklightTarget(kBacklightOff);
  } else if (still > dimMs) {
    display::setBacklightTarget(backlightDim());
  } else {
    display::setBacklightTarget(backlightActive());
  }
}

// One Lights-mode action, as chosen for a gesture in the app.
void stepLamp(int dir);

void performLightsAction(knob::Action action) {
  switch (action) {
    case knob::Action::Toggle:
      if (target::Target* t = g_registry.current()) {
        t->toggle();
        haptics::play(haptics::Pattern::Confirm);
        Serial.printf("tap -> %s now %s\n", t->name(), t->isOn() ? "on" : "off");
      }
      break;
    case knob::Action::ColourMode:
      handleShake();
      Serial.printf("shake -> mode %s\n",
                    g_mode == Mode::Colour ? "COLOUR" : "BRIGHTNESS");
      break;
    case knob::Action::NextLight:
      stepLamp(1);
      break;
    default:
      break;
  }
}

// Moves to the next (or previous) lamp chosen for the gesture in the app (all,
// if none), and shows its name for a moment.
void stepLamp(int dir) {
  const int n = g_registry.count();
  const int before = g_registry.currentIndex();
  for (int k = 1; k < n; ++k) {
    const int i = ((before + dir * k) % n + n) % n;
    auto* light = static_cast<dirigera::Light*>(g_registry.at(i));
    if (!knob::lampInCycle(home_store::lampCycle(), light->id())) continue;
    g_registry.select(i);
    light->requestRefresh();
    g_lampNameUntilMs = millis() + 1500;
    haptics::play(haptics::Pattern::TargetSelect);
    Serial.printf("lamp -> %s\n", light->name());
    return;
  }
}

// Lights mode's gestures: the light picker on the button, tap to toggle,
// shake to swap brightness and colour, twist to adjust.
void handleLightsInput(const gestures::Events& gest, const button::Events& btn) {
  if (btn.holdBegan) {
    g_mode = Mode::Picker;
    g_pickerStartIndex = g_registry.currentIndex();
    gestures::resetTwist();
    haptics::play(haptics::Pattern::TargetSelect);
  }

  if (btn.holdEnded) {
    // Only announce a commit if the selection actually moved; confirming a
    // no-op would be noise.
    if (g_registry.currentIndex() != g_pickerStartIndex) {
      haptics::play(haptics::Pattern::Confirm);
    }
    g_mode = Mode::Brightness;
    gestures::resetTwist();
  }

  // Tap the case to toggle power. This replaces the physical button the
  // design originally called for: the accelerometer is already fitted and
  // already read every cycle, so a knock costs no extra part, no hole in the
  // enclosure, and no mechanical switch to wear out on a device whose whole
  // purpose is being handled.
  // The button always toggles; what a tap does is chosen in the app.
  if (btn.shortPress && g_mode != Mode::Picker) {
    performLightsAction(knob::Action::Toggle);
  }
  if (gest.tap && g_mode != Mode::Picker) {
    performLightsAction(settings_store::current().lightsTap);
  }

  if (gest.becameActive && display::backlightLevel() < backlightDim()) {
    haptics::play(haptics::Pattern::Wake);
  }
  // Picked up: the lamp may have changed while the knob lay still (the IKEA
  // app, a wall switch), so re-read it rather than show a stale level.
  if (gest.becameActive) {
    if (auto* light = static_cast<dirigera::Light*>(g_registry.current())) {
      light->requestRefresh();
    }
  }

  if (gest.shake && g_mode != Mode::Picker) {
    performLightsAction(settings_store::current().lightsShake);
  }

  if (gest.twistSteps != 0) {
    if (g_mode == Mode::Picker) {
      const int previous = g_registry.currentIndex();
      g_registry.step(gest.twistSteps);
      if (g_registry.currentIndex() != previous) {
        haptics::play(haptics::Pattern::TargetSelect);
      }
    } else {
      // Sensitivity scales the steps; the fraction carries to the next pass.
      static const float kScale[3] = {0.6f, 1.0f, 1.6f};
      const knob::KnobSettings& cfg = settings_store::current();
      g_twistCarry += gest.twistSteps * kScale[min<uint8_t>(cfg.twistSensitivity, 2)];
      const int steps = (int)g_twistCarry;
      g_twistCarry -= steps;
      switch (cfg.lightsTwist) {
        case knob::LightsTwist::None:
          break;
        case knob::LightsTwist::Lamps:
          if ((g_lampTwist > 0 && steps < 0) || (g_lampTwist < 0 && steps > 0)) g_lampTwist = 0;
          g_lampTwist += steps;
          if (abs(g_lampTwist) >= kStepsPerLamp) {
            stepLamp(g_lampTwist > 0 ? 1 : -1);
            g_lampTwist = 0;  // One lamp per turn.
          }
          break;
        default:  // Brightness, or colour after a shake.
          if (steps != 0) applyTwist((int8_t)constrain(steps, -127, 127));
          break;
      }
    }
  }
}

// The companion app switches modes by writing the Mode characteristic. The
// knob saves it and restarts, because the two modes start different radios
// (WiFi for the hub, or Bluetooth alone).
// Selects the hub light with this id. False when there is none (or no id).
bool selectLampById(const char* id) {
  if (!id || !id[0]) return false;
  for (int i = 0; i < g_registry.count(); ++i) {
    // Every registered target is a hub light in this firmware.
    auto* light = static_cast<dirigera::Light*>(g_registry.at(i));
    if (strcmp(light->id(), id) == 0) {
      g_registry.select(i);
      Serial.printf("selected [%d] %s (from app)\n", i, light->name());
      return true;
    }
  }
  return false;
}

// ---- WiFi scan for the app's network picker, and the knob's status.
bool g_scanning = false;
// WiFi and the hub are up. Once started they stay up until the knob sleeps,
// so leaving Lights and coming back is instant.
bool g_lightsStarted = false;
uint32_t g_wifiStartMs = 0;  // When startLights() began connecting.
uint32_t g_lastStatusMs = 0;

void startWifiScan() {
  if (g_scanning) return;
  if (!g_lightsStarted) {
    // WiFi is off outside Lights; bring the radio up just for the scan.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);  // Must stay on alongside Bluetooth (see begin()).
  }
  WiFi.scanNetworks(/*async=*/true);
  g_scanning = true;
  Serial.println("wifi: scanning for the app");
}

void pollWifiScan() {
  if (!g_scanning) return;
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  g_scanning = false;
  std::vector<knob::ScanEntry> nets;
  for (int i = 0; i < n; ++i) {
    nets.push_back({std::string(WiFi.SSID(i).c_str()), WiFi.RSSI(i),
                    WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  std::sort(nets.begin(), nets.end(),
            [](const knob::ScanEntry& a, const knob::ScanEntry& b) { return a.rssi > b.rssi; });
  knob_service::setScanResults(knob::encodeScanList(nets, 500));
  WiFi.scanDelete();
  if (!g_lightsStarted) WiFi.mode(WIFI_OFF);
  Serial.printf("wifi: scan found %d networks\n", n);
}

void publishStatus() {
  const uint32_t now = millis();
  if (now - g_lastStatusMs < 1000) return;
  g_lastStatusMs = now;
  knob::KnobStatus st;
  st.mode = (uint8_t)g_knobMode;
  st.ssid = home_store::ssid();
  if (g_lightsStarted) {
    const bool up = WiFi.status() == WL_CONNECTED;
    // "Failed" only once a connection has had a fair chance; a dropout
    // after that is the radio reconnecting by itself.
    static bool everUp = false;
    everUp |= up;
    st.wifi = up ? knob::WifiState::Connected
                 : (everUp || now - g_wifiStartMs < 20000) ? knob::WifiState::Connecting
                                                           : knob::WifiState::Failed;
    st.rssi = up ? (int8_t)WiFi.RSSI() : 0;
    st.hubConnected = g_hub.healthy();
  }
  knob_service::setStatus(st);
}

// WiFi, the hub and its lamps. Called at boot in Lights mode, or the first
// time the app switches to Lights - Bluetooth stays connected meanwhile.
bool g_hubReady = false;  // Lamps discovered and the worker running.

// Music and Photos move images of up to 24 KB, which WiFi and TLS leave no
// room for: they go off, and come back when Lights does.
void stopLights() {
  if (!g_lightsStarted) return;
  g_lightsStarted = false;
  g_hub.pause();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.printf("wifi: off (heap %u, largest %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
}

void startLights() {
  if (g_lightsStarted) return;
  g_lightsStarted = true;

  // Modem sleep: the radio powers down between DTIM beacons while associated.
  // Must stay on: with Bluetooth also running, the C3 shares one antenna
  // between the radios through modem sleep, and ESP-IDF aborts if WiFi sleep
  // is ever disabled.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  g_wifiStartMs = millis();
  connectWithFeedback();
  if (WiFi.status() == WL_CONNECTED) {
    home_store::markWifiGood();
  } else if (home_store::revertWifi()) {
    // New details from the app that do not work (a mistyped password) must
    // not strand the knob: back to the network that last worked.
    Serial.printf("wifi: could not join, back to %s\n", home_store::ssid());
    connectWithFeedback();
  }
  if (WiFi.status() != WL_CONNECTED) return;

  if (g_hubReady) {
    // Back from Music or Photos: the lamps are known; the worker reconnects
    // on the next request.
    g_hub.resume();
    if (target::Target* t = g_registry.current()) {
      static_cast<dirigera::Light*>(t)->requestRefresh();
    }
    Serial.println("wifi: back, hub resumed");
    return;
  }

  Serial.printf("wifi up in %lu ms (%s path)\n",
                (unsigned long)wifi_fast::lastConnectMs(),
                wifi_fast::usedFastPath() ? "fast" : "full");
  enableAutoLightSleepIfSupported();
  syncTimeForTls();

  if (!g_hub.begin()) {
    Serial.println("hub: CONNECTION FAILED");
    return;
  }
  Serial.println("hub: connected");
  const int found = g_hub.discoverLights(&g_registry);
  Serial.printf("hub: %d light(s) found\n", found);
  for (int i = 0; i < g_registry.count(); ++i) {
    target::Target* t = g_registry.at(i);
    Serial.printf("  [%d] %-20s on=%d level=%.2f colour=%s\n", i, t->name(),
                  t->isOn(), t->level(), t->supportsColour() ? "yes" : "no");
  }
  // Start on the lamp chosen in the app; failing that, a named light from
  // config.h rather than whichever happened to be first in the hub's reply.
  if (!selectLampById(home_store::lampId())) {
#ifdef DIRIGERA_PREFERRED_LIGHT
    for (int i = 0; i < g_registry.count(); ++i) {
      if (strstr(g_registry.at(i)->name(), DIRIGERA_PREFERRED_LIGHT)) {
        g_registry.select(i);
        Serial.printf("selected [%d] %s\n", i, g_registry.at(i)->name());
        break;
      }
    }
#endif
  }
  g_hub.startWorker();
  g_hubReady = true;
}

// Mode-specific setup, at boot and on every live switch.
void enterMode(knob::Mode m) {
  // Only Lights takes every bit of rotation; elsewhere a twist must be meant.
  gestures::setDeliberateTwist(m != knob::Mode::Lights);
  gestures::resetTwist();
  switch (m) {
    case knob::Mode::Music: music_mode::begin(); break;
    case knob::Mode::Timer: timer_mode::begin(); break;
    case knob::Mode::Clock: clock_mode::begin(); break;
    case knob::Mode::Dice: dice_mode::begin(); break;
    case knob::Mode::Ambient: ambient_mode::begin(); break;
    case knob::Mode::Photo: photo_mode::begin(); break;
    case knob::Mode::Notify: notify_mode::begin(); break;
    case knob::Mode::Games: games_mode::begin(); break;
    default:
      g_mode = Mode::Brightness;
      if (target::Target* t = g_registry.current()) {
        static_cast<dirigera::Light*>(t)->requestRefresh();
      }
      break;
  }
  field::invalidate();
}

// Switching modes without a restart: the Bluetooth link to the phone stays
// up, which is what made a switch take seconds (the iPhone noticing the knob
// had gone, then finding it again).
void switchMode(knob::Mode next) {
  if (next == g_knobMode) return;
  if (g_knobMode == knob::Mode::Music) music_mode::end();
  if (g_knobMode == knob::Mode::Photo) photo_mode::end();
  field::clearLabels();
  mode_store::save(next);
  Serial.printf("mode -> %s\n", knob::modeName(next));
  g_knobMode = next;
  if (next == knob::Mode::Lights) startLights();
  if (next == knob::Mode::Music || next == knob::Mode::Photo) stopLights();
  enterMode(next);
  knob_service::setMode((uint8_t)next);
  Serial.printf("heap: %u bytes free, largest block %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  haptics::play(haptics::Pattern::ModeChange);
  g_quietUntilMs = millis() + 500;
  g_pokeMs = millis();
  display::setBacklightTarget(backlightActive());
}

// The wake detector exactly as deep sleep sets it up, but awake: reports
// over serial whether (and when) motion would have woken the knob.
void printGravity() {
  for (int k = 0; k < 10; ++k) {
    float g[3];
    gestures::gravity(g);
    Serial.printf("grav x=%+.2f y=%+.2f z=%+.2f\n", g[0], g[1], g[2]);
    const uint32_t until = millis() + 300;
    while ((int32_t)(millis() - until) < 0) {
      imu::Sample s[16];
      int n;
      while ((n = imu::readAll(s, 16)) > 0)
        for (int i = 0; i < n; ++i) gestures::update(s[i]);
      delay(5);
    }
  }
}

void runWakeTest(int kind) {
  if (kind == 5) {
    printGravity();
    return;
  }
  const bool off = kind == 1;
  const uint16_t mg = kind == 4 ? mode_store::testMg() : off ? kOffWakeThresholdMg : kRestlessWakeThresholdMg;
  const uint8_t n = kind == 4 ? mode_store::testSamples() : off ? kOffWakeSamples : kRestlessWakeSamples;
  Serial.printf("waketest %u mg x%u: armed for 20 s\n", mg, n);
  imu::enableWakeOnMotion(mg, n, /*fast=*/true);
  const uint32_t start = millis();
  int hits = 0;
  while (millis() - start < 20000) {
    if (digitalRead(pins::IMU_INT) == HIGH) {
      Serial.printf("waketest: WOULD WAKE at %lu ms\n", (unsigned long)(millis() - start));
      ++hits;
      imu::clearInterrupt();
      delay(300);
    }
    delay(5);
  }
  Serial.printf("waketest: done, %d wake(s)\n", hits);
  imu::resumeFullPower();
}

void pollPhoneRequests() {
  publishStatus();
  notify_mode::poll();  // Every mode: the list is ready when Notifications opens.
  if (const int test = mode_store::takeWakeTest()) runWakeTest(test);
  pollWifiScan();

  knob::Command cmd;
  if (knob_service::takeCommand(&cmd)) {
    noteInteraction();
    if (cmd.type == knob::CommandType::PowerOff) {
      // Off from the app: dark, radios down, and only a firm shake (or the
      // button) wakes it - carrying it around will not.
      Serial.println("power: off from the app");
      haptics::play(haptics::Pattern::Limit);
      const uint32_t until = millis() + 300;
      while ((int32_t)(millis() - until) < 0) haptics::tick();
      enterDeepSleep(/*carried=*/false, /*off=*/true);
    }
    if (cmd.type == knob::CommandType::ScanWifi) {
      startWifiScan();
    } else if (cmd.type == knob::CommandType::SetTime) {
      wall::set(cmd.unixTime, cmd.utcOffsetMinutes);
    } else if (cmd.type == knob::CommandType::Timer &&
               g_knobMode == knob::Mode::Timer) {
      timer_mode::onCommand(cmd);
    } else if (cmd.type == knob::CommandType::Photo &&
               g_knobMode == knob::Mode::Photo) {
      photo_mode::onCommand(cmd);
    } else if (cmd.type == knob::CommandType::Dice &&
               g_knobMode == knob::Mode::Dice) {
      dice_mode::onCommand(cmd);
    }
  }

  knob::LampState lamp;
  if (knob_service::takeLampState(&lamp) && g_knobMode == knob::Mode::Lights) {
    for (int i = 0; i < g_registry.count(); ++i) {
      auto* light = static_cast<dirigera::Light*>(g_registry.at(i));
      if (lamp.lampId == light->id()) {
        light->applyRemoteState(lamp.on, lamp.level / 100.0f, lamp.hue,
                                lamp.saturation / 100.0f);
      }
    }
  }

  knob::HomeConfig home;
  if (knob_service::takeHomeConfig(&home)) {
    home_store::save(home);
    if (home.onlyLamp()) {
      // A new lamp needs no restart: the hub connection stays as it is.
      if (g_knobMode == knob::Mode::Lights && selectLampById(home.lampId.c_str())) {
        haptics::play(haptics::Pattern::TargetSelect);
      }
    } else {
      Serial.println("home: new WiFi / hub setup from app, restarting");
      Serial.flush();
      delay(100);
      ESP.restart();
    }
  }

  knob::KnobSettings settings;
  if (knob_service::takeSettings(&settings)) {
    noteInteraction();
    const bool remoteChanged = settings.mediaRemote != settings_store::current().mediaRemote;
    settings_store::save(settings);
    field::setLook(settings);
    if (remoteChanged) {
      // The remote's Bluetooth service can only be added at start-up.
      Serial.println("settings: media remote changed, restarting");
      Serial.flush();
      delay(100);
      ESP.restart();
    }
    music_mode::applySettings();
    gestures::setRatchet(settings.ratchet);
    Serial.printf("settings: screen off %us, sleep %umin, overlay %s %us\n",
                  settings.screenOffSeconds, settings.sleepMinutes,
                  settings.overlayOn ? "on" : "off", settings.overlaySeconds);
  }

  uint8_t mode;
  if (knob_service::takeModeRequest(&mode)) {
    noteInteraction();
    switchMode(knob::modeFromStored(mode));
  }
  knob::Mode fromSerial;
  if (mode_store::pollSerial(&fromSerial)) switchMode(fromSerial);
}

}  // namespace

// Called first thing at boot. A brownout (the battery dying) or a run of
// crashes keeps resetting the knob, and each boot flashed the backlight on:
// the faster and faster blinking at the end of a battery. Instead the knob
// stays dark and sleeps until the button - by which time it is charging.
void guardAgainstResetLoops() {
  const esp_reset_reason_t why = esp_reset_reason();
  const bool bad = why == ESP_RST_BROWNOUT || why == ESP_RST_PANIC ||
                   why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT ||
                   why == ESP_RST_WDT;
  if (g_resetMagic != kResetMagic) {
    g_resetMagic = kResetMagic;
    g_badResets = 0;
  }
  g_badResets = bad ? g_badResets + 1 : 0;
  if (why != ESP_RST_BROWNOUT && g_badResets < 3) return;

  g_badResets = 0;
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, LOW);
  gpio_hold_en((gpio_num_t)pins::LCD_BL);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pins::BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
  // A firm shake wakes it too, so no button is ever needed. If the battery
  // is still flat it browns out again and comes straight back here, dark.
  if (imu::begin()) {
    imu::enableWakeOnMotion(kOffWakeThresholdMg, kOffWakeSamples, /*fast=*/true);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << pins::IMU_INT, ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  g_offSleep = true;
  esp_deep_sleep_start();
}

void begin() {
  guardAgainstResetLoops();
  settings_store::load();
  home_store::load();
  display::begin();
  field::begin();
  {
    const knob::KnobSettings& look = settings_store::current();
    field::setLook(look);
  }
  haptics::begin();
  button::begin();

  if (!imu::begin()) {
    // Without the IMU there is no twist and no shake, which is the entire
    // interaction model. Fail loudly rather than booting into a dead knob.
    display::setBacklight(backlightActive());
    display::lcd.fillScreen(TFT_BLACK);
    display::lcd.setTextDatum(middle_center);
    display::lcd.setTextColor(TFT_RED);
    display::lcd.drawString("IMU", display::kWidth / 2, display::kHeight / 2);
    // Then sleep until the button is pressed, rather than retry on a loop. A
    // sensor that does not answer is almost always a wiring fault that no
    // number of restarts will fix, and restarting every few seconds kept the
    // CPU, backlight and USB running flat out - a whole battery overnight.
    // Asleep it draws microamps; a press reboots and tries the sensor again,
    // so once the wire is repaired one press brings the knob back.
    delay(3000);
    sleepUntilButton();
  }
  gestures::begin();
  gestures::setRatchet(settings_store::current().ratchet);

  g_knobMode = mode_store::load();
  Serial.printf("mode: %s\n", knob::modeName(g_knobMode));
  display::setBacklightTarget(backlightActive());

  // WiFi and TLS first when they are needed, so their memory is taken before
  // Bluetooth's and a shortfall shows up in the log, not as a failed
  // handshake.
  if (g_knobMode == knob::Mode::Lights) startLights();

  // Bluetooth runs in every mode, so the app can always reach the knob.
  ams_client::begin("Fidget Knob", (uint8_t)g_knobMode,
                    mode_store::loadCoverStyle());
  enterMode(g_knobMode);
  Serial.printf("heap: %u bytes free, largest block %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  haptics::play(haptics::Pattern::Wake);
  g_lastFrameMs = millis();
  g_lastImuMs = millis();
  // Woken by the button: that is someone using it.
  if (button::isDown() || (!g_carriedSleep && !g_offSleep)) noteInteraction();
}

void loop() {
  const uint32_t now = millis();

  haptics::tick();
  display::tickBacklight();

  // ----------------------------------------------------------- input ----
  if (now - g_lastImuMs >= kImuIntervalMs) {
    g_lastImuMs = now;

    // Every sample the IMU took since the last pass, not just the latest one:
    // a frame blocks for ~25 ms and a tap is over in ~10.
    gestures::Events gest{0, false, false, false, false};
    // Summed wide, then clamped: at 1 degree per step a fast spin across a
    // long backlog can cross more steps than the int8 in Events can hold.
    int twistSteps = 0;
    imu::Sample samples[kMaxSamplesPerPass];
    int n;
    do {
      n = imu::readAll(samples, kMaxSamplesPerPass);
      for (int i = 0; i < n; ++i) {
        const gestures::Events e = gestures::update(samples[i]);
        twistSteps += e.twistSteps;
        gest.shake |= e.shake;
        gest.tap |= e.tap;
        gest.becameStill |= e.becameStill;
        gest.becameActive |= e.becameActive;
#ifdef IMU_TRACE
        traceSample(samples[i], e);
#endif
      }
    } while (n == kMaxSamplesPerPass);
    if (settings_store::current().twistReverse) twistSteps = -twistSteps;
    gest.twistSteps = (int8_t)constrain(twistSteps, -127, 127);
    if ((int32_t)(now - g_quietUntilMs) < 0) {
      gest.tap = gest.shake = false;
      gest.twistSteps = 0;
    }

    const button::Events btn = button::update();
    // What counts as being used: a tap, the button, or a twist of a quarter
    // turn one way within a second. Jostling in a bag shakes and wobbles but
    // rarely does any of those.
    {
      if (gest.tap || btn.shortPress || btn.holdBegan) noteInteraction();
      if (gest.twistSteps != 0) {
        if (now - g_twistBurstMs > 1000 || (g_twistBurst > 0) != (gest.twistSteps > 0)) {
          g_twistBurst = 0;
          g_twistBurstMs = now;
        }
        g_twistBurst += gest.twistSteps;
        if (abs(g_twistBurst) >= 90) noteInteraction();
      }
    }

    if (g_knobMode == knob::Mode::Music || isDeskMode(g_knobMode)) {
      if (gest.becameActive && display::backlightLevel() < backlightDim()) {
        haptics::play(haptics::Pattern::Wake);
      }
      switch (g_knobMode) {
        case knob::Mode::Music: music_mode::onGestures(gest, btn); break;
        case knob::Mode::Timer: timer_mode::onGestures(gest, btn); break;
        case knob::Mode::Clock: clock_mode::onGestures(gest, btn); break;
        case knob::Mode::Ambient: ambient_mode::onGestures(gest, btn); break;
        case knob::Mode::Photo: photo_mode::onGestures(gest, btn); break;
        case knob::Mode::Notify: notify_mode::onGestures(gest, btn); break;
        case knob::Mode::Games: games_mode::onGestures(gest, btn); break;
        default: dice_mode::onGestures(gest, btn); break;
      }
    } else {
      handleLightsInput(gest, btn);
    }
  }

  if (g_knobMode == knob::Mode::Music || isDeskMode(g_knobMode)) {
    bool poke = false;
    switch (g_knobMode) {
      case knob::Mode::Music: music_mode::tick(); break;
      case knob::Mode::Timer: poke = timer_mode::tick(); break;
      case knob::Mode::Clock: poke = clock_mode::tick(); break;
      case knob::Mode::Ambient: poke = ambient_mode::tick(); break;
      case knob::Mode::Photo: poke = photo_mode::tick(); break;
      case knob::Mode::Notify: poke = notify_mode::tick(); break;
      case knob::Mode::Games: poke = games_mode::tick(); break;
      default: poke = dice_mode::tick(); break;
    }
    if (isDeskMode(g_knobMode)) ams_client::poll();
    if (poke) {
      g_pokeMs = now;
      g_quietUntilMs = now + 2500;  // The alarm buzz is long and strong.
      display::setBacklightTarget(backlightActive());
    }
    pollPhoneRequests();
    if (now - g_lastFrameMs >= kFrameIntervalMs) {
      g_lastFrameMs = now;
      updateIdleBehaviour();
    }
    delay(1);
    return;
  }

  // The hub link is lost for want of memory: only a clean boot (TLS before
  // Bluetooth) gets it back. Lights is the saved mode, so it boots straight
  // back here.
  if (g_hub.wantsRestart()) {
    Serial.println("hub: no memory for TLS, restarting");
    Serial.flush();
    delay(50);
    ESP.restart();
  }

  // --------------------------------------------------------- network ----
  // Hub writes go out on their own task (see Hub::startWorker); tick() is for
  // any future target that is cheap enough to service inline.
  for (int i = 0; i < g_registry.count(); ++i) {
    g_registry.at(i)->tick();
  }

  // ---------------------------------------------------------- render ----
  if (now - g_lastFrameMs >= kFrameIntervalMs) {
    g_lastFrameMs = now;

    // Nothing to look at with the backlight off, so skip the SPI traffic.
    if (display::backlightLevel() > 0) {
      if (g_mode == Mode::Picker || (int32_t)(now - g_lampNameUntilMs) < 0) {
        target::Target* t = g_registry.current();
        // ASCII only: the built-in font has no glyph for anything above 0x7F,
        // so a typographic dash would render as mojibake.
        field::drawPicker(t ? t->name() : "no lights",
                          g_registry.currentIndex(),
                          max(1, g_registry.count()), currentFieldState());
      } else {
        field::draw(currentFieldState());
      }
    }

    updateIdleBehaviour();
  }

  ams_client::poll();
  pollPhoneRequests();

  // Yielding rather than spinning is what allows automatic light sleep to
  // engage between frames.
  delay(1);
}

}  // namespace app
