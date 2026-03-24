#include "tracey_mctraceface_main.hpp"

#include <tracey_mctraceface/background_process.hpp>
#include <tracey_mctraceface/file_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/perf_capabilities.hpp>
#include <tracey_mctraceface/perf_driver.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>
#include <tracey_mctraceface/subprocess.hpp>

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace tracey_mctraceface {

  namespace {

    auto
    decode_perf_data(
      const std::string& working_dir, const std::string& output, bool sampling)
      -> int {
      PerfConfig perf_config;
      perf_config.sampling = sampling;
      auto script_args = build_perf_script_args(perf_config, working_dir);

      Subprocess perf_script(script_args);

      FileSink sink(output);
      FxtWriter writer(sink);
      writer.write_preamble("tracey_mctraceface", 1, 1'000'000'000ULL, 0, 0);

      StackReconstructor reconstructor(writer);
      PerfScriptParser parser;

      std::string line;
      std::uint64_t event_count = 0;

      while (perf_script.read_line(line)) {
        auto event = parser.feed_line(line);
        if (event) {
          reconstructor.process_event(*event);
          ++event_count;
        }
      }

      if (auto event = parser.finish()) {
        reconstructor.process_event(*event);
        ++event_count;
      }
      reconstructor.finish();

      auto exit_code = perf_script.wait();

      std::cerr << "Decoded " << event_count << " events -> " << output << '\n';

      if (exit_code != 0) {
        std::cerr << "warning: perf script exited with code " << exit_code
                  << '\n';
      }

      return 0;
    }

    auto
    parse_trace_scope(const std::string& s) -> TraceScope {
      if (s == "kernel") return TraceScope::Kernel;
      if (s == "both") return TraceScope::Both;
      return TraceScope::Userspace;
    }

    auto
    parse_timer_resolution(const std::string& s) -> TimerResolution {
      if (s == "low") return TimerResolution::Low;
      if (s == "high") return TimerResolution::High;
      return TimerResolution::Normal;
    }

    auto
    run_run(const nlohmann::json& config) -> int {
      auto sub = config.value("run", nlohmann::json::object());
      auto program = sub.value("program", "");
      auto output = sub.value("output", "trace.fxt");
      auto multi_thread = sub.value("multi-thread", false);
      auto full_execution = sub.value("full-execution", false);
      auto sampling = sub.value("sampling", false);
      auto snapshot_size = sub.value("snapshot-size", 0);
      auto trace_scope_str = sub.value("trace-scope", "userspace");
      auto timer_res_str = sub.value("timer-resolution", "normal");

      // Collect program args
      std::vector<std::string> program_args;
      program_args.push_back(program);
      if (sub.contains("args") && sub["args"].is_array()) {
        for (const auto& arg : sub["args"]) {
          program_args.push_back(arg.get<std::string>());
        }
      }

      // 1. Detect capabilities
      auto caps = detect_capabilities();
      if (!sampling && !caps.has_intel_pt) {
        std::cerr << "warning: Intel PT not available, falling back to "
                     "sampling mode\n";
        sampling = true;
      }

      // 2. Create working directory
      auto work_dir = std::filesystem::temp_directory_path() /
                      ("tracey_mctraceface_" + std::to_string(getpid()));
      std::filesystem::create_directories(work_dir);

      // 3. Build perf config
      PerfConfig perf_config;
      perf_config.multi_thread = multi_thread;
      perf_config.full_execution = full_execution;
      perf_config.sampling = sampling;
      perf_config.snapshot_size_pages =
        static_cast<std::uint32_t>(snapshot_size);
      perf_config.trace_scope = parse_trace_scope(trace_scope_str);
      perf_config.timer_resolution = parse_timer_resolution(timer_res_str);
      perf_config.working_directory = work_dir.string();

      // 4. Fork the target program (stopped)
      std::cerr << "Starting " << program << " ...\n";
      BackgroundProcess target(program_args, true);
      auto target_pid = std::to_string(target.pid());

      // 5. Start perf record
      auto record_args =
        build_perf_record_args(perf_config, caps, {target_pid});
      std::cerr << "Recording with perf (pid " << target_pid << ") ...\n";
      BackgroundProcess perf_record(record_args);

      // Brief pause for perf to attach
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // 6. Resume the target
      target.send_signal(SIGCONT);

      // 7. Wait for target to exit
      auto target_exit = target.wait();
      std::cerr << "Program exited with code " << target_exit << '\n';

      // 8. Signal perf to snapshot and stop
      if (!full_execution && !sampling) {
        if (caps.snapshot_on_exit) {
          perf_record.send_signal(SIGINT);
        } else {
          perf_record.send_signal(SIGUSR2);
        }
        // Brief pause for snapshot
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      perf_record.send_signal(SIGTERM);
      perf_record.wait();

      // 9. Decode the trace
      std::cerr << "Decoding trace ...\n";
      return decode_perf_data(work_dir.string(), output, sampling);
    }

    auto
    run_decode(const nlohmann::json& config) -> int {
      auto sub = config.value("decode", nlohmann::json::object());
      auto working_dir = sub.value("working-directory", "");
      auto output = sub.value("output", "trace.fxt");
      auto sampling = sub.value("sampling", false);

      std::cerr << "Decoding " << working_dir << "/perf.data -> " << output
                << '\n';

      return decode_perf_data(working_dir, output, sampling);
    }

  } // namespace

  auto
  run(const nlohmann::json& config) -> int {
    auto command = config.value("command", "");

    if (command == "run") { return run_run(config); }

    if (command == "attach") {
      auto sub = config.value("attach", nlohmann::json::object());
      auto pid = sub.value("pid", "");
      auto output_path = sub.value("output", "trace.fxt");

      std::cerr << "attach: pid=" << pid << " -> " << output_path << '\n';
      std::cerr << "error: attach command not yet implemented\n";
      return 1;
    }

    if (command == "decode") { return run_decode(config); }

    std::cerr << "error: no command specified\n";
    return 1;
  }

} // namespace tracey_mctraceface
