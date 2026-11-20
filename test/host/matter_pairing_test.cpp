// Run through: python3 scripts/test_light_host.py
#include "matter_pairing.h"
#include "test_assert.h"
#include <algorithm>
#include <iostream>
#include <string>

int main() {
  try {
    using MatterPairing::formatManualCode;
    using MatterPairing::qrPayload;
    // Public Matter test data, never a device's live commissioning credentials.
    const std::string payload = "MT:Y.K9042C00KA0648G00";
    CHECK(qrPayload(payload) == payload);
    CHECK(qrPayload("https://example.invalid/qrcode.html?data=" + payload) == payload);
    CHECK(qrPayload("https://example.invalid/qrcode.html?data=MT%3AY.K9042C00KA0648G00") == payload);
    CHECK(qrPayload("https://example.invalid/?other=value&data=MT%3aY%2eK9042C00KA0648G00&unused=1#fragment") == payload);
    CHECK(qrPayload("https://example.invalid/?data=" + payload + "#fragment") == payload);
    CHECK(qrPayload("https://example.invalid/?data=MT%3A-24J0AFN00KA0648G00") == "MT:-24J0AFN00KA0648G00");
    for (const auto *invalid : {"", "MT:", "not a URL or Matter payload", "https://example.invalid/",
         "https://example.invalid/?metadata=MT%3AY.K9042C00KA0648G00", "https://example.invalid/?data=",
         "https://example.invalid/?data=MT%253AY.K9042C00KA0648G00", "https://example.invalid/?data=MT%3",
         "https://example.invalid/?data=MT%ZZABC", "https://example.invalid/?data=MT%3AABC%00",
         "https://example.invalid/?data=MT%3AABC%27", "https://example.invalid/?data=MT%3AABC%3Cscript%3E",
         "https://example.invalid/?data=MT%3Aabc", "https://example.invalid/?data=MT%3AABC+DEF"}) {
      CHECK(qrPayload(invalid).empty());
    }
    CHECK(formatManualCode("34970112332") == "3497-011-2332");
    CHECK(formatManualCode("01234567890") == "0123-456-7890");
    CHECK(formatManualCode("123456789012345678901") == "1234-567-8901-23456-78901");
    for (const auto *invalid : {"", "123", "3497011233", "349701123322", "3497011233x", "<script>xxx"}) {
      CHECK(formatManualCode(invalid).empty());
    }
    for (const auto *raw : {"34970112332", "01234567890", "123456789012345678901"}) {
      std::string stripped = formatManualCode(raw);
      stripped.erase(std::remove(stripped.begin(), stripped.end(), '-'), stripped.end());
      CHECK(stripped == raw);  // Formatting cannot change PIN/check digits.
    }
    std::cout << "PASS matter_pairing (" << test::checks << " checks)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL matter_pairing: " << error.what() << '\n';
    return 1;
  }
}
