#pragma once
#include <cstdint>

// The public/UI brightness scale is 0..255; lighting CurrentLevel is 1..254.
// Zero is handled as off by the public setter, not written to CurrentLevel.
constexpr uint8_t brightnessToMatter(uint8_t brightness) {
  return brightness < 2 ? 1 : (uint16_t(brightness) * 254 + 127) / 255;
}

constexpr uint8_t brightnessFromMatter(uint8_t level) {
  return level < 1 ? 1 : level > 254 ? 255 : (uint16_t(level) * 255 + 127) / 254;
}

constexpr uint16_t clampLightTemperature(uint16_t value, uint16_t cool, uint16_t warm) {
  return value < cool ? cool : value > warm ? warm : value;
}

struct LightDuties {
  uint16_t warm;
  uint16_t cool;
};

// Split AFTER gamma correction. Preserve the total duty exactly, even at
// low brightness. Mired interpolation is an approximation, not calibration.
constexpr LightDuties mixLightDuty(uint16_t total, uint16_t temperature,
                                  uint16_t cool, uint16_t warm, bool tunable) {
  if (!tunable || warm <= cool) return {total, 0};
  const uint16_t clamped = clampLightTemperature(temperature, cool, warm);
  const uint32_t span = warm - cool;
  const uint16_t coolDuty = (uint32_t(total) * (warm - clamped) + span / 2) / span;
  return {uint16_t(total - coolDuty), coolDuty};
}
