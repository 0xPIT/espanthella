#pragma once
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace test {
inline uint64_t checks = 0;
inline void require(bool condition, const char *expression, const char *file, int line) {
  ++checks;
  if (condition) return;
  std::ostringstream message;
  message << file << ':' << line << ": " << expression;
  throw std::runtime_error(message.str());
}
template <class A, class B>
void equal(const A &actual, const B &expected, const char *expression, const char *file, int line) {
  ++checks;
  if (std::cmp_equal(+actual, +expected)) return;
  std::ostringstream message;
  message << file << ':' << line << ": " << expression << " (actual=" << +actual
          << ", expected=" << +expected << ')';
  throw std::runtime_error(message.str());
}
}  // namespace test
#define CHECK(expression) test::require(bool(expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) \
  test::equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
