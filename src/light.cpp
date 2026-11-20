//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "light.h"
#include "config.h"
#include "light_math.h"

#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>
#include <array>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#ifdef RGB_VERBOSE
// Arduino may encode the RGB LED as a virtual pin above the GPIO range.
static constexpr int RGB_GPIO = RGB_BUILTIN >= SOC_GPIO_PIN_COUNT
                                  ? RGB_BUILTIN - SOC_GPIO_PIN_COUNT : RGB_BUILTIN;
static_assert(PIN_PWM != RGB_GPIO && (!LIGHT_TUNABLE_WHITE || PIN_PWM_CW != RGB_GPIO),
              "PWM must not share the RGB debug LED GPIO");
#endif

#if LIGHT_TUNABLE_WHITE
static MatterColorTemperatureLight matterLight;
static_assert(LIGHT_COOL_MIREDS >= MatterColorTemperatureLight::MIN_COLOR_TEMPERATURE &&
              LIGHT_WARM_MIREDS <= MatterColorTemperatureLight::MAX_COLOR_TEMPERATURE,
              "LED range exceeds the Arduino temperature endpoint's supported range");
#else
static MatterDimmableLight matterLight;
#endif

static Preferences matterPref;
static bool lightReady = false;
static bool prefsEnabled = false;
static LightState savedState;
#if LIGHT_TUNABLE_WHITE
static bool swapCwWw = false;
#endif

// The Matter callback can run on another task. Only publish a snapshot there;
// loop() owns all PWM and Preferences calls, including deferred writes.
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
struct PendingLight {
  LightState state;
  uint32_t changedAt;
};
static PendingLight pendingLight = {{false, 15, LIGHT_DEFAULT_MIREDS}, 0};

static PendingLight readPendingLight() {
  portENTER_CRITICAL(&stateMux);
  PendingLight pending = pendingLight;
  portEXIT_CRITICAL(&stateMux);
  return pending;
}

LightState getLightState() {
  return readPendingLight().state;
}

bool supportsColorTemperature() {
  return LIGHT_TUNABLE_WHITE;
}

bool isCwWwSwapped() {
#if LIGHT_TUNABLE_WHITE
  return swapCwWw;
#else
  return false;
#endif
}

bool setCwWwSwapped([[maybe_unused]] bool swapped) {
#if LIGHT_TUNABLE_WHITE
  if (!lightReady || !prefsEnabled) return false;
  if (swapped == swapCwWw) return true;
  // Save first: a failed NVS write must not leave the UI claiming a choice
  // that will be lost on reboot.
  if (matterPref.putBool(PREF_SWAP_CW_WW, swapped) != 1) return false;
  swapCwWw = swapped;
  handleLight();  // Reassign the current duties even if Matter state did not change.
  return true;
#else
  return false;
#endif
}

// ── Native LEDC configuration ────────────────────────────────────────

static constexpr ledc_mode_t  LEDC_MODE  = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t LEDC_TIMER = LEDC_TIMER_0;

struct PwmChannel {
  int pin;
  ledc_channel_t channel;
  uint32_t lastDuty = UINT32_MAX;
};
static PwmChannel pwmChannels[] = {
  {PIN_PWM, LEDC_CHANNEL_0},
#if LIGHT_TUNABLE_WHITE
  {PIN_PWM_CW, LEDC_CHANNEL_1},
#endif
};

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

static bool checkLightError(esp_err_t err) {
  if (err == ESP_OK) return true;
  Serial.printf("Light driver error: %s\n", esp_err_to_name(err));
  return false;
}

static void setPwmDuty(PwmChannel &output, uint32_t duty) {
  if (duty == output.lastDuty) return;
#if SOC_LEDC_SUPPORT_FADE_STOP
  // C6 can replace an in-flight fade; older targets wait for its completion.
  if (!checkLightError(ledc_fade_stop(LEDC_MODE, output.channel))) return;
#endif
  if (!checkLightError(ledc_set_fade_with_time(LEDC_MODE, output.channel, duty, PWM_FADE_MS))) return;
  if (checkLightError(ledc_fade_start(LEDC_MODE, output.channel, LEDC_FADE_NO_WAIT))) {
    output.lastDuty = duty;
  }
}

// ── RGB LED (WS2812B) — mirrors brightness on boards with RGB_BUILTIN ─

