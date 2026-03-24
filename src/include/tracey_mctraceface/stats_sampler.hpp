#pragma once

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief Periodically samples process stats from /proc.
   *
   * Runs a background thread that reads RSS and I/O counters
   * at a configurable interval. Samples are stored and can be
   * written as FXT counter events after tracing completes.
   */
  class StatsSampler {
  public:
    struct Sample {
      std::uint64_t timestamp_ns;
      std::uint64_t rss_bytes;
      std::uint64_t io_read_bytes;
      std::uint64_t io_write_bytes;
    };

    explicit StatsSampler(
      pid_t pid,
      std::chrono::milliseconds interval = std::chrono::milliseconds(10));

    ~StatsSampler();

    StatsSampler(const StatsSampler&) = delete;
    auto
    operator=(const StatsSampler&) -> StatsSampler& = delete;

    /** @brief Start the sampling thread. */
    void
    start();

    /** @brief Stop the sampling thread and join. */
    void
    stop();

    /** @brief Get all collected samples. */
    auto
    samples() const -> const std::vector<Sample>&;

    /** @brief Read a single sample from /proc right now. */
    auto
    read_once() const -> Sample;

  private:
    pid_t pid_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::vector<Sample> samples_;

    void
    run_loop();
  };

} // namespace tracey_mctraceface
