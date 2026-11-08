//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "identity.h"
#include <Arduino.h>

char deviceId[5];
char hostName[24];
char apSsid[32];

void buildDeviceIdentity() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t id  = static_cast<uint16_t>(mac >> 32);
  snprintf(deviceId, sizeof(deviceId), "%04X", id);
  snprintf(hostName, sizeof(hostName), "espanthella-%04x", id);
  snprintf(apSsid,   sizeof(apSsid),  "espANTHELLA-%s", deviceId);
}