#ifdef RGB_VERBOSE
static void setRgbLed(bool state, uint8_t brightness) {
  uint8_t v = state ? static_cast<uint8_t>(GAMMA_LUT[brightness] >> (PWM_RESOLUTION - 8)) : 0;
  rgbLedWrite(RGB_BUILTIN, v, v, v);
}
#endif

// ── Light state callback (called by Matter stack) ────────────────────

static bool queueLightState(bool on, uint8_t brightness, uint16_t temperature) {
  if (LIGHT_TUNABLE_WHITE && (temperature < LIGHT_COOL_MIREDS || temperature > LIGHT_WARM_MIREDS)) {
    return false;
  }
  const LightState state = {on, brightnessFromMatter(brightness), temperature};
  const uint32_t now = millis();
  portENTER_CRITICAL(&stateMux);
  if (   pendingLight.state.on != state.on
      || pendingLight.state.brightness != state.brightness
      || pendingLight.state.temperature != state.temperature)
  {
    pendingLight = {state, now};
  }
  portEXIT_CRITICAL(&stateMux);
  return true;
}

static void saveLightState(const LightState &state) {
  if (!prefsEnabled) return;
  if (state.on != savedState.on && matterPref.putBool(PREF_ONOFF, state.on)) {
    savedState.on = state.on;
  }
  if (state.brightness != savedState.brightness && matterPref.putUChar(PREF_BRIGHTNESS, state.brightness)) {
    savedState.brightness = state.brightness;
  }
#if LIGHT_TUNABLE_WHITE
  if (state.temperature != savedState.temperature && matterPref.putUShort(PREF_TEMPERATURE, state.temperature)) {
    savedState.temperature = state.temperature;
  }
#endif
}

void flushLightPrefs() {
  saveLightState(getLightState());
}

void handleLight() {
  if (!lightReady) return;
  const PendingLight pending = readPendingLight();
  const LightState &state = pending.state;
  const uint16_t totalDuty = state.on ? GAMMA_LUT[state.brightness] : 0;
  const LightDuties duties = mixLightDuty(totalDuty, state.temperature,
                                        LIGHT_COOL_MIREDS, LIGHT_WARM_MIREDS,
                                        supportsColorTemperature());
#if LIGHT_TUNABLE_WHITE
  // Swap the duty destinations, not Matter temperature or the configured GPIOs.
  setPwmDuty(pwmChannels[0], swapCwWw ? duties.cool : duties.warm);
  setPwmDuty(pwmChannels[1], swapCwWw ? duties.warm : duties.cool);
#else
  setPwmDuty(pwmChannels[0], duties.warm);
#endif
#ifdef RGB_VERBOSE
  static uint16_t lastRgb = UINT16_MAX;
  const uint16_t rgb = state.on ? state.brightness : 0;
  if (rgb != lastRgb) {
    setRgbLed(state.on, state.brightness);
    lastRgb = rgb;
  }
#endif
  if (millis() - pending.changedAt >= LIGHT_SAVE_DELAY_MS) {
    saveLightState(state);
  }
}

bool setLightOnOff(bool on) {
  return lightReady && matterLight.setOnOff(on);
}

bool setLightBrightness(uint8_t brightness) {
  if (!lightReady) return false;
  if (brightness == 0) return setLightOnOff(false);
  return matterLight.setBrightness(brightnessToMatter(brightness));
}

bool setLightTemperature([[maybe_unused]] uint16_t mireds) {
#if LIGHT_TUNABLE_WHITE
  return lightReady && matterLight.setColorTemperature(
    clampLightTemperature(mireds, LIGHT_COOL_MIREDS, LIGHT_WARM_MIREDS));
#else
  return false;
#endif
}

bool toggleLight() {
  return setLightOnOff(!getLightState().on);
}

void refreshLightState() {
  if (!lightReady) return;
  matterLight.updateAccessory();
  handleLight();
}

// ── Touch sensor — brightness cycle ──────────────────────────────────

#if LIGHT_TOUCH_ENABLED
static uint8_t  brightnessStep = 0;
static bool     touchActive    = false;
static uint32_t lastTouchTime  = 0;

