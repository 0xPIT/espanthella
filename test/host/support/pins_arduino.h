#pragma once
#include <cstdint>

// Only the aliases used by this firmware are needed by the host fake.
// The firmware build resolves them using Arduino's real board variant.
#ifdef ARDUINO_XIAO_ESP32C6
constexpr uint8_t D8 = 19;
constexpr uint8_t D9 = 20;
constexpr uint8_t WIFI_ENABLE = 3;
constexpr uint8_t WIFI_ANT_CONFIG = 14;
#endif
