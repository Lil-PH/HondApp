#pragma once

#include <Arduino.h>

struct DashboardSettings {
  uint16_t theme;
  uint8_t brightness;
  uint8_t bootMode;
  uint8_t vehicleMode;
  uint8_t timeFormat;
  uint8_t tempUnit;
  bool menuButtonVisible;
};

// Lê e grava somente as preferências persistentes do painel.
// O restante da interface continua no arquivo principal.
void loadDashboardSettings(DashboardSettings &settings);
void saveDashboardSettings(const DashboardSettings &settings);

// A versão é gravada somente depois que o binário de um Release OTA foi
// recebido por completo e validado pela ESP32. Portanto não depende de uma
// string alterada manualmente no fonte a cada publicação.
String loadInstalledOtaVersion();
void saveInstalledOtaVersion(const String &version);

// Última localização aproximada obtida pela rede. Ela mantém hora e clima úteis
// quando a placa inicia antes de a internet ficar disponível.
void loadSavedLocation(float &latitude, float &longitude, String &timezone, String &city);
void saveLocation(float latitude, float longitude, const String &timezone, const String &city);
