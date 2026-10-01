#pragma once

#include <Arduino.h>

struct DashboardSettings {
  uint16_t theme;
  uint8_t brightness;
  uint8_t bootMode;
  uint8_t vehicleMode;
  uint8_t timeFormat;
  uint8_t tempUnit;
};

// Lê e grava somente as preferências persistentes do painel.
// O restante da interface continua no arquivo principal.
void loadDashboardSettings(DashboardSettings &settings);
void saveDashboardSettings(const DashboardSettings &settings);
