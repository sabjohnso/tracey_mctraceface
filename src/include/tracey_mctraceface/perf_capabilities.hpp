#pragma once

#include <cstdint>
#include <string>

namespace tracey_mctraceface {

  /** @brief Detected kernel version. */
  struct KernelVersion {
    int major = 0;
    int minor = 0;
  };

  /** @brief Detected perf and kernel capabilities. */
  struct PerfCapabilities {
    bool has_intel_pt = false;
    bool has_kernel_tracing = false;
    KernelVersion kernel_version;
    bool snapshot_on_exit = false; // >= 5.4
    bool ctlfd = false;            // >= 5.10
    bool dlfilter = false;         // >= 5.14
    bool kcore = false;            // >= 5.5
  };

  /** @brief Detect perf and kernel capabilities from the system. */
  auto
  detect_capabilities() -> PerfCapabilities;

  /** @brief Parse kernel version from a /proc/version string. */
  auto
  parse_kernel_version(std::string_view version_str) -> KernelVersion;

} // namespace tracey_mctraceface
