#pragma once
#include "esp_err.h"
#include "host_runtime.h"

enum ledc_mode_t { LEDC_LOW_SPEED_MODE };
enum ledc_timer_t { LEDC_TIMER_0 };
enum ledc_channel_t { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };
enum ledc_timer_bit_t { LEDC_TIMER_10_BIT = 10 };
enum ledc_clk_cfg_t { LEDC_AUTO_CLK };
enum ledc_intr_type_t { LEDC_INTR_DISABLE };
enum ledc_fade_mode_t { LEDC_FADE_NO_WAIT };
struct ledc_timer_config_t {
  ledc_mode_t speed_mode;
  ledc_timer_bit_t duty_resolution;
  ledc_timer_t timer_num;
  uint32_t freq_hz;
  ledc_clk_cfg_t clk_cfg;
};
struct ledc_channel_config_t {
  int gpio_num;
  ledc_mode_t speed_mode;
  ledc_channel_t channel;
  ledc_intr_type_t intr_type;
  ledc_timer_t timer_sel;
  uint32_t duty;
  int hpoint;
};
inline esp_err_t hostLedcOperation(const char *operation) {
  host::record(operation);
  if (host::failDriverOperation == operation) {
    host::failDriverOperation.clear();
    return ESP_FAIL;
  }
  return ESP_OK;
}
inline esp_err_t ledc_timer_config(const ledc_timer_config_t *config) {
  ++host::timerCalls;
  host::timerFrequency = config->freq_hz;
  host::timerResolution = config->duty_resolution;
  return hostLedcOperation("pwm.timer");
}
inline esp_err_t ledc_channel_config(const ledc_channel_config_t *config) {
  host::channels.push_back({config->gpio_num, config->channel, config->duty});
  host::duties[config->channel] = config->duty;
  return hostLedcOperation("pwm.channel");
}
inline esp_err_t ledc_fade_func_install(int) {
  ++host::fadeInstallCalls;
  return hostLedcOperation("pwm.install");
}
inline esp_err_t ledc_fade_stop(ledc_mode_t, ledc_channel_t) {
  return hostLedcOperation("pwm.stop");
}
inline esp_err_t ledc_set_fade_with_time(ledc_mode_t, ledc_channel_t channel,
                                      uint32_t duty, int milliseconds) {
  const esp_err_t error = hostLedcOperation("pwm.set");
  if (error == ESP_OK) host::fades.push_back({channel, duty, uint32_t(milliseconds)});
  return error;
}
inline esp_err_t ledc_fade_start(ledc_mode_t, ledc_channel_t channel, ledc_fade_mode_t) {
  const esp_err_t error = hostLedcOperation("pwm.start");
  if (error == ESP_OK) {
    for (auto it = host::fades.rbegin(); it != host::fades.rend(); ++it) {
      if (it->channel == channel) { host::duties[channel] = it->duty; break; }
    }
  }
  return error;
}
