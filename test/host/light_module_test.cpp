// Run: python3 scripts/test_light_host.py
// Each named case runs in a fresh process so production file-static state is
// genuinely initialized once, without a test-only reset API or including .cpp.
#include "light.h"
#include "config.h"
#include "Matter.h"
#include "test_assert.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr bool tunable = LIGHT_TUNABLE_WHITE;
constexpr unsigned channelCount = tunable ? 2 : 1;

unsigned matterLevel(unsigned publicLevel) {
  return std::max(1L, std::lround(publicLevel * (254.0 / 255.0)));
}
unsigned publicLevel(unsigned level) {
  return std::lround(std::clamp(level, 1u, 254u) * (255.0 / 254.0));
}
void checkState(bool on, unsigned brightness, unsigned temperature) {
  const auto state = getLightState();
  CHECK_EQ(state.on, on);
  CHECK_EQ(state.brightness, brightness);
  CHECK_EQ(state.temperature, temperature);
  CHECK_EQ(host::criticalDepth, 0);
}
void seed(bool on, unsigned brightness, unsigned temperature = LIGHT_DEFAULT_MIREDS) {
  host::prefs = {{PREF_ONOFF, on}, {PREF_BRIGHTNESS, brightness}, {PREF_TEMPERATURE, temperature}};
}
void boot(bool on = true, unsigned brightness = 15, unsigned temperature = LIGHT_DEFAULT_MIREDS) {
  seed(on, brightness, temperature);
  CHECK(initLight());
  refreshLightState();
  flushLightPrefs();
  host::clearObservations();
}
uint32_t duty(int channel) {
  CHECK(host::duties.count(channel));
  return host::duties.at(channel);
}
void checkOutput() {
  const auto state = getLightState();
  const unsigned warm = duty(0);
  const unsigned cool = tunable ? duty(1) : 0;
  const double expectedTotal = state.on ? std::pow(state.brightness / 255.0, BRIGHTNESS_GAMMA) * PWM_MAX_DUTY : 0;
  // Float LUT construction may differ by one count from a double oracle.
  CHECK(std::abs(double(warm + cool) - std::round(expectedTotal)) <= 1);
  CHECK(warm <= PWM_MAX_DUTY && cool <= PWM_MAX_DUTY);
  if (!state.on) CHECK_EQ(warm + cool, 0);
  if (state.on && state.brightness == 255) CHECK_EQ(warm + cool, PWM_MAX_DUTY);
  if (!tunable || (state.temperature == LIGHT_WARM_MIREDS && !isCwWwSwapped()) ||
      (tunable && state.temperature == LIGHT_COOL_MIREDS && isCwWwSwapped())) CHECK_EQ(cool, 0);
  if (tunable && ((state.temperature == LIGHT_COOL_MIREDS && !isCwWwSwapped()) ||
                  (state.temperature == LIGHT_WARM_MIREDS && isCwWwSwapped()))) CHECK_EQ(warm, 0);
  for (const auto &fade : host::fades) CHECK_EQ(fade.milliseconds, PWM_FADE_MS);
}
void controller(bool on, unsigned level, unsigned temperature = LIGHT_DEFAULT_MIREDS) {
  const auto ioBefore = host::io.size();
  CHECK(host::endpoint->controllerUpdate(on, level, temperature));
  CHECK_EQ(host::io.size(), ioBefore);
  CHECK_EQ(host::callbackDepth, 0);
}
void expectOnlyWrite(const char *key, unsigned value) {
  CHECK_EQ(host::writes.size(), 1);
  CHECK(host::writes.front().key == key);
  CHECK_EQ(host::writes.front().value, value);
  CHECK_EQ(host::prefs.at(key), value);
}

void beforeInit() {
  CHECK_EQ(supportsColorTemperature(), tunable);
  CHECK(!setLightOnOff(true));
  CHECK(!setLightBrightness(0));
  CHECK(!setLightBrightness(255));
  CHECK(!setLightTemperature(LIGHT_DEFAULT_MIREDS));
  CHECK(!toggleLight());
  handleLight();
  refreshLightState();
  flushLightPrefs();
  CHECK(host::io.empty());
  CHECK(getLightState().brightness >= 1);
}

