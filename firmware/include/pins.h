#pragma once

// Configured pin allocation for the Seeed Xiao ESP32-C3.
// Confirmed LCD wiring: docs/wiring.md. Physical MPU wiring is still to be
// checked; the IMU constants below describe software configuration only.
//
// Board silkscreen (Dn) vs actual GPIO, since they do not match and the
// difference has bitten every person who has ever wired one of these:
//
//   D0=GPIO2  D1=GPIO3  D2=GPIO4  D3=GPIO5  D4=GPIO6  D5=GPIO7
//   D6=GPIO21 D7=GPIO20 D8=GPIO8  D9=GPIO9  D10=GPIO10
//
// Two hard constraints shape this map:
//
// STRAPPING PINS. GPIO2, GPIO8 and GPIO9 are sampled at reset and must read
// HIGH, or the chip boots into download mode instead of running firmware. Only
// signals that idle high may live there:
//   GPIO2 -> button    (internal pull-up, idles high; pressed = low)
//   GPIO8 -> I2C SDA   (the IMU module's pull-ups hold it high)
//   GPIO9 -> I2C SCL   (same, plus the board's own pull-up)
// Anything that idles low - an SPI clock, a motor gate, a backlight - would
// hold the pin low at reset and break booting. This is why the LCD clock
// cannot stay on D8, which is where it is natural to put it.
//
// DEEP-SLEEP WAKE. Only GPIO0..GPIO5 can wake the C3 from deep sleep, and
// GPIO0/GPIO1 are not broken out on the Xiao. That leaves four usable pins, and
// the two wake sources claim two of them:
//   GPIO2 -> button
//   GPIO3 -> IMU motion interrupt

namespace pins {

// UART0 PINS. GPIO20 and GPIO21 are UART0 RX/TX. Selecting USB CDC for Serial
// does not free them: the ESP-IDF console underneath Arduino still claims
// UART0, and taking a pin back through the GPIO matrix is a fight a driver does
// not always win. So nothing timing-critical goes there. Only two signals
// tolerate it, and both are placed deliberately:
//   GPIO21 -> LCD CS  (idles high, same as UART TX; and CS latches nothing
//                      without a clock, so the boot log toggling it is inert)
//   GPIO20 -> motor   (UART RX is an INPUT at boot, so the transistor base
//                      floats and the motor stays off until firmware drives it)

// --- Display (GC9A01 over SPI) ---
constexpr int LCD_CS   = 21;  // D6
constexpr int LCD_DC   = 6;   // D4
constexpr int LCD_RST  = 7;   // D5
constexpr int LCD_MOSI = 10;  // D10, labelled DIN on the panel
constexpr int LCD_SCK  = 4;   // D2, labelled CLK. An ordinary GPIO with no
                              // alternate function competing for it - the
                              // clock is the one signal that must not be lost.
constexpr int LCD_BL   = 5;   // D3, LEDC PWM for dimming.

// --- Input ---
constexpr int BUTTON  = 2;   // D0. Active low, internal pull-up. Wake capable.
constexpr int IMU_INT = 3;   // D1. MPU-6050 motion interrupt. Wake capable.
constexpr int I2C_SDA = 8;   // D8
constexpr int I2C_SCL = 9;   // D9

// --- Haptics ---
// Idles low (transistor off), so it must not be a strapping pin. GPIO20 is safe
// for the reason given above.
constexpr int MOTOR = 20;    // D7

}  // namespace pins
