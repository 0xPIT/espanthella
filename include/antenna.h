#pragma once

// The RF switch exists only on the Seeed Studio XIAO ESP32C6.
#if defined(ARDUINO_XIAO_ESP32C6)
void initAntenna();
bool isExternalAntenna();
// Persist the selection; apply it after the HTTP response has been sent.
bool saveAntenna(bool external);
void handleAntenna();
void clearAntennaPreferences();
#endif