void initialization() {
  CHECK(initLight());
  CHECK_EQ(supportsColorTemperature(), tunable);
  CHECK_EQ(host::endpoint->tunable, tunable);
  CHECK_EQ(host::endpoint->beginCalls, 1);
  CHECK(host::endpoint->started);
  CHECK(bool(host::endpoint->callback));
  CHECK_EQ(host::endpoint->on, true);
  CHECK_EQ(host::endpoint->level, matterLevel(15));
  CHECK(host::prefNamespace == MATTER_PREF_NAMESPACE);
  CHECK(!host::prefsReadOnly);
  CHECK(std::find(host::reads.begin(), host::reads.end(), PREF_ONOFF) != host::reads.end());
  CHECK(std::find(host::reads.begin(), host::reads.end(), PREF_BRIGHTNESS) != host::reads.end());
  CHECK_EQ(host::timerCalls, 1);
  CHECK_EQ(host::timerFrequency, PWM_FREQ);
  CHECK_EQ(host::timerResolution, PWM_RESOLUTION);
  CHECK_EQ(host::fadeInstallCalls, 1);
  CHECK_EQ(host::channels.size(), channelCount);
  CHECK_EQ(host::channels[0].pin, PIN_PWM);
  CHECK_EQ(host::channels[0].channel, 0);
  for (const auto &channel : host::channels) CHECK_EQ(channel.initialDuty, 0);
  CHECK(host::fades.empty());
  CHECK(host::writes.empty());
  if (tunable) {
    CHECK_EQ(host::channels[1].pin, PIN_PWM_CW);
    CHECK_EQ(host::channels[1].channel, 1);
    CHECK_EQ(host::endpoint->temperature, LIGHT_DEFAULT_MIREDS);
    using namespace chip::app::Clusters;
    const auto &attrs = host::endpoint->attributes;
    CHECK_EQ(attrs.size(), 3);
    CHECK_EQ(attrs.at({ColorControl::Id, ColorControl::Attributes::ColorTempPhysicalMinMireds::Id}), LIGHT_COOL_MIREDS);
    CHECK_EQ(attrs.at({ColorControl::Id, ColorControl::Attributes::ColorTempPhysicalMaxMireds::Id}), LIGHT_WARM_MIREDS);
    CHECK_EQ(attrs.at({ColorControl::Id, ColorControl::Attributes::CoupleColorTempToLevelMinMireds::Id}), LIGHT_COOL_MIREDS);
  } else {
    CHECK(host::endpoint->attributes.empty());
    CHECK(!setLightTemperature(0));
    CHECK(!setLightTemperature(65535));
  }
  host::clearObservations();
  refreshLightState();
  CHECK_EQ(host::endpoint->updateCalls, 1);
  checkState(true, publicLevel(matterLevel(15)), LIGHT_DEFAULT_MIREDS);
  checkOutput();
  CHECK_EQ(host::fades.size(), channelCount);
  CHECK(host::writes.empty());
  host::clearObservations();
  handleLight();
  CHECK(host::io.empty());  // no duplicate fades or early NVS writes
}

void touchConfiguration() {
  CHECK(initLight());
  const auto isTouchIO = [](const std::string &op) {
    return op.rfind("gpio.", 0) == 0 || op.rfind("adc.", 0) == 0 || op.rfind("touch.", 0) == 0;
  };
#if LIGHT_TOUCH_ENABLED
  CHECK(std::any_of(host::io.begin(), host::io.end(), isTouchIO));
#else
  CHECK(std::none_of(host::io.begin(), host::io.end(), isTouchIO));
#endif
  host::clearObservations();
  handleTouch();
#if LIGHT_TOUCH_ENABLED
  CHECK(std::any_of(host::io.begin(), host::io.end(), isTouchIO));
#else
  CHECK(host::io.empty());
#endif
}

void queuedCallbacks() {
  boot();
  const auto oldDuties = host::duties;
  controller(true, 254, LIGHT_COOL_MIREDS);
  checkState(true, 255, tunable ? LIGHT_COOL_MIREDS : LIGHT_DEFAULT_MIREDS);
  CHECK(host::duties == oldDuties);
  CHECK(host::fades.empty());
  CHECK(host::writes.empty());
  handleLight();
  checkOutput();
  CHECK(!host::fades.empty());
  CHECK(host::writes.empty());
  host::clearObservations();
  controller(false, 254, LIGHT_COOL_MIREDS);
  controller(true, 80, LIGHT_DEFAULT_MIREDS);
  controller(true, 160, LIGHT_WARM_MIREDS);
  CHECK(host::fades.empty());
  handleLight();
  checkState(true, publicLevel(160), tunable ? LIGHT_WARM_MIREDS : LIGHT_DEFAULT_MIREDS);
  checkOutput();
  CHECK(host::fades.size() <= channelCount);  // intermediate states coalesced
  host::clearObservations();
  controller(true, 160, LIGHT_WARM_MIREDS);
  handleLight();
  CHECK(host::io.empty());
}

