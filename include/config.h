//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#pragma once
#include <cstdint>
#include "soc/soc_caps.h"

// ── Pins ──────────────────────────────────────────────────────────────
#ifdef ARDUINO_ESP32C6_DEV
constexpr uint8_t PIN_PWM     = 7;  // avoid GPIO 12/13 — USB D-/D+ on ESP32-C6
constexpr uint8_t PIN_BUTTON  = 9;  // GPIO9 on ESP32-C6-SuperMini
constexpr uint8_t PIN_TOUCH   = 4;
#elifdef DARDUINO_ESP32S3_DEV
constexpr uint8_t PIN_PWM     = 13;
constexpr uint8_t PIN_BUTTON  = 0;
constexpr uint8_t PIN_TOUCH   = 4;
#else
#error "Error: Pin configuration undefined for this chip type"
#endif

// ── LEDC PWM ──────────────────────────────────────────────────────────
constexpr uint32_t PWM_FREQ       = 19550;
constexpr uint8_t  PWM_RESOLUTION = 10;
constexpr uint16_t PWM_MAX_DUTY   = (1 << PWM_RESOLUTION) - 1;
constexpr uint16_t PWM_FADE_MS    = 20;

#if SOC_TOUCH_SENSOR_SUPPORTED
// ── Touch — ESP32-S3 values INCREASE on contact ──────────────────────
constexpr uint32_t TOUCH_THRESHOLD   = 60000;
#else
// ── Touch — ADC charge-decay sensing (ESP32-C6 etc.) ─────────────────
constexpr uint16_t TOUCH_SAMPLES     = 32;     // ADC reads per measurement
constexpr uint16_t TOUCH_SETTLE_US   = 50;     // decay time after release (µs)
constexpr float    TOUCH_MULTIPLIER  = 1.3f;   // trigger when reading > baseline × this
constexpr uint16_t TOUCH_CAL_READS   = 64;     // baseline calibration samples at boot
#endif

constexpr uint32_t TOUCH_DEBOUNCE_MS = 300;

// ── Brightness steps: off → evenly spaced along gamma curve → 100% ──
constexpr uint8_t  BRIGHTNESS_STEP_COUNT = 4;
constexpr float    BRIGHTNESS_GAMMA      = 1.9f;

// ── WiFi provisioning ────────────────────────────────────────────────
constexpr const char* WIFI_AP_PASSWORD       = nullptr;
constexpr uint32_t    WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr const char* WIFI_PREF_NAMESPACE    = "WiFiCreds";

// ── Matter persistence ───────────────────────────────────────────────
constexpr const char* MATTER_PREF_NAMESPACE = "MatterPrefs";
constexpr const char* PREF_ONOFF            = "OnOff";
constexpr const char* PREF_BRIGHTNESS       = "Brightness";
