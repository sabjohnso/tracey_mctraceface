#include <tracey_mctraceface/stats_sampler.hpp>

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

namespace tracey_mctraceface {

  namespace {

    auto
    now_ns() -> std::uint64_t {
      auto tp = std::chrono::steady_clock::now();
      return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          tp.time_since_epoch())
          .count());
    }

    auto
    read_rss(pid_t pid) -> std::uint64_t {
      auto path = "/proc/" + std::to_string(pid) + "/statm";
      std::ifstream f(path);
      if (!f) return 0;

      std::uint64_t vm_size = 0;
      std::uint64_t rss_pages = 0;
      f >> vm_size >> rss_pages;

      static auto page_size = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
      return rss_pages * page_size;
    }

    struct IoStats {
      std::uint64_t read_bytes = 0;
      std::uint64_t write_bytes = 0;
    };

    auto
    read_io(pid_t pid) -> IoStats {
      auto path = "/proc/" + std::to_string(pid) + "/io";
      std::ifstream f(path);
      if (!f) return {};

      IoStats stats;
      std::string key;
      std::uint64_t val = 0;
      while (f >> key >> val) {
        if (key == "read_bytes:") stats.read_bytes = val;
        if (key == "write_bytes:") stats.write_bytes = val;
      }
      return stats;
    }

  } // namespace

  StatsSampler::StatsSampler(pid_t pid, std::chrono::milliseconds interval)
      : pid_(pid)
      , interval_(interval) {}

  StatsSampler::~StatsSampler() { stop(); }

  void
  StatsSampler::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&StatsSampler::run_loop, this);
  }

  void
  StatsSampler::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
  }

  auto
  StatsSampler::samples() const -> const std::vector<Sample>& {
    return samples_;
  }

  auto
  StatsSampler::read_once() const -> Sample {
    auto io = read_io(pid_);
    return {
      .timestamp_ns = now_ns(),
      .rss_bytes = read_rss(pid_),
      .io_read_bytes = io.read_bytes,
      .io_write_bytes = io.write_bytes,
    };
  }

  void
  StatsSampler::run_loop() {
    while (running_) {
      samples_.push_back(read_once());
      std::this_thread::sleep_for(interval_);
    }
  }

} // namespace tracey_mctraceface
