#include "display.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "pins.h"
#include "pwm_compat.h"

namespace display {
namespace {

class Panel : public LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;  // The C3's only general-purpose SPI host.
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;  // 40 MHz. The GC9A01 tolerates 80 but the
                                  // ribbon on this module does not, reliably.
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = pins::LCD_SCK;
      cfg.pin_mosi = pins::LCD_MOSI;
      cfg.pin_miso = -1;  // Write-only.
      cfg.pin_dc = pins::LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = pins::LCD_CS;
      cfg.pin_rst = pins::LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = kWidth;
      cfg.panel_height = kHeight;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;  // GC9A01 panels ship inverted.
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};

Panel g_panel;

constexpr int kPwmFrequency = 5000;
constexpr int kPwmBits = 8;

uint8_t g_level = 0;
uint8_t g_target = 0;
uint32_t g_lastRampMs = 0;

// Milliseconds per unit of backlight change. Fast enough to feel responsive on
// wake, slow enough that dimming reads as intentional.
constexpr uint32_t kRampIntervalMs = 3;

}  // namespace

LGFX_Device& lcd = g_panel;

void begin() {
  // Released in case the last sleep held the backlight pin low (see
  // sleepUntilButton in app.cpp); a held pin ignores the PWM entirely.
  gpio_hold_dis((gpio_num_t)pins::LCD_BL);
  pwmAttach(pins::LCD_BL, kPwmFrequency, kPwmBits);
  pwmWrite(pins::LCD_BL, 0);

  g_panel.init();
  g_panel.setRotation(0);
  g_panel.fillScreen(TFT_BLACK);

  g_level = 0;
  g_target = 0;
  g_lastRampMs = millis();
}

void setBacklight(uint8_t level) {
  g_level = level;
  g_target = level;
  pwmWrite(pins::LCD_BL, level);
}

void setBacklightTarget(uint8_t level) { g_target = level; }

void tickBacklight() {
  if (g_level == g_target) return;

  const uint32_t now = millis();
  if (now - g_lastRampMs < kRampIntervalMs) return;
  g_lastRampMs = now;

  // Step size scales with distance so a large change does not crawl.
  const int distance = (int)g_target - (int)g_level;
  const int step = distance > 0 ? max(1, distance / 8) : min(-1, distance / 8);
  g_level = (uint8_t)constrain((int)g_level + step, 0, 255);

  pwmWrite(pins::LCD_BL, g_level);
}

uint8_t backlightLevel() { return g_level; }

}  // namespace display
