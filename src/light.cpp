//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "light.h"
#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include "driver/ledc.h"
#include "esp_err.h"

MatterDimmableLight DimmableLight;

static Preferences matterPref;

// ── Native LEDC configuration ────────────────────────────────────────

static constexpr ledc_mode_t    LEDC_MODE    = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t   LEDC_TIMER   = LEDC_TIMER_0;
static constexpr ledc_channel_t LEDC_CHANNEL = LEDC_CHANNEL_0;

// ── Gamma correction LUT (compile-time) ─────────────────────────────
// test resulting curves at https://victornpb.github.io/gamma-table-generator/
//
template <uint16_t Steps>
static constexpr std::array<uint16_t, Steps> makeGammaLUT(float gamma, uint16_t resolution) {
  std::array<uint16_t, Steps> lut{};
  float maxVal = __builtin_powf(Steps - 1, gamma);
  for (uint16_t i = 0; i < Steps; i++) {
    float normalized = __builtin_powf(i, gamma) / maxVal;
    uint16_t duty = static_cast<uint16_t>(__builtin_roundf(normalized * resolution));
    lut[i] = (duty < resolution) ? duty : resolution;
  }
  return lut;
}

static constexpr auto GAMMA_LUT = makeGammaLUT<256>(BRIGHTNESS_GAMMA, PWM_MAX_DUTY);

// ── PWM output ───────────────────────────────────

static uint32_t lastPwmDuty = UINT32_MAX;

static void setPwmDuty(uint32_t duty) {
  if (duty == lastPwmDuty) return;
  lastPwmDuty = duty;
  ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, duty, PWM_FADE_MS);
  ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT);
}

// ── RGB LED (WS2812B) — mirrors brightness on boards with RGB_BUILTIN ─

#ifdef RGB_VERBOSE
static void setRgbLed(bool state, uint8_t brightness) {
  uint8_t v = state ? static_cast<uint8_t>(GAMMA_LUT[brightness] >> (PWM_RESOLUTION - 8)) : 0;
  rgbLedWrite(RGB_BUILTIN, v, v, v);
}
#endif

// ── Light state callback (called by Matter stack) ────────────────────

bool setLightState(bool state, uint8_t brightness) {
  uint32_t duty = state ? GAMMA_LUT[brightness] : 0;

  setPwmDuty(duty);
#ifdef RGB_VERBOSE
  setRgbLed(state, brightness);
#endif

  matterPref.putBool(PREF_ONOFF, state);
  matterPref.putUChar(PREF_BRIGHTNESS, brightness);
  return true;
}

// ── Touch sensor — brightness cycle ──────────────────────────────────

static uint8_t  brightnessStep = 0;
static bool     touchActive    = false;
static uint32_t lastTouchTime  = 0;

static void onTouch() {
  brightnessStep = (brightnessStep + 1) % BRIGHTNESS_STEP_COUNT;
  uint8_t brightness = brightnessStep * 255 / (BRIGHTNESS_STEP_COUNT - 1);

  Serial.printf("Touch: step %u → brightness %u%%\n", brightnessStep, brightness * 100 / 255);

  if (brightnessStep == 0) {
    DimmableLight.setOnOff(false);
  }
  else {
    DimmableLight.setOnOff(true);
    DimmableLight.setBrightness(brightness);
  }
}

#if SOC_TOUCH_SENSOR_SUPPORTED
// ── Native capacitive touch (ESP32-S3 etc.) ─────────────────────────

void handleTouch() {
  uint32_t val = touchRead(PIN_TOUCH);
  bool touched = val > TOUCH_THRESHOLD;

  if (touched && !touchActive && (millis() - lastTouchTime > TOUCH_DEBOUNCE_MS)) {
    touchActive   = true;
    lastTouchTime = millis();
    onTouch();
  }

  if (!touched) {
    touchActive = false;
  }
}

#else
// ── ADC charge-decay capacitive sensing (ESP32-C6 etc.) ─────────────
// 1. Drive the pad HIGH (charge the pad capacitance)
// 2. Release to high-Z analog input
// 3. Wait a fixed decay interval — charge leaks through ADC input impedance
// 4. Read ADC — more capacitance (finger) retains more charge → higher value
//
// No pull-up resistor needed.  The ADC gives 12-bit resolution (0–4095),
// far more dynamic range than the digital charge-time method.

