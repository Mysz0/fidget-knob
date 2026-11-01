#pragma once

#include <Arduino.h>

// LEDC PWM shim.
//
// The ESP32 Arduino core changed the LEDC API incompatibly in 3.0: explicit
// channels were removed and ledcWrite() now addresses the pin directly. Which
// core PlatformIO resolves depends on the platform version pinned in
// platformio.ini, so both spellings have to work.
//
// The rest of the firmware uses the pin-based form, which is the one that
// survives.

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3

inline void pwmAttach(uint8_t pin, uint32_t frequency, uint8_t bits) {
  ledcAttach(pin, frequency, bits);
}

inline void pwmWrite(uint8_t pin, uint32_t duty) { ledcWrite(pin, duty); }

inline void pwmDetach(uint8_t pin) { ledcDetach(pin); }

#else

// Core 2.x needs a channel per pin. Channels are handed out on first attach.
inline uint8_t pwmChannelFor(uint8_t pin) {
  static uint8_t pins[8] = {0};
  static uint8_t used = 0;
  for (uint8_t i = 0; i < used; ++i) {
    if (pins[i] == pin) return i;
  }
  if (used < 8) {
    pins[used] = pin;
    return used++;
  }
  return 0;
}

inline void pwmAttach(uint8_t pin, uint32_t frequency, uint8_t bits) {
  const uint8_t channel = pwmChannelFor(pin);
  ledcSetup(channel, frequency, bits);
  ledcAttachPin(pin, channel);
}

inline void pwmWrite(uint8_t pin, uint32_t duty) {
  ledcWrite(pwmChannelFor(pin), duty);
}

inline void pwmDetach(uint8_t pin) { ledcDetachPin(pin); }

#endif