static void onTouch() {
  brightnessStep = (brightnessStep + 1) % BRIGHTNESS_STEP_COUNT;
  uint8_t brightness = brightnessStep * 255 / (BRIGHTNESS_STEP_COUNT - 1);

  Serial.printf("Touch: step %u → brightness %u%%\n", brightnessStep, brightness * 100 / 255);

  if (brightnessStep == 0) {
    setLightOnOff(false);
  }
  else {
    setLightBrightness(brightness);
    setLightOnOff(true);
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

#endif  // SOC_TOUCH_SENSOR_SUPPORTED
#else
void handleTouch() {}  // No pin access, ADC sampling, or touch state when disabled.
#endif  // LIGHT_TOUCH_ENABLED

// ── Init / teardown ──────────────────────────────────────────────────

#if LIGHT_TUNABLE_WHITE
static bool configureTemperatureRange() {
  using namespace chip::app::Clusters;
  esp_matter_attr_val_t cool = esp_matter_uint16(LIGHT_COOL_MIREDS);
  esp_matter_attr_val_t warm = esp_matter_uint16(LIGHT_WARM_MIREDS);
  // These are read-only to controllers, but can be configured locally before
  // Matter.begin(). Also keep the level/temperature coupling minimum in range.
  return matterLight.setAttributeVal(ColorControl::Id,
           ColorControl::Attributes::ColorTempPhysicalMinMireds::Id, &cool) &&
         matterLight.setAttributeVal(ColorControl::Id,
           ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id, &warm) &&
         matterLight.setAttributeVal(ColorControl::Id,
           ColorControl::Attributes::CoupleColorTempToLevelMinMireds::Id, &cool);
}
#endif

bool initLight() {
  // Validate output capability as well as pin numbers before touching GPIOs.
  for (const PwmChannel &output : pwmChannels) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(output.pin)) {
      Serial.printf("Invalid PWM output GPIO: %d\n", output.pin);
      return false;
    }
  }
  ledc_timer_config_t timer_conf = {
    .speed_mode      = LEDC_MODE,
    .duty_resolution = static_cast<ledc_timer_bit_t>(PWM_RESOLUTION),
    .timer_num       = LEDC_TIMER,
    .freq_hz         = PWM_FREQ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  if (!checkLightError(ledc_timer_config(&timer_conf))) return false;

  for (const PwmChannel &output : pwmChannels) {
    ledc_channel_config_t channel_conf = {
      .gpio_num   = output.pin,
      .speed_mode = LEDC_MODE,
      .channel    = output.channel,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER,
      .duty       = 0,
      .hpoint     = 0,
    };
    if (!checkLightError(ledc_channel_config(&channel_conf))) return false;
  }
  if (!checkLightError(ledc_fade_func_install(0))) return false;
#if LIGHT_TOUCH_ENABLED
#if SOC_TOUCH_SENSOR_SUPPORTED
  touchRead(PIN_TOUCH);        // prime the native touch peripheral
#else
  touchCalibrate();            // measure charge-decay baseline (no touch)
#endif
#endif

  if (!matterPref.begin(MATTER_PREF_NAMESPACE, false)) return false;
  prefsEnabled = true;
  savedState = {matterPref.getBool(PREF_ONOFF, true),
                matterPref.getUChar(PREF_BRIGHTNESS, 15),
                matterPref.getUShort(PREF_TEMPERATURE, LIGHT_DEFAULT_MIREDS)};
#if LIGHT_TUNABLE_WHITE
  swapCwWw = matterPref.getBool(PREF_SWAP_CW_WW, false);
#endif
  const bool on = savedState.on && savedState.brightness != 0;
  const uint8_t brightness = brightnessToMatter(savedState.brightness ? savedState.brightness : 15);
#if LIGHT_TUNABLE_WHITE
  const uint16_t temperature = clampLightTemperature(savedState.temperature,
                                                    LIGHT_COOL_MIREDS, LIGHT_WARM_MIREDS);
  if (!matterLight.begin(on, brightness, temperature) || !configureTemperatureRange()) return false;
  matterLight.onChange(queueLightState);
#else
  if (!matterLight.begin(on, brightness)) return false;
  matterLight.onChange([](bool on, uint8_t brightness) {
    return queueLightState(on, brightness, LIGHT_DEFAULT_MIREDS);
  });
#endif
  lightReady = true;
  return true;
}

void clearLightPrefs() {
  // Factory reset must not allow a queued callback to repopulate preferences.
  prefsEnabled = false;
  matterPref.clear();
#if LIGHT_TUNABLE_WHITE
  swapCwWw = false;
#endif
}