void brightnessAndFullScale() {
  boot();
  unsigned previousTotal = 0;
  for (unsigned requested = 1; requested <= 255; ++requested) {
    const auto ioBefore = host::io.size();
    CHECK(setLightBrightness(requested));
    CHECK_EQ(host::io.size(), ioBefore);
    CHECK_EQ(host::endpoint->level, matterLevel(requested));
    CHECK(host::endpoint->level >= 1 && host::endpoint->level <= 254);
    CHECK(getLightState().on);
    CHECK(std::abs(int(getLightState().brightness) - int(requested)) <= 1);
    handleLight();
    checkOutput();
    const unsigned total = duty(0) + (tunable ? duty(1) : 0);
    CHECK(total >= previousTotal);
    previousTotal = total;
  }
  CHECK_EQ(getLightState().brightness, 255);
  CHECK_EQ(previousTotal, PWM_MAX_DUTY);
  host::clearObservations();
  CHECK(setLightBrightness(0));
  CHECK(host::io.empty());
  checkState(false, 255, LIGHT_DEFAULT_MIREDS);
  CHECK_EQ(host::endpoint->level, 254);
  handleLight();
  checkOutput();
  CHECK(setLightBrightness(0));
  CHECK_EQ(getLightState().brightness, 255);
  CHECK(toggleLight());
  CHECK(getLightState().on);
  handleLight();
  checkOutput();
  CHECK_EQ(duty(0) + (tunable ? duty(1) : 0), PWM_MAX_DUTY);
}

void changesWhileOff() {
  boot(false, 75);
  CHECK(setLightBrightness(180));
  CHECK(host::io.empty());
  CHECK(!getLightState().on);
  CHECK_EQ(getLightState().brightness, publicLevel(matterLevel(180)));
  if (tunable) {
    CHECK(setLightTemperature(0));
    CHECK_EQ(getLightState().temperature, LIGHT_COOL_MIREDS);
    CHECK(!getLightState().on);
    CHECK(setLightTemperature(65535));
    CHECK_EQ(getLightState().temperature, LIGHT_WARM_MIREDS);
    const unsigned midpoint = (LIGHT_COOL_MIREDS + LIGHT_WARM_MIREDS) / 2;
    CHECK(setLightTemperature(midpoint));
    CHECK_EQ(getLightState().temperature, midpoint);
  } else {
    CHECK(!setLightTemperature(0));
    CHECK(!setLightTemperature(65535));
    CHECK_EQ(getLightState().temperature, LIGHT_DEFAULT_MIREDS);
  }
  CHECK(host::io.empty());
  handleLight();
  checkOutput();
  CHECK(host::writes.empty());
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  CHECK_EQ(host::writes.size(), tunable ? 2 : 1);
  for (const auto &write : host::writes) CHECK(write.key != PREF_ONOFF);
  CHECK(setLightOnOff(true));
  handleLight();
  checkOutput();
  if (tunable) CHECK(duty(0) > 0 && duty(1) > 0);
}

[[maybe_unused]] void temperatureRangeAndPower() {
  boot(true, 255);
  CHECK(setLightTemperature(0));
  CHECK_EQ(host::endpoint->temperature, LIGHT_COOL_MIREDS);
  CHECK_EQ(getLightState().temperature, LIGHT_COOL_MIREDS);
  CHECK(host::io.empty());
  handleLight();
  CHECK_EQ(duty(0), 0);
  CHECK_EQ(duty(1), PWM_MAX_DUTY);
  unsigned previousWarm = 0;
  for (unsigned temperature = LIGHT_COOL_MIREDS; temperature <= LIGHT_WARM_MIREDS; ++temperature) {
    CHECK(setLightTemperature(temperature));
    handleLight();
    CHECK_EQ(getLightState().temperature, temperature);
    CHECK_EQ(duty(0) + duty(1), PWM_MAX_DUTY);
    CHECK(duty(0) >= previousWarm);
    previousWarm = duty(0);
  }
  CHECK(setLightTemperature(65535));
  CHECK_EQ(getLightState().temperature, LIGHT_WARM_MIREDS);
  handleLight();
  CHECK_EQ(duty(0), PWM_MAX_DUTY);
  CHECK_EQ(duty(1), 0);
  const auto before = getLightState();
  host::clearObservations();
  CHECK(!host::endpoint->controllerUpdate(false, 20, LIGHT_COOL_MIREDS - 1));
  CHECK(!host::endpoint->controllerUpdate(false, 20, LIGHT_WARM_MIREDS + 1));
  checkState(before.on, before.brightness, before.temperature);
  CHECK(host::io.empty());
  CHECK(setLightBrightness(10));
  const unsigned midpoint = (LIGHT_COOL_MIREDS + LIGHT_WARM_MIREDS) / 2;
  CHECK(setLightTemperature(midpoint));
  handleLight();
  checkOutput();  // split after gamma, including low-brightness rounding
}

