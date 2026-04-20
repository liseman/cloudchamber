#pragma once

#define LGFX_USE_V1

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "board_config.h"

class CrowPanelDisplay : public lgfx::LGFX_Device {
 public:
  CrowPanelDisplay() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = BoardConfig::kDisplaySclkPin;
      cfg.pin_mosi = BoardConfig::kDisplayMosiPin;
      cfg.pin_miso = -1;
      cfg.pin_dc = BoardConfig::kDisplayDcPin;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }

    {
      auto cfg = panel_.config();
      cfg.pin_cs = BoardConfig::kDisplayCsPin;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 3;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }

    {
      auto cfg = light_.config();
      cfg.pin_bl = BoardConfig::kBacklightPin;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }

    setPanel(&panel_);
  }

 private:
  lgfx::Panel_ILI9488 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
};
