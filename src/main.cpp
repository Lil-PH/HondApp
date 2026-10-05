/*
   HONDAPP - DASHBOARD CYBERPUNK (AMOLED BLACK & MIC SENSITIVITY ADJUSTED)
   - Preto AMOLED absoluto e otimizado
   - Sensibilidade média calibrada para o microfone I2S
   - Hardware: ESP32-S3 2.8" Capacitive Touch (ILI9341 + CTP I2C 0x38 + Codec I2S 0x18)
*/

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "settings_store.h"
#include "hondapp_config.h"
#include "hondapp_display.h"
#include "driver/i2s.h"


// Hardware, cores e tipos compartilhados ficam em hondapp_config.h.

// Forward declarations
static void setAmplifierEnabled(bool enabled);
static bool writeCodecRegister(uint8_t reg, uint8_t value);
static bool initCodec();
static bool startMicrophone();
static void recoverMicrophone();
static bool initAudioI2S();
static void playWelcomeVoice();
static void requestRedraw();
bool getTouch(uint16_t *x, uint16_t *y);
void applyManualTime();
void configureWiFiAndClock();
void startWiFiReconfiguration();
void startWiFiPortal();
void updateOnlineWeather();
void maintainOnlineServices();
void checkForFirmwareUpdate(bool force = false);
void performOTAUpdate();
void updateAudioAnimation();
void loadSavedSettings();
void saveSettings();
void drawOfficialHondaSvg(int x, int y, uint16_t color);
void renderBootScreen();
void drawCarSilhouette(int ox, int oy, float scale);
void drawHUDCard(int x, int y, int w, int h, const char* titleLeft, const char* titleRight);
void drawTopMenuTab();
void renderDriverCard();
void drawTemperatureValue(int x, int y, float value, TempUnitMode unit);
void drawWiFiStatusIcon(int centerX, int centerY, bool connected);
void renderClimateCard();
void renderVehicleCard();
void renderAudioCard();
void renderFullscreenCarScreen();
void renderSettingsScreen();
void handleTouchEvents();
static int getSettingsMaxScroll();

HondappDisplay tft;
LGFX_Sprite canvas(&tft);

// ============================================================
// ESTADOS E CONTROLES DO SISTEMA
// ============================================================

ScreenState currentState = STATE_BOOT;


BootLogoType bootMode = BOOT_HONDA;


ActiveTab activeTab = TAB_PERSONALIZACAO;


VehicleDisplayMode vehicleMode = MODE_HOLOGRAMA;


TimeFormatMode timeFormat = TIME_12H;


TempUnitMode tempUnit = TEMP_CELSIUS;


AudioSourceMode audioSource = AUDIO_SRC_SIM;

uint16_t currentThemeHex = C_ACID_GREEN;
uint16_t tempSelectedHex = C_ACID_GREEN;
uint8_t screenBrightness = 255;

int scrollY = 0;
int maxScrollY = 610;
int scrollStartVal = 0;
bool touchPressed = false;
uint16_t startTouchX = 0, startTouchY = 0;
uint16_t lastTouchY = 0;
bool hasDragged = false;
bool longPressTriggered = false;
bool holdingCarCard = false;
bool menuButtonVisible = true;

unsigned long bootStartTime = 0;
unsigned long touchStartTime = 0;

int rpm = 850;
int speedKmh = 0;
float insideTemp = 23.8;
float outsideTemp = 23.0;
float audioBars[14];
float targetBars[14];

int manualHour = 12;
int manualMinute = 0;

// Hora pela internet e temperatura externa para Serra, ES.
static WiFiManager wifiManager;
static bool networkReady = false;
static bool wifiConnecting = false;
static bool wifiPortalActive = false;
static bool weatherRequestInProgress = false;
// Impede que um segundo toque inicie outro download enquanto o primeiro ainda usa a rede.
static bool otaRunning = false;
static unsigned long wifiConnectStartedAt = 0;
static String configuredNetworkName = "Nenhuma rede salva";
static String wifiNotice = "";
static unsigned long wifiNoticeUntil = 0;
static unsigned long lastWeatherUpdate = 0;
static unsigned long lastWeatherAttempt = 0;

// O arquivo OTA é o asset puro publicado no Release do GitHub, nunca o ZIP do Actions.
static const char OTA_FIRMWARE_URL[] = "https://github.com/Lil-PH/HondApp/releases/latest/download/main.cpp.bin";
static const char OTA_RELEASE_API_URL[] = "https://api.github.com/repos/Lil-PH/HondApp/releases/latest";
static const char HONDAPP_FIRMWARE_VERSION[] = "1.0.0";
static bool otaCheckComplete = false;
static bool otaUpdateAvailable = false;
static String otaLatestVersion = "";
static String otaStatus = "AGUARDANDO WI-FI";
static unsigned long lastOtaCheckAt = 0;

// A interface nao precisa ser redesenhada continuamente: este sinal pede um novo
// quadro somente quando um toque, uma conexao ou uma opcao mudou algo visivel.
static bool displayNeedsRedraw = true;
static unsigned long lastRenderAt = 0;
static ScreenState lastRenderedState = static_cast<ScreenState>(-1);

// A aba responde no instante do toque; o conteúdo completo é composto no próximo ciclo.
// Isso evita a sensação de que o toque ficou preso enquanto o canvas é redesenhado.
static bool tabContentPending = false;
static unsigned long tabContentReadyAt = 0;

// O cartão só aparece quando a API do GitHub confirma que há uma versão mais nova.
// O teste das Configurações usa o mesmo cartão, mas desaparece sozinho após seis segundos.
static bool testUpdateNoticeVisible = false;
static unsigned long testUpdateNoticeUntil = 0;

static const float WEATHER_LATITUDE = -20.21f;
static const float WEATHER_LONGITUDE = -40.30f;

// Variáveis de controle do Microfone I2S (Sensibilidade Média Ajustada)
static bool i2sReady = false;
static uint32_t noiseFloor = 0;
static uint8_t calibrationReads = 0;
static uint32_t filteredMicLevel = 0;
static uint8_t micReadMisses = 0;
static unsigned long lastMicRecoveryAttempt = 0;

// Preferências do painel ficam na memória não volátil da ESP32.
// Elas permanecem mesmo depois de desligar ou reiniciar a placa.
static void requestRedraw() {
  displayNeedsRedraw = true;
}

// Cada aba para exatamente no primeiro e no último item; não há repetição da lista.
static int getSettingsMaxScroll() {
  // Leva os três cartões acima da linha fixa dos botões, deixando uma margem visual.
  if (activeTab == TAB_PERSONALIZACAO) return 257;
  // Faz o ajuste manual de hora parar com uma folga maior antes da barra fixa de fechar e salvar.
  if (activeTab == TAB_CONFIGURACOES) return 250;
  return 0;
}

void loadSavedSettings() {
  DashboardSettings settings{};
  loadDashboardSettings(settings);
  currentThemeHex = settings.theme;
  screenBrightness = settings.brightness;
  bootMode = (BootLogoType)settings.bootMode;
  vehicleMode = (VehicleDisplayMode)settings.vehicleMode;
  timeFormat = (TimeFormatMode)settings.timeFormat;
  tempUnit = (TempUnitMode)settings.tempUnit;
  menuButtonVisible = settings.menuButtonVisible;
  // O visualizador sempre inicia em SIM; MIC é apenas uma escolha temporária desta sessão.
  audioSource = AUDIO_SRC_SIM;

  // Não aceita um valor gravado corrompido que poderia deixar a tela ilegível.
  if (screenBrightness < 10) screenBrightness = 10;
  if (bootMode > BOOT_CUSTOM) bootMode = BOOT_HONDA;
  if (vehicleMode > MODE_FOTO_GIF) vehicleMode = MODE_HOLOGRAMA;
  if (timeFormat > TIME_24H) timeFormat = TIME_12H;
  if (tempUnit > TEMP_FAHRENHEIT) tempUnit = TEMP_CELSIUS;
  tempSelectedHex = currentThemeHex;
}

void saveSettings() {
  DashboardSettings settings{};
  settings.theme = currentThemeHex;
  settings.brightness = screenBrightness;
  settings.bootMode = (uint8_t)bootMode;
  settings.vehicleMode = (uint8_t)vehicleMode;
  settings.timeFormat = (uint8_t)timeFormat;
  settings.tempUnit = (uint8_t)tempUnit;
  settings.menuButtonVisible = menuButtonVisible;
  saveDashboardSettings(settings);
}

// ============================================================
// FUNÇÕES DE ÁUDIO / CODEC I2S
// ============================================================
static void setAmplifierEnabled(bool enabled) {
  digitalWrite(AUDIO_ENABLE, enabled ? LOW : HIGH);
}

static bool writeCodecRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(CODEC_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool initCodec() {
  Wire.beginTransmission(CODEC_ADDR);
  if (Wire.endTransmission() != 0) return false;
  const uint8_t sequence[][2] = {
    {0x00, 0x1F}, {0x45, 0x00}, {0x01, 0x30}, {0x02, 0x90}, {0x03, 0x19}, {0x16, 0x03}, {0x04, 0x19}, {0x05, 0x00},
    {0x06, 0x0F}, {0x07, 0x01}, {0x08, 0xFF}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0xC0},
    {0x0D, 0x01}, {0x01, 0x3F}, {0x14, 0x1A}, {0x12, 0x00}, {0x13, 0x10}, {0x09, 0x0C}, {0x0A, 0x0C}, {0x0E, 0x02},
    {0x0F, 0x44}, {0x15, 0x00}, {0x1B, 0x05}, {0x1C, 0x65}, {0x17, 0xBF}, {0x37, 0x08}, {0x32, 0xBF}, {0x44, 0x00}
  };
  for (const auto &entry : sequence) {
    if (!writeCodecRegister(entry[0], entry[1])) return false;
    delay(1);
  }
  delay(20); // Dá tempo para o codec estabilizar antes da primeira leitura.
  return true;
}

static bool startMicrophone() {
  setAmplifierEnabled(false);
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (initCodec() && initAudioI2S()) {
      noiseFloor = 0;
      calibrationReads = 0;
      filteredMicLevel = 0;
      micReadMisses = 0;
      return true;
    }
    delay(30);
  }
  return false;
}

