#pragma once
#include "host_runtime.h"
#include <cstddef>

class Preferences {
  bool opened = false;

  uint32_t get(const char *key, uint32_t fallback) {
    host::record("prefs.get");
    if (!opened) throw std::logic_error("Preferences get before begin");
    host::reads.emplace_back(key);
    const auto found = host::prefs.find(key);
    return found == host::prefs.end() ? fallback : found->second;
  }
  size_t put(const char *key, uint32_t value, size_t bytes) {
    host::record("prefs.put");
    if (!opened || host::prefsReadOnly) throw std::logic_error("Preferences not writable");
    host::writes.push_back({key, value});
    if (host::failNextPrefWrite == key) {
      host::failNextPrefWrite.clear();
      return 0;
    }
    host::prefs[key] = value;
    return bytes;
  }

 public:
  bool begin(const char *name, bool readOnly) {
    host::record("prefs.begin");
    host::prefNamespace = name;
    host::prefsReadOnly = readOnly;
    opened = host::prefBeginSucceeds;
    return opened;
  }
  void end() { opened = false; }
  bool getBool(const char *key, bool fallback) { return get(key, fallback) != 0; }
  uint8_t getUChar(const char *key, uint8_t fallback) { return get(key, fallback); }
  uint16_t getUShort(const char *key, uint16_t fallback) { return get(key, fallback); }
  size_t putBool(const char *key, bool value) { return put(key, value, 1); }
  size_t putUChar(const char *key, uint8_t value) { return put(key, value, 1); }
  size_t putUShort(const char *key, uint16_t value) { return put(key, value, 2); }
  bool clear() {
    host::record("prefs.clear");
    if (!opened) throw std::logic_error("Preferences clear before begin");
    ++host::prefClearCalls;
    host::prefs.clear();
    return true;
  }
};