void restored(bool storedOn, unsigned storedBrightness, unsigned storedTemperature) {
  seed(storedOn, storedBrightness, storedTemperature);
  CHECK(initLight());
  CHECK_EQ(host::endpoint->on, storedOn && storedBrightness != 0);
  const unsigned expectedBrightness = publicLevel(matterLevel(storedBrightness ? storedBrightness : 15));
  const unsigned expectedTemperature = tunable ? std::clamp<unsigned>(storedTemperature, LIGHT_COOL_MIREDS, LIGHT_WARM_MIREDS) : LIGHT_DEFAULT_MIREDS;
  CHECK_EQ(host::endpoint->level, matterLevel(storedBrightness ? storedBrightness : 15));
  CHECK(host::writes.empty());
  refreshLightState();
  checkState(storedOn && storedBrightness != 0, expectedBrightness, expectedTemperature);
  checkOutput();
  CHECK(host::writes.empty());
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  CHECK_EQ(host::prefs.at(PREF_ONOFF), storedOn && storedBrightness != 0);
  CHECK_EQ(host::prefs.at(PREF_BRIGHTNESS), expectedBrightness);
  if (tunable) CHECK_EQ(host::prefs.at(PREF_TEMPERATURE), expectedTemperature);
}

void deferredPersistence() {
  boot();
  controller(false, matterLevel(15));
  CHECK(host::writes.empty());
  handleLight();
  host::now += LIGHT_SAVE_DELAY_MS - 1;
  handleLight();
  CHECK(host::writes.empty());
  ++host::now;
  handleLight();
  expectOnlyWrite(PREF_ONOFF, false);
  host::clearObservations();
  handleLight();
  flushLightPrefs();
  CHECK(host::io.empty());
  CHECK(setLightBrightness(255));
  CHECK(host::writes.empty());
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  expectOnlyWrite(PREF_BRIGHTNESS, 255);
  CHECK(!getLightState().on);
  host::clearObservations();
  if (tunable) {
    CHECK(setLightTemperature(LIGHT_COOL_MIREDS));
    host::now += LIGHT_SAVE_DELAY_MS;
    handleLight();
    expectOnlyWrite(PREF_TEMPERATURE, LIGHT_COOL_MIREDS);
  }
}

void debouncePersistence() {
  boot();
  const uint32_t first = host::now;
  controller(true, 80);
  host::now = first + LIGHT_SAVE_DELAY_MS - 1;
  controller(true, 160);
  handleLight();
  host::now = first + LIGHT_SAVE_DELAY_MS;
  handleLight();
  CHECK(host::writes.empty());
  host::now = first + 2 * LIGHT_SAVE_DELAY_MS - 2;
  handleLight();
  CHECK(host::writes.empty());
  // Repeating an identical callback is not a change and must not starve saving.
  controller(true, 160);
  ++host::now;
  handleLight();
  expectOnlyWrite(PREF_BRIGHTNESS, publicLevel(160));
}

void rolloverPersistence() {
  boot();
  host::now = std::numeric_limits<uint32_t>::max() - LIGHT_SAVE_DELAY_MS / 2;
  controller(false, matterLevel(15));
  host::now += LIGHT_SAVE_DELAY_MS - 1;
  handleLight();
  CHECK(host::writes.empty());
  ++host::now;
  handleLight();
  expectOnlyWrite(PREF_ONOFF, false);
}

void flush() {
  boot();
  CHECK(setLightOnOff(false));
  CHECK(setLightBrightness(255));
  if (tunable) CHECK(setLightTemperature(LIGHT_COOL_MIREDS));
  CHECK(host::io.empty());
  flushLightPrefs();
  CHECK_EQ(host::writes.size(), tunable ? 3 : 2);
  CHECK_EQ(host::prefs.at(PREF_ONOFF), false);
  CHECK_EQ(host::prefs.at(PREF_BRIGHTNESS), 255);
  if (tunable) CHECK_EQ(host::prefs.at(PREF_TEMPERATURE), LIGHT_COOL_MIREDS);
  CHECK(host::fades.empty());
  host::clearObservations();
  flushLightPrefs();
  CHECK(host::io.empty());
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  CHECK(host::writes.empty());
  checkOutput();
}