static void recoverMicrophone() {
  if (millis() - lastMicRecoveryAttempt < 1000) return;
  lastMicRecoveryAttempt = millis();
  if (i2sReady) {
    i2s_zero_dma_buffer(I2S_NUM_0);
  }
  startMicrophone();
}

static bool initAudioI2S() {
  if (i2sReady) return true;
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = 16000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 6;
  config.dma_buf_len = 128;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  
  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRCK;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_DIN;
  pins.mck_io_num = I2S_MCLK;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2sReady = true;
  return true;
}

// ============================================================
// TOUCH CAPACITIVO I2C (0x38) - ROTAÇÃO 3
// ============================================================
bool getTouch(uint16_t *x, uint16_t *y) {
  Wire.beginTransmission(CTP_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((int)CTP_I2C_ADDR, 5);
  if (Wire.available() < 5) return false;

  uint8_t touch_num = Wire.read();
  uint8_t x_msb = Wire.read();
  uint8_t x_lsb = Wire.read();
  uint8_t y_msb = Wire.read();
  uint8_t y_lsb = Wire.read();

  if ((touch_num & 0x0F) == 0) return false;

  uint16_t raw_x = ((x_msb & 0x0F) << 8) | x_lsb;
  uint16_t raw_y = ((y_msb & 0x0F) << 8) | y_lsb;

  int calc_x = raw_y;
  int calc_y = 240 - raw_x;

  if (calc_x < 0) calc_x = 0;
  if (calc_x >= 320) calc_x = 319;
  if (calc_y < 0) calc_y = 0;
  if (calc_y >= 240) calc_y = 239;

  *x = (uint16_t)calc_x;
  *y = (uint16_t)calc_y;

  return true;
}

void applyManualTime() {
  time_t now = time(nullptr);
  struct tm *tmNow = localtime(&now);
  
  tmNow->tm_hour = manualHour;
  tmNow->tm_min = manualMinute;
  tmNow->tm_sec = 0;

  time_t t = mktime(tmNow);
  struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
  settimeofday(&tv, NULL);
}

void startWiFiPortal() {
  if (wifiPortalActive) return;

  // O portal nao bloqueia a interface: o loop principal atende a pagina e continua desenhando o painel.
  WiFi.mode(WIFI_AP_STA);
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.startConfigPortal("HONDAPP-SETUP");
  wifiPortalActive = true;
  wifiConnecting = false;
  configuredNetworkName = "Aguardando configuracao";
}

void configureWiFiAndClock() {
  if (wifiConnecting || wifiPortalActive) return;
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  wifiConnectStartedAt = millis();
  wifiConnecting = true;
  configuredNetworkName = "Conectando...";
}

void startWiFiReconfiguration() {
  // A remocao acontece antes do portal abrir, portanto a rede antiga nao pode ser reutilizada.
  networkReady = false;
  wifiConnecting = false;
  configuredNetworkName = "Nenhuma rede salva";
  WiFi.disconnect(true, true);
  wifiManager.resetSettings();
  wifiNotice = "REDE REMOVIDA - CONECTE EM HONDAPP-SETUP";
  wifiNoticeUntil = millis() + 6000;
  startWiFiPortal();
}

void updateOnlineWeather() {
  if (WiFi.status() != WL_CONNECTED || weatherRequestInProgress) {
    networkReady = false;
    return;
  }

  weatherRequestInProgress = true;
  lastWeatherAttempt = millis();
  // Buffers TLS pequenos evitam que a consulta de clima concorra com a memória do painel.
  WiFiClientSecure weatherClient;
  weatherClient.setInsecure();
  HTTPClient http;
  const String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LATITUDE, 2) +
                     "&longitude=" + String(WEATHER_LONGITUDE, 2) +
                     "&current=temperature_2m";
  http.setTimeout(6000);
  if (http.begin(weatherClient, url)) {
    const int responseCode = http.GET();
    if (responseCode == HTTP_CODE_OK) {
      const String response = http.getString();
      const int currentPos = response.indexOf("\"current\"");
      const int valuePos = currentPos >= 0 ? response.indexOf("\"temperature_2m\":", currentPos) : -1;
      if (valuePos >= 0) {
        const int start = valuePos + strlen("\"temperature_2m\":");
        const int end = response.indexOf(',', start);
        const float onlineTemperature = response.substring(start, end >= 0 ? end : response.length()).toFloat();
        // A API entrega a temperatura do ar em graus Celsius para a localização escolhida.
        if (onlineTemperature > -80.0f && onlineTemperature < 80.0f) {
          outsideTemp = onlineTemperature;
          lastWeatherUpdate = millis();
        }
      }
    }
    http.end();
  }
  weatherRequestInProgress = false;
}

static int compareVersions(String left, String right) {
  left.trim(); right.trim();
  if (left.startsWith("v") || left.startsWith("V")) left.remove(0, 1);
  if (right.startsWith("v") || right.startsWith("V")) right.remove(0, 1);
  for (int part = 0; part < 3; ++part) {
    const int leftDot = left.indexOf('.');
    const int rightDot = right.indexOf('.');
    const int leftValue = (leftDot < 0 ? left : left.substring(0, leftDot)).toInt();
    const int rightValue = (rightDot < 0 ? right : right.substring(0, rightDot)).toInt();
    if (leftValue != rightValue) return leftValue > rightValue ? 1 : -1;
    left = leftDot < 0 ? "0" : left.substring(leftDot + 1);
    right = rightDot < 0 ? "0" : right.substring(rightDot + 1);
  }
  return 0;
}

void checkForFirmwareUpdate(bool force) {
  if (WiFi.status() != WL_CONNECTED || otaRunning) return;
  if (!force && (otaCheckComplete || millis() - lastOtaCheckAt < 600000UL)) return;

  lastOtaCheckAt = millis();
  otaStatus = "CONSULTANDO GITHUB...";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  http.setUserAgent("HondApp-ESP32-OTA/1.0");
  if (!http.begin(client, OTA_RELEASE_API_URL)) {
    otaStatus = "NAO FOI POSSIVEL CONSULTAR";
    otaCheckComplete = true;
    requestRedraw();
    return;
  }
  const int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    const String body = http.getString();
    const int key = body.indexOf("\"tag_name\":");
    const int firstQuote = key >= 0 ? body.indexOf('"', key + 11) : -1;
    const int secondQuote = firstQuote >= 0 ? body.indexOf('"', firstQuote + 1) : -1;
    if (firstQuote >= 0 && secondQuote > firstQuote) {
      otaLatestVersion = body.substring(firstQuote + 1, secondQuote);
      otaUpdateAvailable = compareVersions(otaLatestVersion, HONDAPP_FIRMWARE_VERSION) > 0;
      otaStatus = otaUpdateAvailable ? "NOVA VERSAO DISPONIVEL" : "SISTEMA ATUALIZADO";
      testUpdateNoticeVisible = otaUpdateAvailable;
      // A notificação real permanece enquanto houver um Release mais novo; somente a prévia expira.
      if (otaUpdateAvailable) testUpdateNoticeUntil = 0;
    } else {
      otaStatus = "RELEASE SEM VERSAO VALIDA";
    }
  } else if (httpCode == HTTP_CODE_NOT_FOUND) {
    otaStatus = "NENHUM RELEASE PUBLICADO";
  } else {
    otaStatus = "GITHUB HTTP " + String(httpCode);
  }
  http.end();
  otaCheckComplete = true;
  requestRedraw();
}

void maintainOnlineServices() {
  // WiFiManager precisa ser atendido continuamente enquanto o celular usa 192.168.4.1.
  if (wifiPortalActive) wifiManager.process();

  const bool wasNetworkReady = networkReady;
  const bool nowConnected = WiFi.status() == WL_CONNECTED;
  if (nowConnected && !networkReady) {
    // Este e o unico ponto que confirma a rede: so mostra o nome depois de obter IP na rede escolhida.
    configuredNetworkName = WiFi.SSID();
    if (configuredNetworkName.length() == 0) configuredNetworkName = "Conectada (nome indisponivel)";
    wifiNotice = "WI-FI CONECTADO";
    wifiNoticeUntil = millis() + 3000;
  }
  networkReady = nowConnected;
  if (networkReady != wasNetworkReady) requestRedraw();

  if (networkReady) {
    wifiConnecting = false;
    if (wifiPortalActive) {
      wifiManager.stopConfigPortal();
      wifiPortalActive = false;
    }
    static bool clockConfigured = false;
    if (!clockConfigured) {
      configTzTime("BRT3", "pool.ntp.org", "time.nist.gov");
      clockConfigured = true;
      updateOnlineWeather();
    }
    checkForFirmwareUpdate();
  } else if (wifiConnecting && millis() - wifiConnectStartedAt >= 12000UL) {
    startWiFiPortal();
  }

  // Em caso de falha da internet, espera um minuto antes de tentar de novo; nao cria dezenas de sockets.
  if (networkReady && millis() - lastWeatherUpdate >= 600000UL && millis() - lastWeatherAttempt >= 60000UL) {
    updateOnlineWeather(); // Atualiza a temperatura externa a cada 10 minutos.
    requestRedraw();
  }

  // Remove avisos vencidos sem deixar a mensagem congelada na tela de configuracao.
  if (wifiNoticeUntil != 0 && millis() >= wifiNoticeUntil) {
    wifiNoticeUntil = 0;
    requestRedraw();
  }
}

// Tela direta usada durante a atualização: o canvas normal fica parado enquanto o .bin é gravado.
static void drawOTAStatus(const char* line1, const char* line2, int percent = -1) {
  tft.fillScreen(C_MENU_BG);
  tft.drawRoundRect(12, 16, 296, 208, 8, currentThemeHex);
  tft.setTextColor(C_TEXT_WHITE, C_MENU_BG);
  tft.setTextSize(1);
  tft.setCursor(28, 42); tft.print("HONDAPP // ATUALIZACAO OTA");
  tft.setTextColor(currentThemeHex, C_MENU_BG);
  tft.setCursor(28, 86); tft.print(line1);
  tft.setTextColor(C_TEXT_MUTED, C_MENU_BG);
  tft.setCursor(28, 106); tft.print(line2);
  if (percent >= 0) {
    if (percent > 100) percent = 100;
    tft.drawRoundRect(28, 140, 264, 14, 6, C_BORDER_DARK);
    tft.fillRoundRect(30, 142, ((260 * percent) / 100), 10, 5, currentThemeHex);
    char progress[12];
    sprintf(progress, "%d%%", percent);
    tft.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    tft.setCursor(145, 166); tft.print(progress);
  }
}

void performOTAUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiNotice = "CONECTE O WI-FI ANTES DE ATUALIZAR";
    wifiNoticeUntil = millis() + 4000;
    requestRedraw();
    return;
  }

  drawOTAStatus("CONECTANDO AO WI-FI...", "VERIFICANDO O ARQUIVO NO GITHUB");
  WiFiClientSecure otaClient;
  // O asset de um Release do GitHub passa por mais de um host HTTPS. O cliente
  // sem validação de certificado permite acompanhar essa troca de host no ESP32.
  otaClient.setInsecure();
  otaClient.setHandshakeTimeout(20);
  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(15000);
  http.setReuse(false);
  http.useHTTP10(true);
  // /releases/latest/download redireciona para o servidor que entrega o asset.
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("HondApp-ESP32-OTA/1.0");
  if (!http.begin(otaClient, OTA_FIRMWARE_URL)) {
    drawOTAStatus("ERRO DE CONEXAO", "URL OTA INVALIDA");
    delay(2500);
    requestRedraw();
    return;
  }

  int httpCode = HTTP_CODE_SERVICE_UNAVAILABLE;
  // Uma única conexão por toque evita deixar várias tentativas TLS competindo
  // pela RAM interna. O usuário pode tocar novamente se a internet estiver fora.
  drawOTAStatus("CONECTANDO AO GITHUB...", "VERIFICANDO ATUALIZACAO");
  httpCode = http.GET();
  const int contentLength = http.getSize();
  if (httpCode != HTTP_CODE_OK || contentLength <= 0) {
    const String httpError = http.errorToString(httpCode);
    http.end();
    char detail[42];
    if (httpCode < 0) {
      snprintf(detail, sizeof(detail), "HTTPS FALHOU: %s", httpError.c_str());
    } else {
      snprintf(detail, sizeof(detail), "GITHUB HTTP %d: %s", httpCode, httpError.c_str());
    }
    drawOTAStatus("ATUALIZACAO INDISPONIVEL", detail);
    delay(3500);
    requestRedraw();
    return;
  }

  drawOTAStatus("BAIXANDO ATUALIZACAO...", "ATUALIZANDO SISTEMA - NAO DESLIGUE", 0);
  if (!Update.begin(contentLength, U_FLASH)) {
    http.end();
    drawOTAStatus("ERRO AO PREPARAR", "ESPACO OTA INDISPONIVEL");
    delay(3000);
    requestRedraw();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  size_t totalWritten = 0;
  while (http.connected() && totalWritten < (size_t)contentLength) {
    const size_t available = stream->available();
    if (available) {
      const size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
      const size_t readBytes = stream->readBytes(buffer, toRead);
      if (readBytes == 0 || Update.write(buffer, readBytes) != readBytes) break;
      totalWritten += readBytes;
      const int percent = (int)((totalWritten * 100UL) / contentLength);
      drawOTAStatus("BAIXANDO ATUALIZACAO...", "ATUALIZANDO SISTEMA - NAO DESLIGUE", percent);
    }
    delay(1);
  }
  http.end();

  if (totalWritten == (size_t)contentLength && Update.end(true)) {
    drawOTAStatus("ATUALIZACAO CONCLUIDA", "REINICIANDO HONDAPP...", 100);
    delay(1200);
    ESP.restart();
  }

  const uint8_t updateError = Update.getError();
  Update.abort();
  char detail[42];
  snprintf(detail, sizeof(detail), "BIN INVALIDO/INCOMPLETO (ERRO %u)", updateError);
  drawOTAStatus("ERRO NA ATUALIZACAO", detail);
  delay(3500);
  requestRedraw();
}

void updateAudioAnimation() {
  if (audioSource == AUDIO_SRC_SIM) {
    for (int i = 0; i < 14; i++) {
      if (random(100) < 35) {
        targetBars[i] = random(15, 100) / 100.0f;
      }
      audioBars[i] += (targetBars[i] - audioBars[i]) * 0.35f;
    }
  } else {
    // Modo MIC Ativo: sensibilidade levemente maior, mantendo margem contra saturação.
    if (i2sReady) {
      int16_t samples[64];
      size_t received = 0;
      if (i2s_read(I2S_NUM_0, samples, sizeof(samples), &received, pdMS_TO_TICKS(20)) == ESP_OK && received > 0) {
        micReadMisses = 0;
        size_t count = received / sizeof(int16_t);
        uint64_t sumSquares = 0;
        for (size_t i = 0; i < count; ++i) {
          int32_t sample = samples[i];
          sumSquares += (uint64_t)(sample * sample);
        }
        uint32_t rms = count ? (uint32_t)sqrt((double)sumSquares / count) : 0;
        if (calibrationReads < 8) {
          noiseFloor = (noiseFloor * calibrationReads + rms) / (calibrationReads + 1);
          calibrationReads++;
        }
        uint32_t signal = rms > noiseFloor ? rms - noiseFloor : 0;
        // Ganho levemente maior: sons mais baixos passam a mover as barras sem saturar com facilidade.
        uint32_t target = (signal * 100UL) / 2800UL;
        if (target > 100) target = 100;
        filteredMicLevel = (filteredMicLevel * 3 + target * 2) / 5;
        
        float overallFactor = (float)filteredMicLevel / 100.0f;
        for (int i = 0; i < 14; i++) {
          float noiseMod = (random(75, 125) / 100.0f);
          float barVal = overallFactor * noiseMod;
          if (barVal > 1.0f) barVal = 1.0f;
          audioBars[i] += (barVal - audioBars[i]) * 0.4f;
        }
      } else {
        micReadMisses++;
        if (micReadMisses >= 5) {
          recoverMicrophone();
          micReadMisses = 0;
        }
      }
    }
  }
}

// ============================================================
// LOGO OFICIAL DO SVG
// ============================================================
void drawOfficialHondaSvg(int x, int y, uint16_t color) {
  for (int i = 0; i < 4; i++) {
    canvas.drawRoundRect(x + i, y + i, 100 - (i * 2), 82 - (i * 2), 18 - i, color);
  }

  canvas.fillTriangle(x + 22, y + 16,  x + 36, y + 16,  x + 29, y + 66, color);
  canvas.fillTriangle(x + 29, y + 66,  x + 36, y + 16,  x + 40, y + 66, color);

  canvas.fillTriangle(x + 64, y + 16,  x + 78, y + 16,  x + 60, y + 66, color);
  canvas.fillTriangle(x + 60, y + 66,  x + 78, y + 16,  x + 71, y + 66, color);

  canvas.fillRect(x + 34, y + 38, 32, 12, color);
  canvas.fillCircle(x + 50, y + 49, 14, 0x0000);
  canvas.fillTriangle(x + 36, y + 42, x + 64, y + 42, x + 50, y + 48, color);
}

// ============================================================
// BOOT SCREEN (FUNDO 0x0000 AMOLED)
// ============================================================
void renderBootScreen() {
  canvas.fillScreen(0x0000);

  uint16_t logoColor = C_RED_ACTIVE;
  if (bootMode == BOOT_HONDA) logoColor = C_SILVER;
  else if (bootMode == BOOT_TYPE_R) logoColor = C_RED_ACTIVE;
  else logoColor = currentThemeHex;

  drawOfficialHondaSvg(110, 24, logoColor);

  unsigned long elapsed = millis() - bootStartTime;
  int pct = map(elapsed, 0, 3000, 0, 100);
  if (pct > 100) pct = 100;

  int progressW = map(pct, 0, 100, 0, 260);

  canvas.drawRect(30, 160, 260, 8, C_BORDER_DARK);
  canvas.fillRect(30, 160, progressW, 8, currentThemeHex);

  canvas.setTextColor(C_TEXT_WHITE, 0x0000);
  canvas.setTextSize(1);
  canvas.setCursor(30, 180);
  canvas.print("CARREGANDO...");

  char pctBuf[16];
  sprintf(pctBuf, "%d%%", pct);
  canvas.setCursor(260, 180);
  canvas.print(pctBuf);
}

