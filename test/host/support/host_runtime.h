#pragma once

// Shared observations for the host fakes. The firmware is compiled separately;
// tests never include light.cpp or reach into its file-static state.
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace host {
inline uint32_t now = 100;
inline unsigned callbackDepth = 0;
inline unsigned criticalDepth = 0;
inline unsigned callbackCalls = 0;
inline std::vector<std::string> io;
inline std::vector<std::pair<int, int>> gpioModes;
inline std::vector<std::pair<int, int>> gpioWrites;

inline void record(const std::string &operation) {
  if (callbackDepth) throw std::logic_error("I/O from Matter callback: " + operation);
  if (criticalDepth) throw std::logic_error("I/O inside state critical section: " + operation);
  io.push_back(operation);
}

struct CallbackScope {
  CallbackScope() { ++callbackDepth; ++callbackCalls; }
  ~CallbackScope() { --callbackDepth; }
};

struct PrefWrite {
  std::string key;
  uint32_t value;
};
inline std::map<std::string, uint32_t> prefs;
inline std::vector<PrefWrite> writes;
inline std::vector<std::string> reads;
inline std::string prefNamespace;
inline bool prefsReadOnly = true;
inline bool prefBeginSucceeds = true;
inline unsigned prefClearCalls = 0;
inline std::string failNextPrefWrite;

struct ChannelConfig {
  int pin;
  int channel;
  uint32_t initialDuty;
};
struct Fade {
  int channel;
  uint32_t duty;
  uint32_t milliseconds;
};
inline std::vector<ChannelConfig> channels;
inline std::vector<Fade> fades;
inline std::map<int, uint32_t> duties;
inline unsigned timerCalls = 0;
inline unsigned timerFrequency = 0;
inline unsigned timerResolution = 0;
inline unsigned fadeInstallCalls = 0;
inline int invalidGpio = -1;
inline std::string failDriverOperation;

inline void clearObservations() {
  io.clear();
  writes.clear();
  reads.clear();
  fades.clear();
}
}  // namespace host
