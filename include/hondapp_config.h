#pragma once

#include <Arduino.h>

// Pinos confirmados da LCDWiki ES3C28P.
constexpr int TFT_MOSI = 11;
constexpr int TFT_MISO = 13;
constexpr int TFT_SCLK = 12;
constexpr int TFT_CS = 10;
constexpr int TFT_DC = 46;
constexpr int TFT_RST = -1;
constexpr int TFT_BL = 45;

constexpr int CTP_SDA = 16;
constexpr int CTP_SCL = 15;
constexpr uint8_t CTP_I2C_ADDR = 0x38;
constexpr uint8_t CODEC_ADDR = 0x18;

constexpr int I2S_BCLK = 5;
constexpr int I2S_LRCK = 7;
constexpr int I2S_DOUT = 8;
constexpr int I2S_DIN = 6;
constexpr int I2S_MCLK = 4;
constexpr int AUDIO_ENABLE = 1;

// Rede temporária de configuração exibida na tela da placa.
constexpr char HONDAPP_SETUP_SSID[] = "HondApp";
constexpr char HONDAPP_SETUP_PASSWORD[] = "HondApp2026";

enum ScreenState { STATE_BOOT, STATE_HUD, STATE_SETTINGS, STATE_FULLSCREEN_CAR };
enum BootLogoType { BOOT_HONDA = 0, BOOT_TYPE_R, BOOT_CUSTOM, BOOT_EXTRA1, BOOT_EXTRA2 };
enum ActiveTab { TAB_PERSONALIZACAO = 0, TAB_CONFIGURACOES, TAB_ATUALIZACAO };
enum VehicleDisplayMode { MODE_HOLOGRAMA = 0, MODE_FOTO };
enum TimeFormatMode { TIME_12H = 0, TIME_24H };
enum TempUnitMode { TEMP_CELSIUS = 0, TEMP_FAHRENHEIT };
enum AudioSourceMode { AUDIO_SRC_SIM = 0, AUDIO_SRC_MIC };

// Paleta RGB565 usada em todo o painel.
constexpr uint16_t C_MENU_BG = 0x0000;
constexpr uint16_t C_CARD_BG = 0x0000;
constexpr uint16_t C_BORDER_DARK = 0x2104;
constexpr uint16_t C_TEXT_WHITE = 0xFFFF;
constexpr uint16_t C_TEXT_MUTED = 0x7BEF;
constexpr uint16_t C_BTN_GRAY = 0x18C3;
constexpr uint16_t C_SILVER = 0xC618;
constexpr uint16_t C_ORANGE_BOOT = 0xFD00;
// Vermelho puro RGB565 (#FF0000) para Honda Type-R Red.
constexpr uint16_t C_RED_ACTIVE = 0xF800;
constexpr uint16_t C_AMBER_JDM = 0xFC00;
// Azul puro RGB565 (#0000FF) para Cyberpunk Cyan Blue.
constexpr uint16_t C_CYAN_BLUE = 0x001F;
constexpr uint16_t C_ACID_GREEN = 0x07E0;
constexpr uint16_t C_PURPLE_NEON = 0xA2BF;
constexpr uint16_t C_SPOON_AQUA = 0x073F;
