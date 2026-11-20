#pragma once
#include <string>
#include <string_view>

namespace MatterPairing {

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Arduino exposes a QR-generator URL, not the raw Matter payload. Extract only
// its data parameter and URL-decode it ONCE; scanners must receive MT:..., not
// the web URL or a percent-encoded MT%3A... string.
inline std::string qrPayload(std::string_view source) {
  if (source.substr(0, 3) != "MT:") {
    const auto question = source.find('?');
    if (question == std::string_view::npos) return {};
    source.remove_prefix(question + 1);
    source = source.substr(0, source.find('#'));
    bool found = false;
    while (!source.empty()) {
      const auto amp = source.find('&');
      const auto parameter = source.substr(0, amp);
      if (parameter.substr(0, 5) == "data=") {
        source = parameter.substr(5);
        found = true;
        break;
      }
      if (amp == std::string_view::npos) break;
      source.remove_prefix(amp + 1);
    }
    if (!found) return {};
  }

  std::string payload;
  payload.reserve(source.size());
  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    if (c == '%') {
      if (i + 2 >= source.size()) return {};
      const int high = hexDigit(source[i + 1]), low = hexDigit(source[i + 2]);
      if (high < 0 || low < 0) return {};
      c = static_cast<char>((high << 4) | low);
      i += 2;
    }
    payload += c;
  }
  if (payload.size() <= 3 || payload.compare(0, 3, "MT:") != 0) return {};
  // Matter's Base38 alphabet also makes the value safe in the HTML/JS template.
  if (payload.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.", 3) != std::string::npos) return {};
  return payload;
}

// Preserve every digit, including leading zeroes and the check digit. Matter
// supports the usual 11-digit code and a 21-digit form including vendor/product.
inline std::string formatManualCode(std::string_view digits) {
  if ((digits.size() != 11 && digits.size() != 21) ||
      digits.find_first_not_of("0123456789") != std::string_view::npos) return {};
  std::string formatted;
  formatted.reserve(digits.size() + 4);
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i == 4 || i == 7 || i == 11 || i == 16) formatted += '-';
    formatted += digits[i];
  }
  return formatted;
}

}  // namespace MatterPairing