static uint16_t touchBaseline = 0;

// Returns the average ADC reading after charge-decay (higher = more capacitance)
static uint16_t touchSenseRaw() {
  uint32_t total = 0;
  for (uint16_t s = 0; s < TOUCH_SAMPLES; s++) {
    // Charge the pad to Vdd
    pinMode(PIN_TOUCH, OUTPUT);
    digitalWrite(PIN_TOUCH, HIGH);
    delayMicroseconds(1);

    // Release to high-Z, let charge decay through ADC input impedance
    pinMode(PIN_TOUCH, INPUT);
    delayMicroseconds(TOUCH_SETTLE_US);

    // Sample the remaining voltage
    total += analogRead(PIN_TOUCH);
  }

  return static_cast<uint16_t>(total / TOUCH_SAMPLES);
}

static void touchCalibrate() {
  analogReadResolution(12);
  Serial.printf("Touch calibrating (%u × %u samples)…\n", TOUCH_CAL_READS, TOUCH_SAMPLES);
  uint32_t sum = 0;
  uint16_t minVal = UINT16_MAX, maxVal = 0;
  for (uint16_t i = 0; i < TOUCH_CAL_READS; i++) {
    uint16_t v = touchSenseRaw();
    sum += v;
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }
  touchBaseline = static_cast<uint16_t>(sum / TOUCH_CAL_READS);
  if (touchBaseline < 1) touchBaseline = 1;
  Serial.printf("Touch calibrated: baseline=%u, min=%u, max=%u, threshold=%u (×%.1f)\n",
                touchBaseline, minVal, maxVal,
                static_cast<uint16_t>(touchBaseline * TOUCH_MULTIPLIER),
                (double)TOUCH_MULTIPLIER);
}

void handleTouch() {
  uint16_t val = touchSenseRaw();
  uint16_t threshold = static_cast<uint16_t>(touchBaseline * TOUCH_MULTIPLIER);
  bool touched = val < threshold / 2;

  if (touched && !touchActive) {
    Serial.printf("Touch detected: val=%u, threshold=%u, baseline=%u\n",
                  val, threshold, touchBaseline);
  }

  if (touched && !touchActive && (millis() - lastTouchTime > TOUCH_DEBOUNCE_MS)) {
    touchActive   = true;
    lastTouchTime = millis();
    Serial.printf("Touch accepted (debounce ok)\n");
    onTouch();
  }

  if (!touched && touchActive) {
    Serial.printf("Touch released: val=%u\n", val);
  }

  if (!touched) {
    touchActive = false;
  }
}

#endif

// ── Init / teardown ──────────────────────────────────────────────────

void initLight() {
  ledc_timer_config_t timer_conf = {
    .speed_mode      = LEDC_MODE,
    .duty_resolution = static_cast<ledc_timer_bit_t>(PWM_RESOLUTION),
    .timer_num       = LEDC_TIMER,
    .freq_hz         = PWM_FREQ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer_conf);

  ledc_channel_config_t channel_conf = {
    .gpio_num   = PIN_PWM,
    .speed_mode = LEDC_MODE,
    .channel    = LEDC_CHANNEL,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = LEDC_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ledc_channel_config(&channel_conf);

  ledc_fade_func_install(0);
#if SOC_TOUCH_SENSOR_SUPPORTED
  touchRead(PIN_TOUCH);        // prime the native touch peripheral
#else
  touchCalibrate();            // measure charge-time baseline (no touch)
#endif

  matterPref.begin(MATTER_PREF_NAMESPACE, false);
  bool    lastOnOff      = matterPref.getBool(PREF_ONOFF, true);
  uint8_t lastBrightness = matterPref.getUChar(PREF_BRIGHTNESS, 15);

  DimmableLight.begin(lastOnOff, lastBrightness);
  DimmableLight.onChange(setLightState);
#ifdef RGB_VERBOSE
  setRgbLed(lastOnOff, lastBrightness);
#endif

  DimmableLight.onChangeOnOff([](bool state) {
    Serial.printf("Light OnOff → %s\r\n", state ? "ON" : "OFF");
    return true;
  });
  DimmableLight.onChangeBrightness([](uint8_t level) {
    Serial.printf("Light Brightness → %u\r\n", level);
    return true;
  });
}

void clearLightPrefs() {
  matterPref.clear();
}
