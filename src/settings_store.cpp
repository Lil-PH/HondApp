#include "settings_store.h"
#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "hondapp";
constexpr const char *kOtaVersionKey = "otaver";
constexpr const char *kInitialOtaVersion = "v1";
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

String loadInstalledOtaVersion() {
  Preferences preferences;
  preferences.begin(kNamespace, true);
  const String version = preferences.getString(kOtaVersionKey, kInitialOtaVersion);
  preferences.end();
  return version;
}

void saveInstalledOtaVersion(const String &version) {
  Preferences preferences;
  preferences.begin(kNamespace, false);
  preferences.putString(kOtaVersionKey, version);
  preferences.end();
}

void loadSavedLocation(float &latitude, float &longitude, String &timezone, String &city) {
  Preferences preferences;
  preferences.begin(kNamespace, true);
  latitude = preferences.getFloat("geolat", -20.21f);
  longitude = preferences.getFloat("geolon", -40.30f);
  timezone = preferences.getString("geotz", "America/Sao_Paulo");
  city = preferences.getString("geocity", "Serra, ES");
  preferences.end();
}

void saveLocation(float latitude, float longitude, const String &timezone, const String &city) {
  Preferences preferences;
  preferences.begin(kNamespace, false);
  preferences.putFloat("geolat", latitude);
  preferences.putFloat("geolon", longitude);
  preferences.putString("geotz", timezone);
  preferences.putString("geocity", city);
  preferences.end();
}
