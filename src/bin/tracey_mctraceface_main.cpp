#include "tracey_mctraceface_main.hpp"

#include <tracey_mctraceface/background_process.hpp>
#include <tracey_mctraceface/compressed_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/log.hpp>
#include <tracey_mctraceface/perf_capabilities.hpp>
#include <tracey_mctraceface/perf_driver.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>
#include <tracey_mctraceface/subprocess.hpp>
#include <tracey_mctraceface/trace_filter.hpp>
#include <tracey_mctraceface/trace_server.hpp>

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

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    volatile sig_atomic_t g_interrupted = 0;

    void
    sigint_handler(int /*sig*/) {
      g_interrupted = 1;
    }

    auto
    decode_perf_data(
      const std::string& working_dir,
      const std::string& output,
      bool sampling,
      const TraceFilter::Config& filter_config = {}) -> int {
      PerfConfig perf_config;
      perf_config.sampling = sampling;
      auto script_args = build_perf_script_args(perf_config, working_dir);

      {
        std::string cmd = "perf script command:";
        for (const auto& a : script_args) {
          cmd += " " + a;
        }
        log::debug(cmd);
      }

      Subprocess perf_script(script_args);

      auto sink = make_sink(output);
      FxtWriter writer(*sink);

      // Use current wall clock as base_time so Perfetto has a time reference
      auto now = std::chrono::system_clock::now();
      auto base_time_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          now.time_since_epoch())
          .count());

      writer.write_preamble(
        "tracey_mctraceface", 0, 1'000'000'000ULL, 0, base_time_ns);

      StackReconstructor reconstructor(writer);
      PerfScriptParser parser;
      TraceFilter filter(filter_config);

      std::string line;
      std::uint64_t event_count = 0;

      while (perf_script.read_line(line)) {
        auto event = parser.feed_line(line);
        if (event && filter.should_pass(*event)) {
          reconstructor.process_event(*event);
          ++event_count;
        }
      }

      if (auto event = parser.finish()) {
        if (filter.should_pass(*event)) {
          reconstructor.process_event(*event);
          ++event_count;
        }
      }
      reconstructor.finish();

      if (filter.start_symbol_missing()) {
        std::cerr << "warning: start symbol '" << filter_config.start_symbol
                  << "' was not found in the trace\n";
      }
      if (filter.slice_count() > 0) {
        std::cerr << "Recorded " << filter.slice_count() << " slice(s)\n";
      }

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

    void
    setup_logging(const nlohmann::json& sub) {
      if (sub.value("quiet", false)) {
        log::set_level(log::Level::Quiet);
      } else if (sub.value("verbose", false)) {
        log::set_level(log::Level::Verbose);
      } else {
        log::set_level(log::Level::Normal);
      }
    }

    auto
    parse_timer_resolution(const std::string& s) -> TimerResolution {
      if (s == "low") return TimerResolution::Low;
      if (s == "high") return TimerResolution::High;
      return TimerResolution::Normal;
    }

    void
    maybe_serve(const nlohmann::json& sub, const std::string& output) {
      if (sub.value("serve", false)) {
        auto port = static_cast<std::uint16_t>(sub.value("serve-port", 8080));
        serve_trace(output, port);
      }
    }

    auto
    build_filter_config(const nlohmann::json& sub) -> TraceFilter::Config {
      return {
        .start_symbol = sub.value("start-symbol", ""),
        .end_symbol = sub.value("end-symbol", ""),
        .multi_slice = sub.value("multi-slice", false),
      };
    }

    auto
    parse_pids(const std::string& pid_str) -> std::vector<std::string> {
      std::vector<std::string> pids;
      std::string current;
      for (char c : pid_str) {
        if (c == ',') {
          if (!current.empty()) {
            pids.push_back(current);
            current.clear();
          }
        } else {
          current += c;
        }
      }
      if (!current.empty()) pids.push_back(current);
      return pids;
    }

    auto
    build_config_from_sub(const nlohmann::json& sub) -> PerfConfig {
      PerfConfig config;
      config.multi_thread = sub.value("multi-thread", false);
      config.full_execution = sub.value("full-execution", false);
      config.sampling = sub.value("sampling", false);
      config.snapshot_size_pages =
        static_cast<std::uint32_t>(sub.value("snapshot-size", 0));
      config.trace_scope =
        parse_trace_scope(sub.value("trace-scope", "userspace"));
      config.timer_resolution =
        parse_timer_resolution(sub.value("timer-resolution", "normal"));
      return config;
    }

    auto
    run_run(const nlohmann::json& config) -> int {
      auto sub = config.value("run", nlohmann::json::object());
      auto program = sub.value("program", "");
      auto output_val = sub.value("output", "trace.fxt");
      if (sub.value("no-decode", false) && output_val == "trace.fxt") {
        output_val = "perf.data";
      }
      auto output = std::filesystem::absolute(output_val).string();

      // Collect program args (without the program name itself)
      std::vector<std::string> program_args;
      if (sub.contains("args") && sub["args"].is_array()) {
        for (const auto& arg : sub["args"]) {
          program_args.push_back(arg.get<std::string>());
        }
      }

      auto perf_config = build_config_from_sub(sub);

      // 1. Detect capabilities
      auto caps = detect_capabilities();
      if (!perf_config.sampling && !caps.has_intel_pt) {
        std::cerr << "warning: Intel PT not available, falling back to "
                     "sampling mode\n";
        perf_config.sampling = true;
      }

      // 2. Create working directory and configure perf output
      auto no_decode = sub.value("no-decode", false);
      std::filesystem::path work_dir;
      std::filesystem::path perf_data_path;

      if (no_decode) {
        // --no-decode: write perf.data to -o path (or "perf.data")
        perf_data_path = std::filesystem::absolute(output);
        work_dir = perf_data_path.parent_path();
      } else {
        work_dir = std::filesystem::temp_directory_path() /
                   ("tracey_mctraceface_" + std::to_string(getpid()));
        perf_data_path = work_dir / "perf.data";
      }
      std::filesystem::create_directories(work_dir);
      perf_config.working_directory = work_dir.string();

      // 3. Let perf record launch the target program directly.
      auto record_args =
        build_perf_record_args(perf_config, caps, program, program_args);

      // Override the perf.data output path for --no-decode
      if (no_decode) {
        for (std::size_t i = 0; i < record_args.size(); ++i) {
          if (record_args[i] == "-o" && i + 1 < record_args.size()) {
            record_args[i + 1] = perf_data_path.string();
            break;
          }
        }
      }

      setup_logging(sub);

      {
        std::string cmd = "perf record command:";
        for (const auto& a : record_args) {
          cmd += " " + a;
        }
        log::debug(cmd);
      }

      log::info("Recording " + program + " ...");
      BackgroundProcess perf_record(record_args);

      // 4. Wait for perf (and the target) to finish
      auto perf_exit = perf_record.wait();
      std::cerr << "perf record exited with code " << perf_exit << '\n';

      // 5. Decode or save for later
      if (no_decode) {
        std::cerr << "Saved to: " << perf_data_path.string() << '\n';
        return 0;
      }

      log::info("Decoding trace ...");
      auto result = decode_perf_data(
        work_dir.string(),
        output,
        perf_config.sampling,
        build_filter_config(sub));
      maybe_serve(sub, output);
      return result;
    }

    auto
    run_attach(const nlohmann::json& config) -> int {
      auto sub = config.value("attach", nlohmann::json::object());
      auto pid_str = sub.value("pid", "");
      auto output_val = sub.value("output", "trace.fxt");
      if (sub.value("no-decode", false) && output_val == "trace.fxt") {
        output_val = "perf.data";
      }
      auto output = std::filesystem::absolute(output_val).string();

      auto pids = parse_pids(pid_str);
      if (pids.empty()) {
        std::cerr << "error: no PIDs specified\n";
        return 1;
      }

      auto perf_config = build_config_from_sub(sub);

      // 1. Detect capabilities
      auto caps = detect_capabilities();
      if (!perf_config.sampling && !caps.has_intel_pt) {
        std::cerr << "warning: Intel PT not available, falling back to "
                     "sampling mode\n";
        perf_config.sampling = true;
      }

      // 2. Create working directory and configure perf output
      auto no_decode = sub.value("no-decode", false);
      std::filesystem::path work_dir;
      std::filesystem::path perf_data_path;

      if (no_decode) {
        perf_data_path = std::filesystem::absolute(output);
        work_dir = perf_data_path.parent_path();
      } else {
        work_dir = std::filesystem::temp_directory_path() /
                   ("tracey_mctraceface_" + std::to_string(getpid()));
        perf_data_path = work_dir / "perf.data";
      }
      std::filesystem::create_directories(work_dir);
      perf_config.working_directory = work_dir.string();

      // 3. Start perf record
      auto record_args = build_perf_record_args(perf_config, caps, pids);

      if (no_decode) {
        for (std::size_t i = 0; i < record_args.size(); ++i) {
          if (record_args[i] == "-o" && i + 1 < record_args.size()) {
            record_args[i + 1] = perf_data_path.string();
            break;
          }
        }
      }

      setup_logging(sub);

      {
        std::string cmd = "perf record command:";
        for (const auto& a : record_args) {
          cmd += " " + a;
        }
        log::debug(cmd);
      }

      log::info("Attaching to PID(s) " + pid_str + " ...");
      BackgroundProcess perf_record(record_args);

      // Brief pause for perf to attach
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // 4. Wait for Ctrl+C
      log::info("Recording. Press Ctrl+C to stop.");
      g_interrupted = 0;

      struct sigaction sa{};
      sa.sa_handler = sigint_handler;
      sigemptyset(&sa.sa_mask);
      sigaction(SIGINT, &sa, nullptr);

      while (g_interrupted == 0) {
        // Check if perf exited unexpectedly
        if (auto code = perf_record.try_wait()) {
          std::cerr << "perf record exited unexpectedly (code " << *code
                    << ")\n";
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      // Restore default SIGINT handler
      sa.sa_handler = SIG_DFL;
      sigaction(SIGINT, &sa, nullptr);

      // 5. Signal perf to snapshot and stop
      log::info("Stopping recording ...");
      if (!perf_config.full_execution && !perf_config.sampling) {
        if (caps.snapshot_on_exit) {
          perf_record.send_signal(SIGINT);
        } else {
          perf_record.send_signal(SIGUSR2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      perf_record.send_signal(SIGTERM);
      perf_record.wait();

      // 6. Decode or save for later
      if (no_decode) {
        std::cerr << "Saved to: " << perf_data_path.string() << '\n';
        return 0;
      }

      log::info("Decoding trace ...");
      auto result = decode_perf_data(
        work_dir.string(),
        output,
        perf_config.sampling,
        build_filter_config(sub));
      maybe_serve(sub, output);
      return result;
    }

    auto
    run_decode(const nlohmann::json& config) -> int {
      auto sub = config.value("decode", nlohmann::json::object());
      auto working_dir = sub.value("working-directory", "");
      auto output =
        std::filesystem::absolute(sub.value("output", "trace.fxt")).string();
      auto sampling = sub.value("sampling", false);
      setup_logging(sub);

      log::info("Decoding " + working_dir + " -> " + output);

      auto result = decode_perf_data(
        working_dir, output, sampling, build_filter_config(sub));
      maybe_serve(sub, output);
      return result;
    }

  } // namespace

  auto
  run(const nlohmann::json& config) -> int {
    try {
      auto command = config.value("command", "");

      if (command == "run") { return run_run(config); }

      if (command == "attach") { return run_attach(config); }

      if (command == "decode") { return run_decode(config); }

      std::cerr << "error: no command specified\n";
      return 1;
    } catch (const std::exception& e) {
      std::cerr << "error: " << e.what() << '\n';
      return 1;
    }
  }

} // namespace tracey_mctraceface
