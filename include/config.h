//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#pragma once
#include <cstdint>
#include "soc/soc_caps.h"
#include <pins_arduino.h>  // Board aliases such as XIAO D8/D9, including in build flags.

// ── Pins ──────────────────────────────────────────────────────────────
#if defined(ARDUINO_XIAO_ESP32C6)
constexpr uint8_t DEFAULT_PWM_PIN = D8;  // adjacent to D9 on the header
constexpr uint8_t PIN_BUTTON      = 9;   // onboard BOOT button
constexpr uint8_t PIN_TOUCH       = 4;   // unused when LIGHT_TOUCH_ENABLED=0
#elif defined(ARDUINO_ESP32C6_DEV)
constexpr uint8_t DEFAULT_PWM_PIN = 7;  // avoid GPIO 12/13 — USB D-/D+ on ESP32-C6
constexpr uint8_t PIN_BUTTON      = 9;  // GPIO9 on ESP32-C6-SuperMini
constexpr uint8_t PIN_TOUCH       = 4;
#elif defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t DEFAULT_PWM_PIN = 13;
constexpr uint8_t PIN_BUTTON      = 0;
constexpr uint8_t PIN_TOUCH       = 4;
#else
#error "Error: Pin configuration undefined for this chip type"
#endif

#ifndef LIGHT_WW_PIN
#define LIGHT_WW_PIN DEFAULT_PWM_PIN
#endif
#ifndef LIGHT_TOUCH_ENABLED
#define LIGHT_TOUCH_ENABLED 1
#endif
constexpr uint8_t PIN_PWM = LIGHT_WW_PIN;
static_assert(LIGHT_TOUCH_ENABLED == 0 || LIGHT_TOUCH_ENABLED == 1,
              "LIGHT_TOUCH_ENABLED must be 0 or 1");
static_assert(LIGHT_WW_PIN >= 0 && LIGHT_WW_PIN < SOC_GPIO_PIN_COUNT,
              "Assign LIGHT_WW_PIN to a valid output GPIO for your board");
static_assert(PIN_PWM != PIN_BUTTON && (!LIGHT_TOUCH_ENABLED || PIN_PWM != PIN_TOUCH),
              "PWM must not share the button or enabled touch GPIO");

// ── Light hardware ────────────────────────────────────────────────────
// Override these with build_flags for a WW/CW build. PIN_PWM drives the
// single LED, or WW; CW has no default GPIO and must be assigned explicitly.
#ifndef LIGHT_TUNABLE_WHITE
#define LIGHT_TUNABLE_WHITE 0
#endif
#ifndef LIGHT_CW_PIN
#define LIGHT_CW_PIN -1
#endif
#ifndef LIGHT_WARM_KELVIN
#define LIGHT_WARM_KELVIN 2700
#endif
#ifndef LIGHT_COOL_KELVIN
#define LIGHT_COOL_KELVIN 6500
#endif
#ifndef LIGHT_DEFAULT_KELVIN
#define LIGHT_DEFAULT_KELVIN LIGHT_WARM_KELVIN
#endif

constexpr int PIN_PWM_CW = LIGHT_CW_PIN;
constexpr uint16_t LIGHT_WARM_MIREDS = 1000000UL / LIGHT_WARM_KELVIN;
constexpr uint16_t LIGHT_COOL_MIREDS = 1000000UL / LIGHT_COOL_KELVIN;
constexpr uint16_t LIGHT_DEFAULT_MIREDS = 1000000UL / LIGHT_DEFAULT_KELVIN;

static_assert(LIGHT_TUNABLE_WHITE == 0 || LIGHT_TUNABLE_WHITE == 1,
              "LIGHT_TUNABLE_WHITE must be 0 or 1");
// The Arduino temperature endpoint clamps setters to 100..500 mireds.
static_assert(LIGHT_WARM_KELVIN >= 2000 && LIGHT_COOL_KELVIN <= 10000 &&
              LIGHT_WARM_KELVIN < LIGHT_COOL_KELVIN,
              "LED endpoints must be ordered within 2000..10000 K");
static_assert(LIGHT_DEFAULT_KELVIN >= LIGHT_WARM_KELVIN &&
              LIGHT_DEFAULT_KELVIN <= LIGHT_COOL_KELVIN,
              "Default temperature must be within the LED range");
#if LIGHT_TUNABLE_WHITE
static_assert(PIN_PWM_CW >= 0 && PIN_PWM_CW < SOC_GPIO_PIN_COUNT,
              "Assign LIGHT_CW_PIN to a valid output GPIO for your board");
static_assert(PIN_PWM_CW != PIN_PWM && PIN_PWM_CW != PIN_BUTTON &&
              (!LIGHT_TOUCH_ENABLED || PIN_PWM_CW != PIN_TOUCH),
              "CW PWM must not share the WW, button, or enabled touch GPIO");
#endif
#if defined(ARDUINO_ESP32C6_DEV) || defined(ARDUINO_XIAO_ESP32C6)
static_assert(PIN_PWM != 12 && PIN_PWM != 13 &&
              (!LIGHT_TUNABLE_WHITE || (PIN_PWM_CW != 12 && PIN_PWM_CW != 13)),
              "GPIO12/13 are reserved for USB on ESP32-C6");
#endif
#if defined(ARDUINO_XIAO_ESP32C6)
static_assert(PIN_PWM != WIFI_ENABLE && PIN_PWM != WIFI_ANT_CONFIG &&
              (!LIGHT_TUNABLE_WHITE || (PIN_PWM_CW != WIFI_ENABLE && PIN_PWM_CW != WIFI_ANT_CONFIG)),
              "GPIO3/14 are reserved for the XIAO ESP32C6 antenna switch");
#endif
static_assert(LIGHT_COOL_MIREDS < LIGHT_WARM_MIREDS,
              "LED endpoints must have distinct representable mired values");

// ── LEDC PWM ──────────────────────────────────────────────────────────
constexpr uint32_t PWM_FREQ       = 14550; // 19550;
constexpr uint8_t  PWM_RESOLUTION = 10;
constexpr uint16_t PWM_MAX_DUTY   = (1 << PWM_RESOLUTION) - 1;
constexpr uint16_t PWM_FADE_MS    = 20;

#if LIGHT_TOUCH_ENABLED
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
#endif  // LIGHT_TOUCH_ENABLED

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
constexpr const char* PREF_TEMPERATURE      = "Temperature";  // mireds
constexpr const char* PREF_SWAP_CW_WW       = "SwapCwWw";    // tunable-white output wiring
constexpr uint32_t LIGHT_SAVE_DELAY_MS      = 1000;
