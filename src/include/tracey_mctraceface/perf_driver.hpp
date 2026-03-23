#pragma once

#include <tracey_mctraceface/perf_capabilities.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  /** @brief Trace scope: what to record. */
  enum class TraceScope { Userspace, Kernel, Both };

  /** @brief Timer resolution for Intel PT. */
  enum class TimerResolution { Low, Normal, High };

  /** @brief Configuration for a perf recording session. */
  struct PerfConfig {
    bool multi_thread = false;
    bool full_execution = false;
    bool sampling = false;
    std::uint32_t snapshot_size_pages = 0; // 0 = auto
    TraceScope trace_scope = TraceScope::Userspace;
    TimerResolution timer_resolution = TimerResolution::Normal;
    std::string working_directory;
  };

  /**
   * @brief Build the argument vector for `perf record`.
   */
  auto
  build_perf_record_args(
    const PerfConfig& config,
    const PerfCapabilities& caps,
    const std::vector<std::string>& pids) -> std::vector<std::string>;

  /**
   * @brief Build the argument vector for `perf script`.
   */
  auto
  build_perf_script_args(
    const PerfConfig& config, const std::string& working_directory)
    -> std::vector<std::string>;

  /**
   * @brief Return the scope selector string for perf event config.
   */
  auto
  scope_selector(TraceScope scope) -> std::string;

  /**
   * @brief Return the Intel PT timer config string.
   */
  auto
  timer_config(TimerResolution res) -> std::string;

} // namespace tracey_mctraceface
