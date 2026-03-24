#include <tracey_mctraceface/stats_sampler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>
#include <thread>

using namespace tracey_mctraceface;

TEST_CASE("read_once returns non-zero RSS for self", "[stats_sampler]") {
  StatsSampler sampler(getpid());
  auto sample = sampler.read_once();
  CHECK(sample.rss_bytes > 0);
  CHECK(sample.timestamp_ns > 0);
}

TEST_CASE("read_once returns I/O stats for self", "[stats_sampler]") {
  StatsSampler sampler(getpid());
  auto sample = sampler.read_once();
  // We've done I/O just by running, so read_bytes should be > 0
  CHECK(sample.io_read_bytes > 0);
}

TEST_CASE("polling collects multiple samples", "[stats_sampler]") {
  StatsSampler sampler(getpid(), std::chrono::milliseconds(5));
  sampler.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  sampler.stop();

  CHECK(sampler.samples().size() >= 3);

  // Timestamps should be monotonically increasing
  for (std::size_t i = 1; i < sampler.samples().size(); ++i) {
    CHECK(
      sampler.samples()[i].timestamp_ns >=
      sampler.samples()[i - 1].timestamp_ns);
  }
}

TEST_CASE("sampler handles invalid PID gracefully", "[stats_sampler]") {
  StatsSampler sampler(999999999);
  auto sample = sampler.read_once();
  CHECK(sample.rss_bytes == 0);
}
