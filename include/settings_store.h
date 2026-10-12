#pragma once

#include <Arduino.h>

struct DashboardSettings {
  uint16_t theme;
  uint8_t brightness;
  uint8_t bootMode;
  uint8_t timeFormat;
  uint8_t tempUnit;
  bool menuButtonVisible;
};

// Lê e grava somente as preferências persistentes do painel.
void loadDashboardSettings(DashboardSettings &settings);
void saveDashboardSettings(const DashboardSettings &settings);

String loadInstalledOtaVersion();
void saveInstalledOtaVersion(const String &version);

void loadSavedLocation(float &latitude, float &longitude, String &timezone, String &city);
void saveLocation(float latitude, float longitude, const String &timezone, const String &city);
