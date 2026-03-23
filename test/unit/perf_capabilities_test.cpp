#include <tracey_mctraceface/perf_capabilities.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tracey_mctraceface;

TEST_CASE(
  "parse kernel version from /proc/version string", "[perf_capabilities]") {
  auto v = parse_kernel_version(
    "Linux version 6.8.0-106-generic (buildd@lcy02-amd64-096)");
  CHECK(v.major == 6);
  CHECK(v.minor == 8);
}

TEST_CASE("parse old kernel version", "[perf_capabilities]") {
  auto v = parse_kernel_version(
    "Linux version 5.4.0-42-generic (buildd@lgw01-amd64-038)");
  CHECK(v.major == 5);
  CHECK(v.minor == 4);
}

TEST_CASE("parse invalid version string", "[perf_capabilities]") {
  auto v = parse_kernel_version("not a version");
  CHECK(v.major == 0);
  CHECK(v.minor == 0);
}

TEST_CASE("detect_capabilities runs without crash", "[perf_capabilities]") {
  auto caps = detect_capabilities();
  // Just verify it doesn't crash; actual values are system-dependent
  CHECK(caps.kernel_version.major >= 0);
}
