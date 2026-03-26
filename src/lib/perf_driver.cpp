#include <tracey_mctraceface/perf_driver.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  auto
  scope_selector(TraceScope scope) -> std::string {
    switch (scope) {
      case TraceScope::Userspace:
        return "u";
      case TraceScope::Kernel:
        return "k";
      case TraceScope::Both:
        return "uk";
    }
    return "u";
  }

  auto
  timer_config(TimerResolution res) -> std::string {
    switch (res) {
      case TimerResolution::Low:
        return "";
      case TimerResolution::Normal:
        return "cyc=1,cyc_thresh=1,mtc_period=0";
      case TimerResolution::High:
        return "cyc=1,cyc_thresh=1,mtc_period=0,noretcomp=1";
    }
    return "";
  }

  namespace {

    auto
    build_common_record_args(
      const PerfConfig& config, const PerfCapabilities& caps)
      -> std::vector<std::string> {
      std::vector<std::string> args = {"perf", "record"};

      // Output file
      auto data_file = config.working_directory + "/perf.data";
      args.push_back("-o");
      args.push_back(data_file);
      args.push_back("--timestamp");

      // Event
      auto sel = scope_selector(config.trace_scope);
      if (config.sampling) {
        args.push_back("--event=cycles/freq=10000/" + sel);
      } else {
        auto tc = timer_config(config.timer_resolution);
        auto event = std::string("--event=intel_pt/");
        if (!tc.empty())
          event += tc + "/";
        else
          event += "/";
        event += sel;
        args.push_back(event);
      }

      // Snapshot control
      if (!config.full_execution && !config.sampling) {
        if (caps.snapshot_on_exit) {
          args.push_back("--snapshot=e");
        } else {
          args.push_back("--snapshot");
        }
      }

      // Snapshot size
      if (config.snapshot_size_pages > 0 && !config.sampling) {
        args.push_back("-m," + std::to_string(config.snapshot_size_pages));
      }

      // Kcore
      if (
        config.trace_scope != TraceScope::Userspace && caps.kcore &&
        !config.sampling) {
        args.push_back("--kcore");
      }

      return args;
    }

  } // namespace

  auto
  build_perf_record_args(
    const PerfConfig& config,
    const PerfCapabilities& caps,
    const std::vector<std::string>& pids) -> std::vector<std::string> {
    auto args = build_common_record_args(config, caps);

    if (!pids.empty()) {
      std::string pid_list;
      for (std::size_t i = 0; i < pids.size(); ++i) {
        if (i > 0) pid_list += ",";
        pid_list += pids[i];
      }
      if (config.multi_thread) {
        args.push_back("-p");
      } else {
        args.push_back("--per-thread");
        args.push_back("-t");
      }
      args.push_back(pid_list);
    }

    return args;
  }

  auto
  build_perf_record_args(
    const PerfConfig& config,
    const PerfCapabilities& caps,
    const std::string& program,
    const std::vector<std::string>& program_args) -> std::vector<std::string> {
    auto args = build_common_record_args(config, caps);

    // Separator between perf args and the target command
    args.push_back("--");
    args.push_back(program);
    for (const auto& a : program_args) {
      args.push_back(a);
    }

    return args;
  }

  auto
  build_perf_script_args(
    const PerfConfig& config, const std::string& working_directory)
    -> std::vector<std::string> {
    std::vector<std::string> args = {"perf", "script"};

    args.push_back("-i");
    // Accept either a directory (append /perf.data) or a file path
    auto input_path = working_directory;
    if (std::filesystem::is_directory(working_directory)) {
      input_path += "/perf.data";
    }
    args.push_back(input_path);
    args.push_back("--ns");

    if (!config.sampling) {
      args.push_back("--itrace=bep");
      args.push_back("-F");
      args.push_back(
        "pid,tid,time,flags,ip,addr,sym,symoff,synth,dso,event,period");
    } else {
      args.push_back("-F");
      args.push_back("pid,tid,time,ip,sym,symoff,dso,event,period");
    }

    return args;
  }

} // namespace tracey_mctraceface
