#include "settings_store.h"
#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "hondapp";
}

void loadDashboardSettings(DashboardSettings &settings) {
  Preferences preferences;
  preferences.begin(kNamespace, true);
  settings.theme = preferences.getUShort("theme", 0x07E0);
  settings.brightness = preferences.getUChar("bright", 255);
  settings.bootMode = preferences.getUChar("boot", 0);
  settings.vehicleMode = preferences.getUChar("vehicle", 0);
  settings.timeFormat = preferences.getUChar("timefmt", 0);
  settings.tempUnit = preferences.getUChar("tempunit", 0);
  settings.menuButtonVisible = preferences.getBool("menubtn", true);
  preferences.end();
}

void saveDashboardSettings(const DashboardSettings &settings) {
  Preferences preferences;
  preferences.begin(kNamespace, false);
  preferences.putUShort("theme", settings.theme);
  preferences.putUChar("bright", settings.brightness);
  preferences.putUChar("boot", settings.bootMode);
  preferences.putUChar("vehicle", settings.vehicleMode);
  preferences.putUChar("timefmt", settings.timeFormat);
  preferences.putUChar("tempunit", settings.tempUnit);
  preferences.putBool("menubtn", settings.menuButtonVisible);
  preferences.end();
}
