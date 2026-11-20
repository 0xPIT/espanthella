#include <Arduino.h>
#include <pins_arduino.h>
#include "antenna.h"
#include <cassert>
#include <utility>

int main() {
  initAntenna();
  assert(!isExternalAntenna());
  assert(host::prefNamespace == "Antenna");
  assert(host::gpioWrites.size() == 2);
  assert(host::gpioWrites[0] == (std::pair<int, int>{WIFI_ENABLE, LOW}));
  assert(host::gpioWrites[1] == (std::pair<int, int>{WIFI_ANT_CONFIG, LOW}));

  assert(saveAntenna(true));
  assert(isExternalAntenna());
  assert(host::prefs["External"] == 1);
  assert(host::gpioWrites.size() == 2); // Let HTTP return before switching.
  host::now += 249;
  handleAntenna();
  assert(host::gpioWrites.size() == 2);
  ++host::now;
  handleAntenna();
  assert(host::gpioWrites.back() == (std::pair<int, int>{WIFI_ANT_CONFIG, HIGH}));

  host::gpioWrites.clear();
  initAntenna();
  assert(isExternalAntenna());
  assert(host::gpioWrites.back() == (std::pair<int, int>{WIFI_ANT_CONFIG, HIGH}));
  host::failNextPrefWrite = "External";
  assert(!saveAntenna(false));
  assert(isExternalAntenna());
  assert(host::prefs["External"] == 1);

  clearAntennaPreferences();
  assert(!isExternalAntenna());
  assert(host::prefs.empty());
  host::gpioWrites.clear();
  initAntenna();
  assert(host::gpioWrites.back() == (std::pair<int, int>{WIFI_ANT_CONFIG, LOW}));
}
