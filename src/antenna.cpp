#include "antenna.h"

#if defined(ARDUINO_XIAO_ESP32C6)
#include <Arduino.h>
#include <Preferences.h>
#include <pins_arduino.h>

namespace {
constexpr char NAMESPACE[] = "Antenna";
constexpr char KEY[] = "External";
constexpr uint32_t SWITCH_DELAY_MS = 250;
bool externalAntenna = false;
bool switchPending = false;
uint32_t switchAt = 0;

void applyAntenna(bool external) {
  // GPIO3 enables the RF switch; GPIO14 selects ceramic (LOW) or external (HIGH).
  pinMode(WIFI_ENABLE, OUTPUT);
  digitalWrite(WIFI_ENABLE, LOW);
  delay(100);
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, external ? HIGH : LOW);
}
}  // namespace

void initAntenna() {
  Preferences pref;
  if (pref.begin(NAMESPACE, true)) {
    externalAntenna = pref.getBool(KEY, false);
    pref.end();
  }
  applyAntenna(externalAntenna);
}

bool isExternalAntenna() { return externalAntenna; }

bool saveAntenna(bool external) {
  if (external == externalAntenna) return true;
  Preferences pref;
  if (!pref.begin(NAMESPACE, false)) return false;
  const bool saved = pref.putBool(KEY, external) == 1;
  pref.end();
  if (!saved) return false;
  externalAntenna = external;
  switchPending = true;
  switchAt = millis();
  return true;
}

void handleAntenna() {
  if (switchPending && millis() - switchAt >= SWITCH_DELAY_MS) {
    switchPending = false;
    applyAntenna(externalAntenna);
  }
}

void clearAntennaPreferences() {
  Preferences pref;
  if (pref.begin(NAMESPACE, false)) {
    pref.clear();
    pref.end();
  }
  // Factory reset reboots; leave the current RF path intact until the reply
  // has gone out. The board and initAntenna() select internal on next boot.
  externalAntenna = false;
  switchPending = false;
}
#endif