// ============================================================
// SILHUETA DO VEÍCULO (VETOR)
// ============================================================
void drawCarSilhouette(int ox, int oy, float scale) {
  canvas.fillGradientRect(ox + (10 * scale), oy + (52 * scale), 125 * scale, 4 * scale, (uint16_t)0x03E0, currentThemeHex);

  canvas.drawLine(ox + (5 * scale), oy + (36 * scale), ox + (20 * scale), oy + (25 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (20 * scale), oy + (25 * scale), ox + (45 * scale), oy + (19 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (45 * scale), oy + (19 * scale), ox + (58 * scale), oy + (6 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (58 * scale), oy + (6 * scale), ox + (88 * scale), oy + (6 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (88 * scale), oy + (6 * scale), ox + (108 * scale), oy + (22 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (108 * scale), oy + (22 * scale), ox + (128 * scale), oy + (26 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (128 * scale), oy + (26 * scale), ox + (137 * scale), oy + (36 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (137 * scale), oy + (36 * scale), ox + (130 * scale), oy + (44 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (130 * scale), oy + (44 * scale), ox + (10 * scale), oy + (44 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (10 * scale), oy + (44 * scale), ox + (5 * scale), oy + (36 * scale), C_TEXT_WHITE);

  canvas.drawLine(ox + (58 * scale), oy + (7 * scale), ox + (68 * scale), oy + (20 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (68 * scale), oy + (20 * scale), ox + (88 * scale), oy + (20 * scale), C_TEXT_WHITE);
  canvas.drawLine(ox + (88 * scale), oy + (20 * scale), ox + (86 * scale), oy + (7 * scale), C_TEXT_WHITE);

  canvas.fillRect(ox + (126 * scale), oy + (28 * scale), 8 * scale, 6 * scale, currentThemeHex);

  canvas.drawCircle(ox + (30 * scale), oy + (44 * scale), 8 * scale, C_TEXT_WHITE);
  canvas.drawCircle(ox + (30 * scale), oy + (44 * scale), 3 * scale, C_TEXT_MUTED);
  canvas.drawCircle(ox + (105 * scale), oy + (44 * scale), 8 * scale, C_TEXT_WHITE);
  canvas.drawCircle(ox + (105 * scale), oy + (44 * scale), 3 * scale, C_TEXT_MUTED);
}

// ============================================================
// DASHBOARD HUD
// ============================================================
void drawHUDCard(int x, int y, int w, int h, const char* titleLeft, const char* titleRight) {
  canvas.fillRoundRect(x, y, w, h, 6, C_CARD_BG);
  canvas.drawRoundRect(x, y, w, h, 6, C_BORDER_DARK);

  canvas.drawFastHLine(x + 2, y, 10, currentThemeHex);
  canvas.drawFastVLine(x, y + 2, 8, currentThemeHex);
  canvas.drawFastHLine(x + w - 12, y, 10, currentThemeHex);
  canvas.drawFastVLine(x + w - 1, y + 2, 8, currentThemeHex);
  canvas.drawFastHLine(x + 2, y + h - 1, 10, currentThemeHex);
  canvas.drawFastVLine(x, y + h - 9, 8, currentThemeHex);
  canvas.drawFastHLine(x + w - 12, y + h - 1, 10, currentThemeHex);
  canvas.drawFastVLine(x + w - 1, y + h - 9, 8, currentThemeHex);

  canvas.setTextSize(1);
  if (titleLeft) {
    canvas.setTextColor(currentThemeHex, C_CARD_BG);
    canvas.setCursor(x + 8, y + 6);
    canvas.print(titleLeft);
  }
}

void drawTopMenuTab() {
  int x = 125, y = 0, w = 70, h = 14;
  canvas.fillRoundRect(x, y, w, h, 4, C_CARD_BG);
  canvas.drawRoundRect(x, y, w, h, 4, currentThemeHex);
  canvas.fillCircle(x + 10, y + 7, 2, currentThemeHex);

  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 18, y + 3);
  canvas.print("MENU  v");
}

// Aviso sobre o painel: só é exibido após a confirmação de um Release mais novo.
// Um toque no cartão abre diretamente a aba Atualização.
void drawTestUpdateNotice() {
  if (testUpdateNoticeUntil != 0 && millis() >= testUpdateNoticeUntil) {
    testUpdateNoticeVisible = false;
    testUpdateNoticeUntil = 0;
  }
  if (!testUpdateNoticeVisible) return;

  // Caixa um pouco mais alta: a segunda linha fica afastada da moldura vermelha.
  const int x = 154, y = 18, w = 158, h = 42;
  canvas.fillRoundRect(x, y, w, h, 5, C_CARD_BG);
  canvas.drawRoundRect(x, y, w, h, 5, C_RED_ACTIVE);
  canvas.fillCircle(x + 12, y + 12, 4, C_RED_ACTIVE);
  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 22, y + 8);
  canvas.print("NOVA ATUALIZACAO");
  canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
  canvas.setCursor(x + 22, y + 26);
  canvas.print("TOQUE PARA ATUALIZAR");
}

void renderDriverCard() {
  // Quatro cartões com a mesma área: 158 × 117 px, separados por uma folga de 2 px.
  int x = 1, y = 2, w = 158, h = 117;
  drawHUDCard(x, y, w, h, "CIVIC", NULL);

  time_t now = time(nullptr);
  struct tm *tmNow = localtime(&now);

  char timeStr[12], secStr[4], dateStr[32];
  if (timeFormat == TIME_12H) {
    int h12 = tmNow->tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (tmNow->tm_hour >= 12) ? "PM" : "AM";
    sprintf(timeStr, "%02d:%02d", h12, tmNow->tm_min);
    sprintf(secStr, "%s", ampm);
  } else {
    sprintf(timeStr, "%02d:%02d", tmNow->tm_hour, tmNow->tm_min);
    sprintf(secStr, "%02d", tmNow->tm_sec);
  }

  const char *weekDays[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  const char *months[]   = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

  sprintf(dateStr, "%s %d %s %d", weekDays[tmNow->tm_wday], tmNow->tm_mday, months[tmNow->tm_mon], tmNow->tm_year + 1900);

  canvas.setTextColor(currentThemeHex, C_CARD_BG);
  canvas.setTextSize(1);
  int labelX = x + (w - (11 * 6)) / 2;
  canvas.setCursor(labelX, y + 28);
  canvas.print("TIME / DATE");

  int timeWidth = strlen(timeStr) * 18;
  int timeX = x + (w - timeWidth) / 2 - 8;
  if (timeX < x + 4) timeX = x + 4;

  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  canvas.setTextSize(3);
  canvas.setCursor(timeX, y + 42);
  canvas.print(timeStr);

  canvas.setTextColor(currentThemeHex, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(timeX + timeWidth + 3, y + 50);
  canvas.print(secStr);

  canvas.drawFastHLine(x + 12, y + 72, 131, C_BORDER_DARK);
  canvas.fillCircle(x + 78, y + 72, 3, currentThemeHex);

  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  int dateX = x + (w - strlen(dateStr) * 6) / 2;
  if (dateX < x + 4) dateX = x + 4;
  // A data fica mais afastada da linha e do marcador central para leitura mais limpa.
  canvas.setCursor(dateX, y + 84);
  canvas.print(dateStr);
}

// Grau e unidade desenhados como vetor: ficam legíveis mesmo sem o glifo ° na fonte.
void drawTemperatureValue(int x, int y, float value, TempUnitMode unit) {
  char valueText[8];
  sprintf(valueText, "%4.1f", value);

  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  canvas.setTextSize(2);
  canvas.setCursor(x, y);
  canvas.print(valueText);

  // Pequena bolinha de grau, seguida da unidade C ou F.
  const int degreeX = x + 53;
  const int degreeY = y + 3;
  canvas.drawCircle(degreeX, degreeY, 3, C_TEXT_WHITE);
  canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(degreeX + 6, y + 5);
  canvas.print(unit == TEMP_CELSIUS ? "C" : "F");
}

// Ícone vetorial de Wi-Fi, desenhado no próprio painel para não depender de texto.
void drawWiFiStatusIcon(int centerX, int centerY, bool connected) {
  const uint16_t signalColor = connected ? C_ACID_GREEN : C_TEXT_MUTED;

  // Ícone compacto: fica abaixo da linha divisória e não encosta nela.
  canvas.drawLine(centerX - 8, centerY - 5, centerX - 6, centerY - 8, signalColor);
  canvas.drawLine(centerX - 6, centerY - 8, centerX + 6, centerY - 8, signalColor);
  canvas.drawLine(centerX + 6, centerY - 8, centerX + 8, centerY - 5, signalColor);
  canvas.drawLine(centerX - 5, centerY - 2, centerX - 3, centerY - 4, signalColor);
  canvas.drawLine(centerX - 3, centerY - 4, centerX + 3, centerY - 4, signalColor);
  canvas.drawLine(centerX + 3, centerY - 4, centerX + 5, centerY - 2, signalColor);
  canvas.fillCircle(centerX, centerY, 1, signalColor);

  // Sem rede, a faixa vermelha atravessa o símbolo como o ícone de Wi-Fi desligado.
  if (!connected) {
    canvas.drawLine(centerX - 8, centerY - 9, centerX + 8, centerY + 7, C_RED_ACTIVE);
    canvas.drawLine(centerX - 7, centerY - 9, centerX + 8, centerY + 6, C_RED_ACTIVE);
  }
}

void renderClimateCard() {
  int x = 161, y = 2, w = 158, h = 117;
  drawHUDCard(x, y, w, h, "CLIMATE MONITOR", NULL);

  int tx = x + 10;
  int ty = y + 26;
  canvas.fillCircle(tx + 5, ty + 16, 4, currentThemeHex);
  canvas.fillRect(tx + 3, ty + 4, 4, 13, currentThemeHex);
  canvas.drawCircle(tx + 5, ty + 4, 2, currentThemeHex);
  canvas.fillRect(tx + 4, ty + 8, 2, 7, 0x0000);

  canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 28, y + 26); canvas.print("INSIDE");

  float dispInside = (tempUnit == TEMP_CELSIUS) ? insideTemp : (insideTemp * 1.8f + 32.0f);
  float dispOutside = (tempUnit == TEMP_CELSIUS) ? outsideTemp : (outsideTemp * 1.8f + 32.0f);

  drawTemperatureValue(x + 85, y + 26, dispInside, tempUnit);

  for (int i = 0; i < 8; i++) {
    uint16_t col = (i < 4) ? currentThemeHex : C_BORDER_DARK;
    canvas.fillRect(x + 28 + (i * 7), y + 36, 5, 5, col);
  }

  int tx2 = x + 10;
  int ty2 = y + 50;
  canvas.fillCircle(tx2 + 5, ty2 + 16, 4, currentThemeHex);
  canvas.fillRect(tx2 + 3, ty2 + 4, 4, 13, currentThemeHex);
  canvas.drawCircle(tx2 + 5, ty2 + 4, 2, currentThemeHex);
  canvas.fillRect(tx2 + 4, ty2 + 8, 2, 7, 0x0000);

  canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 28, y + 52); canvas.print("OUTSIDE");

  drawTemperatureValue(x + 85, y + 52, dispOutside, tempUnit);

  for (int i = 0; i < 8; i++) {
    uint16_t col = (i < 4) ? currentThemeHex : C_BORDER_DARK;
    canvas.fillRect(x + 28 + (i * 7), y + 62, 5, 5, col);
  }

  canvas.drawFastHLine(x + 8, y + 74, 141, C_BORDER_DARK);
  canvas.fillCircle(x + 14, y + 83, 2, currentThemeHex);
  canvas.setTextColor(currentThemeHex, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 20, y + 80);
  canvas.print("UNIT:");
  canvas.drawCircle(x + 55, y + 82, 2, currentThemeHex);
  canvas.setCursor(x + 60, y + 80);
  canvas.print(tempUnit == TEMP_CELSIUS ? "C" : "F");

  // Substitui o texto WIFI/OFF por um ícone visual de conexão.
  drawWiFiStatusIcon(x + 130, y + 92, networkReady);
}

void renderVehicleCard() {
  int x = 1, y = 121, w = 158, h = 117;
  drawHUDCard(x, y, w, h, "VEHICLE PROFILE", NULL);

  canvas.setTextColor(currentThemeHex, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 104, y + 6);
  canvas.print("360 LIVE");

  canvas.drawRoundRect(x + 100, y + 4, 50, 11, 3, currentThemeHex);

  // Veículo maior e centralizado: o card agora fica inteiramente dedicado à imagem.
  if (vehicleMode == MODE_HOLOGRAMA) {
    drawCarSilhouette(x + 11, y + 35, 1.02f);
  } else {
    const int imageW = 137;
    const int imageH = 62;
    const int imageX = x + (w - imageW) / 2;
    const int imageY = y + 29;
    canvas.fillRoundRect(imageX, imageY, imageW, imageH, 6, 0x1084);
    canvas.drawRoundRect(imageX, imageY, imageW, imageH, 6, currentThemeHex);
    canvas.drawRect(imageX + 24, imageY + 18, 28, 20, C_TEXT_WHITE);
    canvas.fillCircle(imageX + 31, imageY + 25, 3, currentThemeHex);
    canvas.fillTriangle(imageX + 27, imageY + 37, imageX + 39, imageY + 27, imageX + 49, imageY + 37, C_TEXT_MUTED);
    canvas.setTextColor(C_TEXT_WHITE, 0x1084);
    canvas.setTextSize(1);
    canvas.setCursor(imageX + 66, imageY + 27);
    canvas.print("IMAGEM");
  }

  if (holdingCarCard) {
    unsigned long heldTime = millis() - touchStartTime;
    int progressPct = map(heldTime, 0, 1000, 0, 100);
    if (progressPct > 100) progressPct = 100;

    canvas.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 6, 0x0821);

    int iconX = x + (w / 2) - 12;
    int iconY = y + 18;
    canvas.fillRoundRect(iconX, iconY, 24, 20, 4, C_AMBER_JDM);
    canvas.fillRoundRect(iconX + 2, iconY + 2, 20, 16, 3, 0x0000);
    canvas.fillCircle(iconX + 7, iconY + 7, 2, C_AMBER_JDM);
    canvas.fillTriangle(iconX + 3, iconY + 16, iconX + 11, iconY + 9, iconX + 17, iconY + 16, C_AMBER_JDM);

    canvas.setTextColor(C_AMBER_JDM, 0x0000);
    canvas.setTextSize(1);
    
    const char* line1 = "SEGURE PARA TROCAR";
    int l1X = x + (w - strlen(line1) * 6) / 2;
    canvas.setCursor(l1X, y + 46);
    canvas.print(line1);

    const char* line2 = "IMAGEM...";
    int l2X = x + (w - strlen(line2) * 6) / 2;
    canvas.setCursor(l2X, y + 60);
    canvas.print(line2);

    int barX = x + 16;
    int barY = y + 82;
    int barW = w - 32;
    int fillW = map(progressPct, 0, 100, 0, barW - 4);

    canvas.drawRoundRect(barX, barY, barW, 8, 4, C_BORDER_DARK);
    if (fillW > 0) {
      canvas.fillRoundRect(barX + 2, barY + 2, fillW, 4, 2, C_AMBER_JDM);
    }
  }
}

void renderAudioCard() {
  int x = 161, y = 121, w = 158, h = 117;
  drawHUDCard(x, y, w, h, "AUDIO VISUALIZER", NULL);

  int startX = x + 8;
  int baseY = y + 72;

  for (int col = 0; col < 14; col++) {
    for (int row = 0; row < 8; row++) {
      canvas.fillRect(startX + (col * 10), baseY - (row * 6), 8, 4, C_BORDER_DARK);
    }
  }

  for (int col = 0; col < 14; col++) {
    int height = (int)(audioBars[col] * 8.0f);
    for (int row = 0; row < height; row++) {
      canvas.fillRect(startX + (col * 10), baseY - (row * 6), 8, 4, currentThemeHex);
    }
  }

  canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
  canvas.setTextSize(1);
  canvas.setCursor(x + 8, y + 88);
  canvas.print(audioSource == AUDIO_SRC_SIM ? "SIM" : "MIC");

  canvas.drawRoundRect(x + 48, y + 84, 52, 15, 7, C_BORDER_DARK);

  if (audioSource == AUDIO_SRC_SIM) {
    canvas.fillRoundRect(x + 49, y + 85, 25, 13, 6, currentThemeHex);
    canvas.setTextColor(0x0000, currentThemeHex);
    canvas.setCursor(x + 54, y + 88); canvas.print("SIM");

    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(x + 77, y + 88); canvas.print("MIC");
  } else {
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(x + 54, y + 88); canvas.print("SIM");

    canvas.fillRoundRect(x + 75, y + 85, 25, 13, 6, currentThemeHex);
    canvas.setTextColor(0x0000, currentThemeHex);
    canvas.setCursor(x + 79, y + 88); canvas.print("MIC");
  }
}

void renderFullscreenCarScreen() {
  canvas.fillScreen(0x0000);

  canvas.drawRoundRect(5, 5, 310, 230, 8, currentThemeHex);
  canvas.drawRoundRect(7, 7, 306, 226, 6, C_BORDER_DARK);

  canvas.setTextColor(currentThemeHex, 0x0000);
  canvas.setTextSize(1);
  canvas.setCursor(15, 14);
  canvas.print("VEHICLE PROFILE // 360 FULLSCREEN");

  // Sem preset, RPM ou velocidade: a tela cheia prioriza apenas a imagem centralizada.
  if (vehicleMode == MODE_HOLOGRAMA) {
    drawCarSilhouette(30, 74, 1.9f);
  } else {
    const int imageW = 296;
    const int imageH = 166;
    const int imageX = (320 - imageW) / 2;
    const int imageY = 38;
    canvas.fillRoundRect(imageX, imageY, imageW, imageH, 8, 0x1084);
    canvas.drawRoundRect(imageX, imageY, imageW, imageH, 8, currentThemeHex);
    canvas.setTextColor(C_TEXT_WHITE, 0x1084);
    canvas.setTextSize(2);
    canvas.setCursor(112, 116);
    canvas.print("IMAGEM");
  }

  canvas.setTextColor(C_TEXT_MUTED, 0x0000);
  canvas.setTextSize(1);
  canvas.setCursor(70, 218);
  canvas.print("[ TOQUE EM QUALQUER LUGAR PARA SAIR ]");
}

// Mostra imediatamente qual aba foi escolhida e avisa que o conteúdo está sendo preparado.
// O desenho completo continua no canvas no ciclo seguinte, sem mudar nenhuma área de toque.
void showPendingTabTransition() {
  tft.fillRect(0, 29, 320, 172, C_MENU_BG);

  const int tabY = 29;
  const int tabH = 26;
  const int personalX = 5, personalW = 104;
  const int configX = 110, configW = 110;
  const int updateX = 221, updateW = 94;

  if (activeTab == TAB_PERSONALIZACAO) {
    tft.fillRoundRect(personalX, tabY, personalW, tabH, 6, C_CARD_BG);
    tft.drawRoundRect(personalX, tabY, personalW, tabH, 6, tempSelectedHex);
  }
  tft.setTextColor(activeTab == TAB_PERSONALIZACAO ? C_TEXT_WHITE : C_TEXT_MUTED,
                   activeTab == TAB_PERSONALIZACAO ? C_CARD_BG : C_MENU_BG);
  tft.setTextSize(1);
  tft.setCursor(21, 38); tft.print("Personalizar");

  if (activeTab == TAB_CONFIGURACOES) {
    tft.fillRoundRect(configX, tabY, configW, tabH, 6, C_CARD_BG);
    tft.drawRoundRect(configX, tabY, configW, tabH, 6, tempSelectedHex);
  }
  tft.setTextColor(activeTab == TAB_CONFIGURACOES ? C_TEXT_WHITE : C_TEXT_MUTED,
                   activeTab == TAB_CONFIGURACOES ? C_CARD_BG : C_MENU_BG);
  tft.setCursor(126, 38); tft.print("Configuracoes");

  if (activeTab == TAB_ATUALIZACAO) {
    tft.fillRoundRect(updateX, tabY, updateW, tabH, 6, C_CARD_BG);
    tft.drawRoundRect(updateX, tabY, updateW, tabH, 6, tempSelectedHex);
  }
  tft.setTextColor(activeTab == TAB_ATUALIZACAO ? C_TEXT_WHITE : C_TEXT_MUTED,
                   activeTab == TAB_ATUALIZACAO ? C_CARD_BG : C_MENU_BG);
  tft.setCursor(235, 38); tft.print("Atualizacao");

  tft.drawFastHLine(0, 58, 320, C_BORDER_DARK);
  tft.setTextColor(C_TEXT_MUTED, C_MENU_BG);
  tft.setCursor(110, 120);
  tft.print("PREPARANDO MENU...");
}

void renderSettingsScreen() {
  canvas.fillScreen(C_MENU_BG);

  canvas.fillCircle(15, 14, 5, tempSelectedHex);
  canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
  canvas.setTextSize(1);
  canvas.setCursor(26, 10);
  canvas.print("MENU // HONDAPP");

  canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
  canvas.setCursor(302, 10);
  canvas.print("X");

  canvas.drawFastHLine(0, 26, 320, C_BORDER_DARK);

  // Abas um pouco maiores para facilitar o toque, com texto centralizado em cada botão.
  const int tabY = 29;
  const int tabH = 26;
  const int personalX = 5, personalW = 104;
  const int configX = 110, configW = 110;
  const int updateX = 221, updateW = 94;

  if (activeTab == TAB_PERSONALIZACAO) {
    canvas.fillRoundRect(personalX, tabY, personalW, tabH, 6, C_CARD_BG);
    canvas.drawRoundRect(personalX, tabY, personalW, tabH, 6, tempSelectedHex);
  }
  canvas.setTextColor(activeTab == TAB_PERSONALIZACAO ? C_TEXT_WHITE : C_TEXT_MUTED, activeTab == TAB_PERSONALIZACAO ? C_CARD_BG : C_MENU_BG);
  canvas.setCursor(21, 38); canvas.print("Personalizar");

  if (activeTab == TAB_CONFIGURACOES) {
    canvas.fillRoundRect(configX, tabY, configW, tabH, 6, C_CARD_BG);
    canvas.drawRoundRect(configX, tabY, configW, tabH, 6, tempSelectedHex);
  }
  canvas.setTextColor(activeTab == TAB_CONFIGURACOES ? C_TEXT_WHITE : C_TEXT_MUTED, activeTab == TAB_CONFIGURACOES ? C_CARD_BG : C_MENU_BG);
  canvas.setCursor(126, 38); canvas.print("Configuracoes");

  if (activeTab == TAB_ATUALIZACAO) {
    canvas.fillRoundRect(updateX, tabY, updateW, tabH, 6, C_CARD_BG);
    canvas.drawRoundRect(updateX, tabY, updateW, tabH, 6, tempSelectedHex);
  }
  canvas.setTextColor(activeTab == TAB_ATUALIZACAO ? C_TEXT_WHITE : C_TEXT_MUTED, activeTab == TAB_ATUALIZACAO ? C_CARD_BG : C_MENU_BG);
  canvas.setCursor(235, 38); canvas.print("Atualizacao");

  canvas.drawFastHLine(0, 58, 320, C_BORDER_DARK);

  canvas.setClipRect(0, 60, 320, 135);

  if (activeTab == TAB_PERSONALIZACAO) {
    int sy = 68 - scrollY;

    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy);
    canvas.print("# PALETA DE CORES (TEMA HUD)");

    bool sel1 = (tempSelectedHex == C_RED_ACTIVE);
    canvas.fillRoundRect(10, sy + 16, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 16, 145, 38, 6, sel1 ? C_RED_ACTIVE : C_BORDER_DARK);
    canvas.fillCircle(25, sy + 35, 7, C_RED_ACTIVE);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(38, sy + 22); canvas.print("Honda Type-R Red");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(38, sy + 36); canvas.print("#ef4444");

    bool sel2 = (tempSelectedHex == C_AMBER_JDM);
    canvas.fillRoundRect(165, sy + 16, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(165, sy + 16, 145, 38, 6, sel2 ? C_AMBER_JDM : C_BORDER_DARK);
    canvas.fillCircle(180, sy + 35, 7, C_AMBER_JDM);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(193, sy + 22); canvas.print("JDM Amber VTEC");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(193, sy + 36); canvas.print("#f59e0b");

    bool sel3 = (tempSelectedHex == C_CYAN_BLUE);
    canvas.fillRoundRect(10, sy + 60, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 60, 145, 38, 6, sel3 ? C_CYAN_BLUE : C_BORDER_DARK);
    canvas.fillCircle(25, sy + 79, 7, C_CYAN_BLUE);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(38, sy + 66); canvas.print("Cyberpunk Cyan Blue");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(38, sy + 80); canvas.print("#06b6d4");

    bool sel4 = (tempSelectedHex == C_ACID_GREEN);
    canvas.fillRoundRect(165, sy + 60, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(165, sy + 60, 145, 38, 6, sel4 ? C_ACID_GREEN : C_BORDER_DARK);
    canvas.fillCircle(180, sy + 79, 7, C_ACID_GREEN);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(193, sy + 66); canvas.print("Motorsport Acid Green");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(193, sy + 80); canvas.print("#00ff66");

    bool sel5 = (tempSelectedHex == C_PURPLE_NEON);
    canvas.fillRoundRect(10, sy + 104, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 104, 145, 38, 6, sel5 ? C_PURPLE_NEON : C_BORDER_DARK);
    canvas.fillCircle(25, sy + 123, 7, C_PURPLE_NEON);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(38, sy + 110); canvas.print("Midnight Purple Neon");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(38, sy + 124); canvas.print("#a855f7");

    bool sel6 = (tempSelectedHex == C_SPOON_AQUA);
    canvas.fillRoundRect(165, sy + 104, 145, 38, 6, C_CARD_BG);
    canvas.drawRoundRect(165, sy + 104, 145, 38, 6, sel6 ? C_SPOON_AQUA : C_BORDER_DARK);
    canvas.fillCircle(180, sy + 123, 7, C_SPOON_AQUA);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(193, sy + 110); canvas.print("Spoon Sports Aqua");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(193, sy + 124); canvas.print("#00e5ff");

    canvas.drawFastHLine(10, sy + 150, 300, C_BORDER_DARK);
  }

  if (activeTab == TAB_CONFIGURACOES) {
    int sy = 68 - scrollY;
    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy);
    canvas.print("# LUMINOSIDADE DA TELA (BACKLIGHT)");

    int brightPct = map(screenBrightness, 10, 255, 4, 100);
    char bBuf[16];
    sprintf(bBuf, "%d%%", brightPct);
    canvas.setTextColor(tempSelectedHex, C_MENU_BG);
    canvas.setCursor(275, sy);
    canvas.print(bBuf);

    canvas.fillRoundRect(10, sy + 16, 32, 32, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 16, 32, 32, 6, C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setTextSize(2);
    canvas.setCursor(21, sy + 24); canvas.print("-");

    int barW = 216;
    int fillW = map(screenBrightness, 10, 255, 0, barW - 4);
    canvas.drawRoundRect(52, sy + 26, barW, 12, 6, C_BORDER_DARK);
    if (fillW > 0) {
      canvas.fillRoundRect(54, sy + 28, fillW, 8, 4, tempSelectedHex);
    }

    canvas.setTextSize(1);
    canvas.fillRoundRect(278, sy + 16, 32, 32, 6, C_CARD_BG);
    canvas.drawRoundRect(278, sy + 16, 32, 32, 6, C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setTextSize(2);
    canvas.setCursor(288, sy + 24); canvas.print("+");

    canvas.setTextSize(1);
    canvas.drawFastHLine(10, sy + 60, 300, C_BORDER_DARK);

    // A linha fica com uma margem igual acima e abaixo, sem encostar no cartão Wi-Fi nem no próximo título.
    canvas.drawFastHLine(10, sy + 164, 300, C_BORDER_DARK);

    const uint16_t manualTimeColor = networkReady ? C_TEXT_MUTED : C_TEXT_WHITE;
    const uint16_t manualTimeBorder = networkReady ? C_BORDER_DARK : tempSelectedHex;
    canvas.setTextColor(manualTimeColor, C_MENU_BG);
    canvas.setCursor(10, sy + 176);
    canvas.print(networkReady ? "# HORA PELA REDE WI-FI (BLOQUEADA)" : "# AJUSTE MANUAL DE HORA (HH:MM)");

    // Controles afastados do título para a leitura não ficar apertada.
    canvas.fillRoundRect(105, sy + 199, 110, 36, 6, C_CARD_BG);
    canvas.drawRoundRect(105, sy + 199, 110, 36, 6, manualTimeBorder);

    char timeEditBuf[16];
    sprintf(timeEditBuf, "%02d:%02d", manualHour, manualMinute);
    canvas.setTextColor(networkReady ? C_TEXT_MUTED : tempSelectedHex, C_MENU_BG);
    canvas.setTextSize(2);
    canvas.setCursor(127, sy + 209);
    canvas.print(timeEditBuf);
    canvas.setTextSize(1);

    canvas.fillRoundRect(45, sy + 199, 36, 17, 4, C_CARD_BG);
    canvas.drawRoundRect(45, sy + 199, 36, 17, 4, C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(59, sy + 204); canvas.print("^");

    canvas.fillRoundRect(45, sy + 218, 36, 17, 4, C_CARD_BG);
    canvas.drawRoundRect(45, sy + 218, 36, 17, 4, C_BORDER_DARK);
    canvas.setCursor(59, sy + 223); canvas.print("v");

    canvas.fillRoundRect(239, sy + 199, 36, 17, 4, C_CARD_BG);
    canvas.drawRoundRect(239, sy + 199, 36, 17, 4, C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(253, sy + 204); canvas.print("^");

    canvas.fillRoundRect(239, sy + 218, 36, 17, 4, C_CARD_BG);
    canvas.drawRoundRect(239, sy + 218, 36, 17, 4, C_BORDER_DARK);
    canvas.setCursor(253, sy + 223); canvas.print("v");

    canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
    canvas.setCursor(48, sy + 240);
    canvas.print("HORA");
    canvas.setCursor(241, sy + 240);
    canvas.print("MINUTO");

    canvas.drawFastHLine(10, sy + 254, 300, C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy + 268);
    canvas.print("# BOTAO MENU NO PAINEL");
    canvas.fillRoundRect(10, sy + 280, 300, 34, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 280, 300, 34, 6, menuButtonVisible ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(menuButtonVisible ? C_TEXT_WHITE : C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(24, sy + 292);
    canvas.print(menuButtonVisible ? "VISIVEL" : "OCULTO");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(112, sy + 292);
    canvas.print("TOQUE PARA ALTERAR");

    canvas.drawFastHLine(10, sy + 322, 300, C_BORDER_DARK);
    canvas.fillRoundRect(10, sy + 334, 300, 34, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 334, 300, 34, 6, C_RED_ACTIVE);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(74, sy + 346);
    canvas.print("TESTAR NOTIFICACAO DE ATUALIZACAO");
  }

  if (activeTab == TAB_PERSONALIZACAO) {
    int sy = 68 - scrollY;
    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy + 160);
    canvas.print("# FOTO, GIF OU HOLOGRAMA DO VEICULO");

    bool mHolo = (vehicleMode == MODE_HOLOGRAMA);
    canvas.fillRoundRect(10, sy + 176, 145, 32, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 176, 145, 32, 6, mHolo ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(24, sy + 187); canvas.print("HOLOGRAMA VETOR");

    bool mFoto = (vehicleMode == MODE_FOTO_GIF);
    canvas.fillRoundRect(165, sy + 176, 145, 32, 6, C_CARD_BG);
    canvas.drawRoundRect(165, sy + 176, 145, 32, 6, mFoto ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(205, sy + 187); canvas.print("FOTO OU GIF");

    canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
    canvas.setCursor(10, sy + 216);
    canvas.print("MODELO DO PRESET");

    canvas.fillRoundRect(10, sy + 228, 300, 32, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 228, 300, 32, 6, tempSelectedHex);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(20, sy + 238);
    canvas.print("Honda Civic 1999 Sedan (EJ/EK)");

    canvas.drawFastHLine(10, sy + 268, 300, C_BORDER_DARK);

    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy + 278);
    canvas.print("TELA DE INICIALIZACAO & LOGO");

    canvas.fillRoundRect(200, sy + 272, 110, 22, 11, C_ORANGE_BOOT);
    canvas.setTextColor(C_TEXT_WHITE, C_ORANGE_BOOT);
    canvas.setCursor(212, sy + 279);
    canvas.print("> TESTAR BOOT");

    canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
    canvas.setCursor(10, sy + 296);
    canvas.print("LOGO OU GIF DA INICIALIZACAO");

    canvas.fillRoundRect(10, sy + 310, 95, 52, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 310, 95, 52, 6, bootMode == BOOT_HONDA ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(C_SILVER, C_CARD_BG);
    canvas.setCursor(35, sy + 332); canvas.print("HONDA");

    canvas.fillRoundRect(112, sy + 310, 95, 52, 6, C_CARD_BG);
    canvas.drawRoundRect(112, sy + 310, 95, 52, 6, bootMode == BOOT_TYPE_R ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(C_RED_ACTIVE, C_CARD_BG);
    canvas.setCursor(134, sy + 332); canvas.print("TYPE R");

    canvas.fillRoundRect(215, sy + 310, 95, 52, 6, C_CARD_BG);
    canvas.drawRoundRect(215, sy + 310, 95, 52, 6, bootMode == BOOT_CUSTOM ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(238, sy + 332); canvas.print("CUSTOM");

    canvas.drawFastHLine(10, sy + 378, 300, C_BORDER_DARK);
  }

  if (activeTab == TAB_CONFIGURACOES) {
    int sy = 68 - scrollY;
    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(10, sy + 70);
    canvas.print("# REDE WI-FI");
    canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
    canvas.setCursor(10, sy + 84);
    canvas.print("REDE ATUAL: ");
    canvas.setTextColor(networkReady ? tempSelectedHex : C_AMBER_JDM, C_MENU_BG);
    String shownNetwork = configuredNetworkName;
    if (shownNetwork.length() > 27) shownNetwork = shownNetwork.substring(0, 24) + "...";
    canvas.print(shownNetwork);
    canvas.setTextColor(networkReady ? C_ACID_GREEN : C_AMBER_JDM, C_MENU_BG);
    canvas.setCursor(10, sy + 97);
    canvas.print(networkReady ? "STATUS: CONECTADA" : "STATUS: NAO CONECTADA");

    // Abaixo do estado da rede, sem intervalo grande entre as opções.
    canvas.fillRoundRect(10, sy + 110, 300, 42, 6, C_CARD_BG);
    canvas.drawRoundRect(10, sy + 110, 300, 42, 6, tempSelectedHex);
    canvas.setTextColor(C_TEXT_WHITE, C_CARD_BG);
    canvas.setCursor(54, sy + 117);
    canvas.print("TROCAR REDE WI-FI");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(44, sy + 134);
    canvas.print("HONDAPP-SETUP  |  192.168.4.1");
  } else if (activeTab == TAB_ATUALIZACAO) {
    canvas.setTextColor(C_TEXT_WHITE, C_MENU_BG);
    canvas.setCursor(18, 78);
    canvas.print("ATUALIZACAO OTA");
    canvas.drawFastHLine(18, 91, 284, C_BORDER_DARK);
    canvas.setTextColor(networkReady ? C_ACID_GREEN : C_AMBER_JDM, C_MENU_BG);
    canvas.setCursor(18, 108);
    canvas.print(networkReady ? "WI-FI CONECTADO" : "WI-FI NAO CONECTADO");
    canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
    canvas.setCursor(18, 125);
    canvas.print("GITHUB .BIN VIA HTTPS");
    canvas.fillRoundRect(18, 142, 284, 42, 6, C_CARD_BG);
    canvas.drawRoundRect(18, 142, 284, 42, 6, networkReady ? tempSelectedHex : C_BORDER_DARK);
    canvas.setTextColor(networkReady ? C_TEXT_WHITE : C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(76, 153);
    canvas.print("VERIFICAR E ATUALIZAR");
    canvas.setTextColor(C_TEXT_MUTED, C_CARD_BG);
    canvas.setCursor(32, 169);
    if (otaCheckComplete && otaLatestVersion.length()) {
      canvas.print("INSTALADA ");
      canvas.print(HONDAPP_FIRMWARE_VERSION);
      canvas.print("  /  DISPONIVEL ");
      canvas.print(otaLatestVersion);
    } else {
      canvas.print(otaStatus);
    }
  }

  canvas.clearClipRect();

  if (wifiNoticeUntil > millis()) {
    canvas.fillRoundRect(18, 158, 284, 24, 5, C_CARD_BG);
    canvas.drawRoundRect(18, 158, 284, 24, 5, C_AMBER_JDM);
    canvas.setTextColor(C_AMBER_JDM, C_CARD_BG);
    canvas.setCursor(28, 166);
    canvas.print(wifiNotice);
  }

  // Faixa inferior compacta: os botões ficam centralizados na altura da faixa e mantêm suas laterais.
  canvas.drawFastHLine(0, 201, 320, C_BORDER_DARK);

  canvas.setTextColor(C_TEXT_MUTED, C_MENU_BG);
  canvas.setCursor(10, 215);
  canvas.print("HONDAPP");

  canvas.fillRoundRect(126, 208, 55, 24, 6, C_BTN_GRAY);
  canvas.drawRoundRect(126, 208, 55, 24, 6, C_BORDER_DARK);
  canvas.setTextColor(C_TEXT_WHITE, C_BTN_GRAY);
  canvas.setCursor(136, 216);
  canvas.print("FECHAR");

  canvas.fillRoundRect(186, 208, 124, 24, 8, tempSelectedHex);
  
  uint16_t saveTextColor = C_TEXT_WHITE;
  if (tempSelectedHex == C_ACID_GREEN || tempSelectedHex == C_SPOON_AQUA) {
    saveTextColor = 0x0000;
  }
  
  canvas.setTextColor(saveTextColor, tempSelectedHex);
  canvas.setCursor(195, 216);
  canvas.print("SALVAR E APLICAR");
}

// ============================================================
// EVENTOS DE TOUCH
// ============================================================
void handleTouchEvents() {
  uint16_t touchX, touchY;
  bool isTouching = getTouch(&touchX, &touchY);

  // O leitor de toque continua rapido, mas um quadro novo so e enviado no ritmo
  // limitado pelo loop principal. Isso evita gastar CPU e SPI em redesenhos extras.
  // Nas listas, só há novo desenho quando uma opção muda ou a posição realmente muda.
  if (currentState != STATE_SETTINGS && (isTouching || touchPressed)) requestRedraw();

  if (currentState == STATE_HUD) {
    if (isTouching) {
      if (!touchPressed) {
        touchPressed = true;
        startTouchX = touchX;
        startTouchY = touchY;
        touchStartTime = millis();
        longPressTriggered = false;

        if (startTouchX >= 1 && startTouchX <= 159 && startTouchY >= 121 && startTouchY <= 238) {
          holdingCarCard = true;
        }
      } else {
        lastTouchY = touchY;
        if (holdingCarCard && !longPressTriggered) {
          if (millis() - touchStartTime > 1000) {
            longPressTriggered = true;
            holdingCarCard = false;
            currentState = STATE_SETTINGS;
            activeTab = TAB_PERSONALIZACAO;
            scrollY = 160;
            tempSelectedHex = currentThemeHex;
            
            time_t now = time(nullptr);
            struct tm *tmNow = localtime(&now);
            manualHour = tmNow->tm_hour;
            manualMinute = tmNow->tm_min;
          }
        }
      }
    } 
    else {
      if (touchPressed) {
        holdingCarCard = false;
        if (!longPressTriggered) {
          // O aviso fica sobre o cartão de clima, por isso é tratado antes do toque normal do painel.
          if (testUpdateNoticeVisible && startTouchX >= 154 && startTouchX <= 312 && startTouchY >= 18 && startTouchY <= 60) {
            currentState = STATE_SETTINGS;
            activeTab = TAB_ATUALIZACAO;
            scrollY = 0;
            tempSelectedHex = currentThemeHex;
            testUpdateNoticeVisible = false;
          }
          else if ((menuButtonVisible && startTouchY <= 25 && startTouchX >= 100 && startTouchX <= 220) ||
                   (startTouchY <= 28 && (int)lastTouchY - (int)startTouchY >= 30)) {
            // O botão flutua sobre o HUD; quando ele está oculto, o mesmo menu abre puxando a faixa superior para baixo.
            currentState = STATE_SETTINGS;
            tempSelectedHex = currentThemeHex;
            time_t now = time(nullptr);
            struct tm *tmNow = localtime(&now);
            manualHour = tmNow->tm_hour;
            manualMinute = tmNow->tm_min;
          }
          else if (startTouchX >= 1 && startTouchX <= 159 && startTouchY >= 2 && startTouchY <= 118) {
            timeFormat = (timeFormat == TIME_12H) ? TIME_24H : TIME_12H;
            saveSettings();
            delay(120);
          }
          else if (startTouchX >= 161 && startTouchX <= 319 && startTouchY >= 2 && startTouchY <= 118) {
            tempUnit = (tempUnit == TEMP_CELSIUS) ? TEMP_FAHRENHEIT : TEMP_CELSIUS;
            saveSettings();
            delay(120);
          }
          else if (startTouchX >= 1 && startTouchX <= 159 && startTouchY >= 121 && startTouchY <= 238) {
            currentState = STATE_FULLSCREEN_CAR;
          }
          else if (startTouchX >= 161 && startTouchX <= 319 && startTouchY >= 205 && startTouchY <= 220) {
            audioSource = (audioSource == AUDIO_SRC_SIM) ? AUDIO_SRC_MIC : AUDIO_SRC_SIM;
            if (audioSource == AUDIO_SRC_MIC) {
              startMicrophone();
            }
            delay(150);
          }
        }
        touchPressed = false;
        longPressTriggered = false;
      }
    }
  } 
  else if (currentState == STATE_FULLSCREEN_CAR) {
    if (isTouching) {
      if (!touchPressed) touchPressed = true;
    } else {
      if (touchPressed) {
        currentState = STATE_HUD;
        touchPressed = false;
        delay(150);
      }
    }
  }
  else if (currentState == STATE_SETTINGS) {
    if (isTouching) {
      if (!touchPressed) {
        touchPressed = true;
        startTouchX = touchX;
        startTouchY = touchY;
        lastTouchY = touchY;
        scrollStartVal = scrollY;
        hasDragged = false;

        // As abas mudam no toque inicial; tocar na aba aberta nao gera redesenho extra.
        if (startTouchY >= 29 && startTouchY <= 55) {
          ActiveTab requestedTab = activeTab;
          if (startTouchX >= 5 && startTouchX <= 109) requestedTab = TAB_PERSONALIZACAO;
          else if (startTouchX >= 110 && startTouchX <= 220) requestedTab = TAB_CONFIGURACOES;
          else if (startTouchX >= 221 && startTouchX <= 315) requestedTab = TAB_ATUALIZACAO;
          if (requestedTab != activeTab) {
            // A seleção aparece já no primeiro contato. O conteúdo pesado fica pendente
            // apenas até o próximo ciclo, em vez de atrasar a resposta do toque.
            activeTab = requestedTab;
            scrollY = 0;
            tabContentPending = true;
            tabContentReadyAt = millis() + 1UL;
            showPendingTabTransition();
            requestRedraw();
          }
        }
      } else {
        int diffY = startTouchY - touchY;
        if (abs(diffY) > 6) hasDragged = true;

        if (hasDragged && touchY >= 58 && touchY <= 200) {
          int nextScrollY = scrollStartVal + diffY;
          if (nextScrollY < 0) nextScrollY = 0;
          const int maxTabScroll = getSettingsMaxScroll();
          if (nextScrollY > maxTabScroll) nextScrollY = maxTabScroll;
          if (nextScrollY != scrollY) {
            scrollY = nextScrollY;
            requestRedraw();
          }
        }
      }
    } 
    else {
      if (touchPressed) {
        if (!hasDragged) {
          if (startTouchX >= 290 && startTouchY <= 25) {
            currentState = STATE_HUD;
          }

          if (startTouchX >= 126 && startTouchX <= 181 && startTouchY >= 208 && startTouchY <= 232) {
            currentState = STATE_HUD;
          }

          if (startTouchX >= 186 && startTouchX <= 310 && startTouchY >= 208 && startTouchY <= 232) {
            currentThemeHex = tempSelectedHex;
            // Nunca substitui a hora sincronizada por Wi-Fi com um valor manual antigo.
            if (!networkReady) applyManualTime();
            saveSettings();
            currentState = STATE_HUD;
          }

          if (startTouchY >= 60 && startTouchY <= 195) {
            int contentY = startTouchY - 68 + scrollY;

            if (activeTab == TAB_PERSONALIZACAO) {
              if (contentY >= 16 && contentY <= 54) {
                if (startTouchX >= 10 && startTouchX <= 155) tempSelectedHex = C_RED_ACTIVE;
                if (startTouchX >= 165 && startTouchX <= 310) tempSelectedHex = C_AMBER_JDM;
              }
              if (contentY >= 60 && contentY <= 98) {
                if (startTouchX >= 10 && startTouchX <= 155) tempSelectedHex = C_CYAN_BLUE;
                if (startTouchX >= 165 && startTouchX <= 310) tempSelectedHex = C_ACID_GREEN;
              }
              if (contentY >= 104 && contentY <= 142) {
                if (startTouchX >= 10 && startTouchX <= 155) tempSelectedHex = C_PURPLE_NEON;
                if (startTouchX >= 165 && startTouchX <= 310) tempSelectedHex = C_SPOON_AQUA;
              }
            }

            if (activeTab == TAB_CONFIGURACOES) {
              if (contentY >= 10 && contentY <= 50) {
                if (startTouchX <= 45) {
                  if (screenBrightness > 25) screenBrightness -= 25;
                  else screenBrightness = 10;
                } else if (startTouchX >= 270) {
                  if (screenBrightness < 230) screenBrightness += 25;
                  else screenBrightness = 255;
                } else {
                  int touchVal = map(startTouchX, 52, 268, 10, 255);
                  if (touchVal < 10) touchVal = 10;
                  if (touchVal > 255) touchVal = 255;
                  screenBrightness = touchVal;
                }
                tft.setBrightness(screenBrightness);
              }

              // Com Wi-Fi conectado, a hora vem da rede e os controles manuais ficam bloqueados.
              if (!networkReady && contentY >= 199 && contentY <= 239) {
                if (startTouchX >= 45 && startTouchX <= 81) {
                  if (contentY <= 217) {
                    manualHour = (manualHour == 23) ? 0 : manualHour + 1;
                  } else {
                    manualHour = (manualHour == 0) ? 23 : manualHour - 1;
                  }
                } 
                else if (startTouchX >= 239 && startTouchX <= 275) {
                  if (contentY <= 217) {
                    manualMinute = (manualMinute == 59) ? 0 : manualMinute + 1;
                  } else {
                    manualMinute = (manualMinute == 0) ? 59 : manualMinute - 1;
                  }
                }
              }

            }

            if (activeTab == TAB_PERSONALIZACAO) {
              if (contentY >= 176 && contentY <= 208) {
                if (startTouchX <= 155) vehicleMode = MODE_HOLOGRAMA;
                else vehicleMode = MODE_FOTO_GIF;
              }

              if (contentY >= 272 && contentY <= 294 && startTouchX >= 200) {
                currentState = STATE_BOOT;
                bootStartTime = millis();
              }

              if (contentY >= 310 && contentY <= 362) {
                if (startTouchX <= 105) bootMode = BOOT_HONDA;
                else if (startTouchX <= 210) bootMode = BOOT_TYPE_R;
                else bootMode = BOOT_CUSTOM;
              }
            }

            if (activeTab == TAB_CONFIGURACOES) {
              if (contentY >= 110 && contentY <= 152) {
                startWiFiReconfiguration();
              }
              if (contentY >= 280 && contentY <= 314) {
                menuButtonVisible = !menuButtonVisible;
              }
              if (contentY >= 334 && contentY <= 368) {
                // Prévia local: não consulta o GitHub nem inicia download OTA.
                testUpdateNoticeVisible = true;
                testUpdateNoticeUntil = millis() + 6000UL;
                currentState = STATE_HUD;
              }
            }

            if (activeTab == TAB_ATUALIZACAO) {
              // Primeiro toque consulta o Release; só baixa quando existe uma versão realmente mais nova.
              if (startTouchX >= 18 && startTouchX <= 302 && startTouchY >= 142 && startTouchY <= 184) {
                checkForFirmwareUpdate(true);
                if (otaUpdateAvailable) performOTAUpdate();
              }
            }
          }
        }
        if (!hasDragged) requestRedraw();
        touchPressed = false;
        hasDragged = false;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(CTP_SDA, CTP_SCL, 400000);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(AUDIO_ENABLE, OUTPUT);
  setAmplifierEnabled(false);

  loadSavedSettings();
  tft.init();
  tft.setRotation(3);
  tft.setBrightness(screenBrightness);

  // Este painel usa o alvo ESP32-S3 N16R8: 16 MB de flash com duas áreas OTA
  // e 8 MB de PSRAM. O framebuffer sai da RAM interna e deixa espaço para HTTPS.
  if (psramFound()) canvas.setPsram(true);
  canvas.createSprite(320, 240);

  struct tm t_compile = {0};
  char monStr[4];
  sscanf(__DATE__, "%s %d %d", monStr, &t_compile.tm_mday, &t_compile.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &t_compile.tm_hour, &t_compile.tm_min, &t_compile.tm_sec);

  const char *monthNames = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int monIndex = (strstr(monthNames, monStr) - monthNames) / 3;

  t_compile.tm_mon = monIndex;
  t_compile.tm_year -= 1900;

  time_t t = mktime(&t_compile);
  struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  configureWiFiAndClock();
  bootStartTime = millis();

}

void loop() {
  // Prioriza o toque: serviços de rede não podem atrasar a troca visual de abas.
  handleTouchEvents();
  maintainOnlineServices();

  const unsigned long now = millis();
  const bool bootAnimating = currentState == STATE_BOOT;
  const bool hudAnimating = currentState == STATE_HUD;
  // Boot permanece fluido; o visualizador usa 20 FPS. Telas estaticas so redesenham por evento.
  const unsigned long frameInterval = bootAnimating ? 33UL : (hudAnimating ? 50UL : 0UL);
  const bool stateChanged = currentState != lastRenderedState;
  const bool animationDue = frameInterval > 0 && now - lastRenderAt >= frameInterval;

  // Depois do retorno imediato da aba, entrega o conteúdo no ciclo seguinte.
  const bool waitForPendingTab = tabContentPending && now < tabContentReadyAt;
  if (!waitForPendingTab && (displayNeedsRedraw || stateChanged || animationDue)) {
    if (currentState == STATE_BOOT) {
      renderBootScreen();
      if (now - bootStartTime >= 3000) {
        currentState = STATE_HUD;
        // A consulta OTA começa depois da conexão Wi-Fi e mostra o cartão apenas se houver Release novo.
        requestRedraw();
      }
    } else if (currentState == STATE_HUD) {
      canvas.fillScreen(0x0000);
      updateAudioAnimation();
      renderDriverCard();
      renderClimateCard();
      renderVehicleCard();
      renderAudioCard();
      // O botão é desenhado por último, flutuando sobre os cartões sem deslocá-los.
      if (menuButtonVisible) drawTopMenuTab();
      // O aviso vem por último, sobre o painel já pronto; assim o fundo não o apaga.
      drawTestUpdateNotice();
    } else if (currentState == STATE_FULLSCREEN_CAR) {
      renderFullscreenCarScreen();
    } else if (currentState == STATE_SETTINGS) {
      renderSettingsScreen();
    }

    canvas.pushSprite(0, 0);
    lastRenderedState = currentState;
    lastRenderAt = now;
    displayNeedsRedraw = false;
    tabContentPending = false;
  }

  // Cede tempo ao Wi-Fi e ao portal, sem impor um atraso que limite o toque.
  delay(1);
}