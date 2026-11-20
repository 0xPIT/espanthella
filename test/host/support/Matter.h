#pragma once
#include "host_runtime.h"
#include "esp_err.h"
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
namespace ColorTemperatureMireds { constexpr uint32_t Id = 0x0007; }
namespace ColorTempPhysicalMinMireds { constexpr uint32_t Id = 0x400b; }
namespace ColorTempPhysicalMaxMireds { constexpr uint32_t Id = 0x400c; }
namespace CoupleColorTempToLevelMinMireds { constexpr uint32_t Id = 0x400d; }
}  // namespace Attributes
}  // namespace chip::app::Clusters::ColorControl
namespace chip::app::Clusters::OnOff {
constexpr uint32_t Id = 0x0006;
namespace Attributes { namespace OnOff { constexpr uint32_t Id = 0x0000; } }
}
namespace chip::app::Clusters::LevelControl {
constexpr uint32_t Id = 0x0008;
namespace Attributes { namespace CurrentLevel { constexpr uint32_t Id = 0x0000; } }
}
struct esp_matter_attr_val_t { union { uint32_t u32; uint16_t u16; uint8_t u8; bool b; } val; };
namespace esp_matter::attribute {
inline unsigned reports = 0;
inline bool failNextReport = false;
inline esp_err_t report(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *);
}
inline esp_matter_attr_val_t esp_matter_invalid(void *) { return {{0}}; }
inline esp_matter_attr_val_t esp_matter_bool(bool value) { return {{uint32_t(value)}}; }
inline esp_matter_attr_val_t esp_matter_nullable_uint8(uint8_t value) { return {{value}}; }
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
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> reported;
  unsigned reportedUpdates = 0;
  bool failNextAttributeRead = false;
  std::function<bool(bool, uint8_t, uint16_t)> callback;

  explicit Endpoint(bool supportsTemperature) : tunable(supportsTemperature) { endpoint = this; }
  bool begin(bool initialOn, uint8_t initialLevel, uint16_t initialTemperature) {
    ++beginCalls;
    started = endpointBeginSucceeds;
    on = initialOn;
    level = std::clamp<unsigned>(initialLevel, 1, 254);
    temperature = std::clamp<unsigned>(initialTemperature, 100, 500);
    using namespace chip::app::Clusters;
    reported[{OnOff::Id, OnOff::Attributes::OnOff::Id}] = on;
    reported[{LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id}] = level;
    if (tunable) reported[{ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id}] = temperature;
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
    using namespace chip::app::Clusters;
    reported[{OnOff::Id, OnOff::Attributes::OnOff::Id}] = on;
    reported[{LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id}] = level;
    if (tunable) reported[{ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id}] = temperature;
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
  uint16_t getEndPointId() const { return 1; }
  bool getOnOff() const { return on; }
  uint8_t getBrightness() const { return level; }
  uint16_t getColorTemperature() const { return temperature; }
  bool getAttributeVal(uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t *value) {
    if (failNextAttributeRead) { failNextAttributeRead = false; return false; }
    const auto it = reported.find({cluster, attribute});
    if (it == reported.end()) return false;
    value->val.u32 = it->second;
    return true;
  }
  bool updateAttributeVal(uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t *value) {
    ++reportedUpdates;
    reported[{cluster, attribute}] = value->val.u32;
    // ESP-Matter's PRE_UPDATE callback also updates the endpoint cache.
    using namespace chip::app::Clusters;
    if (cluster == OnOff::Id) on = value->val.b;
    if (cluster == LevelControl::Id) level = value->val.u8;
    if (cluster == ColorControl::Id && attribute == ColorControl::Attributes::ColorTemperatureMireds::Id) {
      temperature = value->val.u16;
    }
    notify(on, level, temperature);
    return true;
  }
  bool setAttributeVal(uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t *value) {
    ++attributeCalls;
    if (attributeCalls == failAttributeCall) return false;
    attributes[{cluster, attribute}] = value->val.u16;
    return true;
  }
};
}  // namespace host

namespace esp_matter::attribute {
inline esp_err_t report(uint16_t, uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t *value) {
  host::record("matter.report");
  if (failNextReport) { failNextReport = false; return ESP_FAIL; }
  if (!host::endpoint || !host::endpoint->started) return ESP_FAIL;
  ++reports;
  host::endpoint->reported[{cluster, attribute}] = value->val.u32;
  return ESP_OK;
}
}

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