void failedWriteIsRetried() {
  boot();
  CHECK(setLightBrightness(255));
  host::failNextPrefWrite = PREF_BRIGHTNESS;
  flushLightPrefs();
  CHECK_EQ(host::writes.size(), 1);
  CHECK(host::prefs.at(PREF_BRIGHTNESS) != 255);
  host::clearObservations();
  flushLightPrefs();
  expectOnlyWrite(PREF_BRIGHTNESS, 255);
  host::clearObservations();
  flushLightPrefs();
  CHECK(host::writes.empty());
}

void factoryReset() {
  boot();
  CHECK(setLightBrightness(255));
  CHECK(setLightOnOff(false));
  if (tunable) CHECK(setLightTemperature(LIGHT_COOL_MIREDS));
  CHECK(host::writes.empty());
  clearLightPrefs();
  CHECK_EQ(host::prefClearCalls, 1);
  CHECK(host::prefs.empty());
  host::clearObservations();
  flushLightPrefs();
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  controller(true, 100);
  host::now += LIGHT_SAVE_DELAY_MS;
  handleLight();
  refreshLightState();
  flushLightPrefs();
  CHECK(host::prefs.empty());
  CHECK(host::writes.empty());
  CHECK(std::none_of(host::io.begin(), host::io.end(), [](const std::string &op) {
    return op.rfind("prefs.", 0) == 0;
  }));
  checkOutput();
}

void swappedOutputs() {
  if (!tunable) {
    boot();
    CHECK(!isCwWwSwapped());
    CHECK(!setCwWwSwapped(true));
    CHECK(host::writes.empty());
    return;
  }
  boot(true, 255, LIGHT_WARM_MIREDS);
  CHECK(!isCwWwSwapped());
  CHECK_EQ(duty(0), PWM_MAX_DUTY);
  CHECK_EQ(duty(1), 0);
  CHECK(setCwWwSwapped(true));
  CHECK(isCwWwSwapped());
  expectOnlyWrite(PREF_SWAP_CW_WW, true);
  CHECK_EQ(duty(0), 0);
  CHECK_EQ(duty(1), PWM_MAX_DUTY);
  checkState(true, 255, LIGHT_WARM_MIREDS);
  CHECK_EQ(host::endpoint->temperature, LIGHT_WARM_MIREDS);
  host::clearObservations();
  CHECK(setCwWwSwapped(true));
  CHECK(host::io.empty());
  CHECK(setLightTemperature(LIGHT_COOL_MIREDS));
  handleLight();
  CHECK_EQ(duty(0), PWM_MAX_DUTY);
  CHECK_EQ(duty(1), 0);
  CHECK(setLightOnOff(false));
  handleLight();
  checkOutput();
  CHECK_EQ(duty(0), 0);
  CHECK_EQ(duty(1), 0);
  CHECK(setCwWwSwapped(false));
  checkOutput();
  CHECK_EQ(duty(0), 0);
  CHECK_EQ(duty(1), 0);
}

void swapWriteFailure() {
  boot();
  if (!tunable) return;
  host::failNextPrefWrite = PREF_SWAP_CW_WW;
  const auto oldDuties = host::duties;
  CHECK(!setCwWwSwapped(true));
  CHECK(!isCwWwSwapped());
  CHECK(host::duties == oldDuties);
  CHECK_EQ(host::prefs.count(PREF_SWAP_CW_WW), 0);
  CHECK(setCwWwSwapped(true));
  CHECK(isCwWwSwapped());
}

void swapRestoreAndReset() {
  seed(true, 255, LIGHT_WARM_MIREDS);
  host::prefs[PREF_SWAP_CW_WW] = true;
  CHECK(initLight());
  if (!tunable) {
    CHECK(!isCwWwSwapped());
    return;
  }
  CHECK(isCwWwSwapped());
  refreshLightState();
  CHECK_EQ(duty(0), 0);
  CHECK_EQ(duty(1), PWM_MAX_DUTY);
  clearLightPrefs();
  CHECK(!isCwWwSwapped());
  CHECK(host::prefs.empty());
  host::clearObservations();
  CHECK(!setCwWwSwapped(true));
  CHECK(host::writes.empty());
  flushLightPrefs();
  CHECK(host::writes.empty());
}

