//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#pragma once
#include <cstdint>

extern char deviceId[5];    // "A3F1"
extern char hostName[24];   // "espanthella-a3f1"
extern char apSsid[32];    // "espANTHELLA-A3F1"

void buildDeviceIdentity();
