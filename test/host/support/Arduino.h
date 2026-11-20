#pragma once
#include "host_runtime.h"
#include <cstddef>
#include <cstdint>

using portMUX_TYPE = unsigned;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE *) { ++host::criticalDepth; }
inline void portEXIT_CRITICAL(portMUX_TYPE *) {
  if (!host::criticalDepth) throw std::logic_error("unbalanced critical section");
  --host::criticalDepth;
}
inline uint32_t millis() { return host::now; }
constexpr int OUTPUT = 1;
constexpr int INPUT = 0;
constexpr int HIGH = 1;
constexpr int LOW = 0;
inline void pinMode(int pin, int mode) {
  host::record("gpio.mode");
  host::gpioModes.push_back({pin, mode});
}
inline void digitalWrite(int pin, int level) {
  host::record("gpio.write");
  host::gpioWrites.push_back({pin, level});
}
inline void delay(unsigned ms) { host::now += ms; }
inline void delayMicroseconds(unsigned) {}
inline int analogRead(int) { host::record("adc.read"); return 1000; }
inline void analogReadResolution(int) { host::record("adc.resolution"); }
inline uint32_t touchRead(int) { host::record("touch.read"); return 1000; }
inline void rgbLedWrite(int, uint8_t, uint8_t, uint8_t) { host::record("rgb.write"); }
struct HostSerial {
  template <class... Args> void printf(const char *, Args...) {}
};
inline HostSerial Serial;
