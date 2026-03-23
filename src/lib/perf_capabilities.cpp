#include <tracey_mctraceface/perf_capabilities.hpp>

#include <unistd.h>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>

namespace tracey_mctraceface {

  auto
  parse_kernel_version(std::string_view version_str) -> KernelVersion {
    KernelVersion v;

    // Find "Linux version X.Y" pattern
    auto pos = version_str.find("Linux version ");
    if (pos == std::string_view::npos) return v;
    pos += 14; // skip "Linux version "

    // Parse major
    auto end = version_str.data() + version_str.size();
    auto [p1, ec1] = std::from_chars(version_str.data() + pos, end, v.major);
    if (ec1 != std::errc{} || p1 >= end || *p1 != '.') return v;

    // Parse minor
    std::from_chars(p1 + 1, end, v.minor);
    return v;
  }

  auto
  detect_capabilities() -> PerfCapabilities {
    PerfCapabilities caps;

    // Intel PT support
    caps.has_intel_pt =
      std::filesystem::exists("/sys/bus/event_source/devices/intel_pt");

    // Root check for kernel tracing
    caps.has_kernel_tracing = (geteuid() == 0);

    // Kernel version
    std::ifstream version_file("/proc/version");
    if (version_file) {
      std::string version_str;
      std::getline(version_file, version_str);
      caps.kernel_version = parse_kernel_version(version_str);

      auto kv = caps.kernel_version;
      auto at_least = [&](int major, int minor) {
        return kv.major > major || (kv.major == major && kv.minor >= minor);
      };

      caps.snapshot_on_exit = at_least(5, 4);
      caps.kcore = at_least(5, 5);
      caps.ctlfd = at_least(5, 10);
      caps.dlfilter = at_least(5, 14);
    }

    return caps;
  }

} // namespace tracey_mctraceface
