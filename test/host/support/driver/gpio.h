#pragma once
#include "host_runtime.h"
#include "soc/soc_caps.h"
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) \
  ((pin) >= 0 && (pin) < SOC_GPIO_PIN_COUNT && (pin) != host::invalidGpio)
