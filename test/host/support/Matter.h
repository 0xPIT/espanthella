#pragma once
#include "host_runtime.h"
#include <algorithm>
#include <functional>

// Only the endpoint surface used by light.cpp is modeled. Like Arduino Matter,
// begin() does not notify, updateAccessory() does, and setters notify only on
// changes without implicitly turning the light on. Callbacks use Matter's
// 1..254 CurrentLevel, not the public/UI 1..255 scale. This fake does NOT model
// a Matter server, transport, attribute transactions, or RTOS scheduling.
namespace chip::app::Clusters::ColorControl {
constexpr uint32_t Id = 0x0300;
namespace Attributes {
namespace ColorTempPhysicalMinMireds { constexpr uint32_t Id = 0x400b; }
namespace ColorTempPhysicalMaxMireds { constexpr uint32_t Id = 0x400c; }
namespace CoupleColorTempToLevelMinMireds { constexpr uint32_t Id = 0x400d; }
}  // namespace Attributes
}  // namespace chip::app::Clusters::ColorControl
struct esp_matter_attr_val_t { union { uint16_t u16; } val; };
inline esp_matter_attr_val_t esp_matter_uint16(uint16_t value) { return {{value}}; }

namespace host {
class Endpoint;
inline Endpoint *endpoint = nullptr;
inline bool endpointBeginSucceeds = true;
inline unsigned failAttributeCall = 0;

class Endpoint {
 public:
  const bool tunable;
  bool started = false;
  bool on = false;
  uint8_t level = 1;
  uint16_t temperature = 370;
  unsigned beginCalls = 0;
  unsigned attributeCalls = 0;
  unsigned updateCalls = 0;
  std::map<std::pair<uint32_t, uint32_t>, uint16_t> attributes;
  std::function<bool(bool, uint8_t, uint16_t)> callback;

  explicit Endpoint(bool supportsTemperature) : tunable(supportsTemperature) { endpoint = this; }
  bool begin(bool initialOn, uint8_t initialLevel, uint16_t initialTemperature) {
    ++beginCalls;
    started = endpointBeginSucceeds;
    on = initialOn;
    level = std::clamp<unsigned>(initialLevel, 1, 254);
    temperature = std::clamp<unsigned>(initialTemperature, 100, 500);
    return started;
  }
  bool notify(bool nextOn, uint8_t nextLevel, uint16_t nextTemperature) {
    if (!callback) return true;
    CallbackScope scope;
    return callback(nextOn, nextLevel, nextTemperature);
  }
  // Inject a controller snapshot at the registered callback boundary. Invalid
  // temperature values are intentional tests of the module's rejection path.
  bool controllerUpdate(bool nextOn, uint8_t nextLevel, uint16_t nextTemperature) {
    if (!started) return false;
    if (!notify(nextOn, nextLevel, nextTemperature)) return false;
    on = nextOn;
    level = nextLevel;
    temperature = nextTemperature;
    return true;
  }
  bool setOnOff(bool value) {
    if (!started) return false;
    return value == on || controllerUpdate(value, level, temperature);
  }
  bool setBrightness(uint8_t value) {
    if (!started) return false;
    const uint8_t clamped = std::clamp<unsigned>(value, 1, 254);
    return clamped == level || controllerUpdate(on, clamped, temperature);
  }
  bool setColorTemperature(uint16_t value) {
    if (!started) return false;
    const uint16_t clamped = std::clamp<unsigned>(value, 100, 500);
    return clamped == temperature || controllerUpdate(on, level, clamped);
  }
  void updateAccessory() {
    ++updateCalls;
    notify(on, level, temperature);
  }
  bool setAttributeVal(uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t *value) {
    ++attributeCalls;
    if (attributeCalls == failAttributeCall) return false;
    attributes[{cluster, attribute}] = value->val.u16;
    return true;
  }
};
}  // namespace host

class MatterDimmableLight : public host::Endpoint {
 public:
  MatterDimmableLight() : Endpoint(false) {}
  bool begin(bool on, uint8_t level) { return Endpoint::begin(on, level, 370); }
  void onChange(std::function<bool(bool, uint8_t)> cb) {
    callback = [cb](bool on, uint8_t level, uint16_t) { return cb(on, level); };
  }
};
class MatterColorTemperatureLight : public host::Endpoint {
 public:
  static constexpr uint16_t MIN_COLOR_TEMPERATURE = 100;
  static constexpr uint16_t MAX_COLOR_TEMPERATURE = 500;
  MatterColorTemperatureLight() : Endpoint(true) {}
  void onChange(std::function<bool(bool, uint8_t, uint16_t)> cb) { callback = cb; }
};
