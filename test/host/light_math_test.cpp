// Run all host tests: python3 scripts/test_light_host.py
// Independent oracles: nearest-integer scale conversion and linear fractional
// power split. No firmware equations, gamma table, or mixed-duty implementation
// are copied here.
#include "light_math.h"
#include "test_assert.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

static_assert(brightnessToMatter(255) == 254, "full UI scale must not become Matter null");
static_assert(brightnessFromMatter(254) == 255, "Matter full scale is UI full scale");
static_assert(mixLightDuty(1, 200, 100, 300, true).warm +
              mixLightDuty(1, 200, 100, 300, true).cool == 1,
              "single-count PWM must not disappear in a mix");

static void brightness() {
  unsigned previousMatter = 0;
  unsigned previousPublic = 0;
  for (unsigned value = 0; value <= 255; ++value) {
    const auto encoded = brightnessToMatter(value);
    const auto decoded = brightnessFromMatter(value);
    const auto expectedEncoded = std::max(1L, std::lround(value * (254.0 / 255.0)));
    const auto expectedDecoded = std::lround(std::clamp(value, 1u, 254u) * (255.0 / 254.0));
    CHECK_EQ(encoded, expectedEncoded);
    CHECK_EQ(decoded, expectedDecoded);
    CHECK(encoded >= 1 && encoded <= 254);
    CHECK(decoded >= 1);
    CHECK(encoded >= previousMatter);
    CHECK(decoded >= previousPublic);
    previousMatter = encoded;
    previousPublic = decoded;
    if (value) CHECK(std::abs(int(brightnessFromMatter(encoded)) - int(value)) <= 1);
    if (value >= 1 && value <= 254) CHECK_EQ(brightnessToMatter(decoded), value);
  }
  CHECK_EQ(brightnessToMatter(0), 1);
  CHECK_EQ(brightnessFromMatter(0), 1);
  CHECK_EQ(brightnessFromMatter(255), 255);
}

static void checkMix(uint16_t total, uint16_t temperature, uint16_t cool, uint16_t warm) {
  const auto duties = mixLightDuty(total, temperature, cool, warm, true);
  CHECK_EQ(unsigned(duties.warm) + unsigned(duties.cool), unsigned(total));
  CHECK(duties.warm <= total && duties.cool <= total);
  if (temperature <= cool) {
    CHECK_EQ(duties.cool, total);
    CHECK_EQ(duties.warm, 0);
  } else if (temperature >= warm) {
    CHECK_EQ(duties.warm, total);
    CHECK_EQ(duties.cool, 0);
  } else {
    const double coolFraction = double(warm - temperature) / double(warm - cool);
    // Quantization error must be at most half a PWM count in either channel.
    CHECK(std::abs(duties.cool - total * coolFraction) <= 0.500000001);
    CHECK(std::abs(duties.warm - total * (1.0 - coolFraction)) <= 0.500000001);
  }
}

static void temperaturesAndMixing() {
  constexpr uint16_t cool = 1000000 / 6500;
  constexpr uint16_t warm = 1000000 / 2700;
  // Exhaust every 10-bit duty and every mired inside the production LED range.
  for (unsigned total = 0; total <= 1023; ++total) {
    unsigned previousWarm = 0;
    unsigned previousCool = total;
    for (unsigned temperature = cool; temperature <= warm; ++temperature) {
      checkMix(total, temperature, cool, warm);
      const auto duties = mixLightDuty(total, temperature, cool, warm, true);
      CHECK(duties.warm >= previousWarm);
      CHECK(duties.cool <= previousCool);
      previousWarm = duties.warm;
      previousCool = duties.cool;
    }
  }
  // Exhaust all 16-bit temperature inputs, not just physically valid mireds.
  for (unsigned temperature = 0; temperature <= 65535; ++temperature) {
    for (const auto range : {std::pair<uint16_t, uint16_t>{cool, warm}, {0, 65535}, {100, 500}, {1, 2}}) {
      CHECK_EQ(clampLightTemperature(temperature, range.first, range.second),
               std::clamp<unsigned>(temperature, range.first, range.second));
    }
    for (const unsigned total : {0u, 1u, 2u, 3u, 255u, 1023u, 32768u, 65535u}) {
      checkMix(total, temperature, cool, warm);
      checkMix(total, temperature, 0, 65535);
    }
  }
  // Exhaust the entire duty input type at endpoints/midpoints, including a
  // one-mired span and degenerate ranges that must fall back to single-channel.
  for (unsigned total = 0; total <= 65535; ++total) {
    checkMix(total, cool, cool, warm);
    checkMix(total, warm, cool, warm);
    checkMix(total, 200, 100, 300);
    checkMix(total, 32767, 0, 65535);
    checkMix(total, 1, 1, 2);
    const auto midpoint = mixLightDuty(total, 200, 100, 300, true);
    CHECK(std::abs(int(midpoint.warm) - int(midpoint.cool)) <= 1);
    for (const auto range : {std::pair<uint16_t, uint16_t>{100, 100}, {300, 100}, {0, 0}}) {
      const auto fallback = mixLightDuty(total, 200, range.first, range.second, true);
      CHECK_EQ(fallback.warm, total);
      CHECK_EQ(fallback.cool, 0);
    }
    const auto single = mixLightDuty(total, 0, cool, warm, false);
    CHECK_EQ(single.warm, total);
    CHECK_EQ(single.cool, 0);
  }
}

int main() {
  try {
    brightness();
    temperaturesAndMixing();
    std::cout << "PASS light_math (" << test::checks << " checks)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL light_math: " << error.what() << '\n';
    return 1;
  }
}