void failedInitialization(const std::string &kind) {
  if (kind == "gpio") host::invalidGpio = PIN_PWM;
  else if (kind == "cw-gpio") host::invalidGpio = PIN_PWM_CW;
  else if (kind == "prefs") host::prefBeginSucceeds = false;
  else if (kind == "endpoint") host::endpointBeginSucceeds = false;
  else if (kind == "range") host::failAttributeCall = 2;
  else host::failDriverOperation = "pwm." + kind;
  CHECK(!initLight());
  host::clearObservations();
  CHECK(!setLightOnOff(true));
  CHECK(!setLightBrightness(255));
  CHECK(!setLightTemperature(LIGHT_COOL_MIREDS));
  refreshLightState();
  handleLight();
  CHECK(host::io.empty());
  CHECK(host::writes.empty());
}

void failedFadeIsRetried() {
  boot();
  CHECK(setLightBrightness(255));
  host::failDriverOperation = "pwm.start";
  handleLight();
  CHECK(duty(0) != PWM_MAX_DUTY);
  host::clearObservations();
  handleLight();
  CHECK(!host::fades.empty());
  checkOutput();
}

using Case = std::pair<const char *, std::function<void()>>;
const std::vector<Case> cases = {
  {"before-init", beforeInit},
  {"initialization", initialization},
  {"touch-configuration", touchConfiguration},
  {"queued-callbacks", queuedCallbacks},
  {"brightness-full-scale", brightnessAndFullScale},
  {"changes-while-off", changesWhileOff},
  {"restore-legacy-zero-on", [] { restored(true, 0, LIGHT_DEFAULT_MIREDS); }},
  {"restore-legacy-zero-off", [] { restored(false, 0, LIGHT_DEFAULT_MIREDS); }},
  {"restore-off-retained-level", [] { restored(false, 100, LIGHT_DEFAULT_MIREDS); }},
  {"restore-full-scale", [] { restored(true, 255, LIGHT_DEFAULT_MIREDS); }},
  {"restore-minimum", [] { restored(true, 1, LIGHT_DEFAULT_MIREDS); }},
  {"restore-temperature-low", [] { restored(true, 15, 0); }},
  {"restore-temperature-high", [] { restored(true, 15, 65535); }},
  {"deferred-changed-keys", deferredPersistence},
  {"debounce-persistence", debouncePersistence},
  {"rollover-persistence", rolloverPersistence},
  {"flush", flush},
  {"failed-write-retry", failedWriteIsRetried},
  {"factory-reset", factoryReset},
  {"swapped-outputs", swappedOutputs},
  {"swap-write-failure", swapWriteFailure},
  {"swap-restore-and-reset", swapRestoreAndReset},
  {"init-invalid-gpio", [] { failedInitialization("gpio"); }},
  {"init-failed-timer", [] { failedInitialization("timer"); }},
  {"init-failed-channel", [] { failedInitialization("channel"); }},
  {"init-failed-fade-install", [] { failedInitialization("install"); }},
  {"init-failed-preferences", [] { failedInitialization("prefs"); }},
  {"init-failed-endpoint", [] { failedInitialization("endpoint"); }},
  {"failed-fade-retry", failedFadeIsRetried},
#if LIGHT_TUNABLE_WHITE
  {"temperature-range-power", temperatureRangeAndPower},
  {"init-invalid-cw-gpio", [] { failedInitialization("cw-gpio"); }},
  {"init-failed-range", [] { failedInitialization("range"); }},
#endif
};
}  // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--list") {
    for (const auto &entry : cases) std::cout << entry.first << '\n';
    return 0;
  }
  if (argc != 2) {
    std::cerr << "Pass --list or exactly one case name (fresh process per case).\n";
    return 2;
  }
  for (const auto &entry : cases) {
    if (entry.first != std::string(argv[1])) continue;
    try {
      entry.second();
      CHECK_EQ(host::callbackDepth, 0);
      CHECK_EQ(host::criticalDepth, 0);
      std::cout << "PASS mode=" << LIGHT_TUNABLE_WHITE << ' ' << entry.first
                << " (" << test::checks << " checks)\n";
      return 0;
    } catch (const std::exception &error) {
      std::cerr << "FAIL mode=" << LIGHT_TUNABLE_WHITE << ' ' << entry.first << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cerr << "Unknown case: " << argv[1] << '\n';
  return 2;
}
