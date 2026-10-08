#pragma once

#include <LovyanGFX.hpp>
#include "hondapp_config.h"

// Driver do ILI9341 e do backlight da LCDWiki ES3C28P.
// Mantém os parâmetros que já funcionam com o painel físico.
class HondappDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel;
  lgfx::Bus_SPI busSpi;
  lgfx::Light_PWM backlight;

public:
  HondappDisplay() {
    {
      auto config = busSpi.config();
      config.spi_host = SPI2_HOST;
      config.spi_mode = 0;
      config.freq_write = 60000000;
      config.freq_read = 16000000;
      config.pin_sclk = TFT_SCLK;
      config.pin_mosi = TFT_MOSI;
      config.pin_miso = TFT_MISO;
      config.pin_dc = TFT_DC;
      busSpi.config(config);
      panel.setBus(&busSpi);
    }
    {
      auto config = panel.config();
      config.pin_cs = TFT_CS;
      config.pin_rst = TFT_RST;
      config.pin_busy = -1;
      config.panel_width = 240;
      config.panel_height = 320;
      config.offset_x = 0;
      config.offset_y = 0;
      config.offset_rotation = 0;
      config.readable = true;
      config.invert = true;
      // Mantém a ordem RGB: a troca para BGR alterou toda a paleta planejada do painel.
      config.rgb_order = false;
      config.dlen_16bit = false;
      config.bus_shared = false;
      panel.config(config);
    }
    {
      auto config = backlight.config();
      config.pin_bl = TFT_BL;
      config.invert = false;
      config.freq = 44100;
      config.pwm_channel = 7;
      backlight.config(config);
      panel.setLight(&backlight);
    }
    setPanel(&panel);
  }
};
