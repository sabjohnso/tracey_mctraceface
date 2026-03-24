#include "tracey_mctraceface_main.hpp"

#include <tracey_mctraceface/background_process.hpp>
#include <tracey_mctraceface/compressed_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/perf_capabilities.hpp>
#include <tracey_mctraceface/perf_driver.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>
#include <tracey_mctraceface/stats_sampler.hpp>
#include <tracey_mctraceface/subprocess.hpp>
#include <tracey_mctraceface/trace_filter.hpp>
#include <tracey_mctraceface/trace_server.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
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
      const TraceFilter::Config& filter_config = {},
      const std::vector<StatsSampler::Sample>& stats = {}) -> int {
      PerfConfig perf_config;
      perf_config.sampling = sampling;
      auto script_args = build_perf_script_args(perf_config, working_dir);

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

      // Write process stats as counter tracks.
      // Note: counter timestamps use steady_clock, not perf's clock.
      // They show correct relative timing between samples but may not
      // align precisely with trace events. This is intentional — the
      // sampler runs independently to avoid perturbing the trace.
      if (!stats.empty()) {
        auto stats_base = stats.front().timestamp_ns;
        for (const auto& s : stats) {
          auto ts = s.timestamp_ns - stats_base;
          writer.write_counter(
            0,
            0,
            "stats",
            "RSS (bytes)",
            1,
            static_cast<std::int64_t>(s.rss_bytes),
            ts);
          writer.write_counter(
            0,
            0,
            "stats",
            "I/O Read (bytes)",
            2,
            static_cast<std::int64_t>(s.io_read_bytes),
            ts);
          writer.write_counter(
            0,
            0,
            "stats",
            "I/O Write (bytes)",
            3,
            static_cast<std::int64_t>(s.io_write_bytes),
            ts);
        }
        std::cerr << "Wrote " << stats.size() << " stat samples\n";
      }

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
    find_trampoline() -> std::string {
      // Look for trampoline next to the main executable
      auto self = std::filesystem::read_symlink("/proc/self/exe");
      auto trampoline = self.parent_path() / "trampoline";
      if (std::filesystem::exists(trampoline)) return trampoline.string();
      return "trampoline"; // fall back to PATH
    }

    auto
    run_run(const nlohmann::json& config) -> int {
      auto sub = config.value("run", nlohmann::json::object());
      auto program = sub.value("program", "");
      auto output =
        std::filesystem::absolute(sub.value("output", "trace.fxt")).string();

      // Collect program args
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

      // 2. Create working directory
      auto work_dir = std::filesystem::temp_directory_path() /
                      ("tracey_mctraceface_" + std::to_string(getpid()));
      std::filesystem::create_directories(work_dir);
      perf_config.working_directory = work_dir.string();

      // 3. Launch the trampoline.
      //    The trampoline is a tiny binary that waits for a byte on
      //    stdin, then execs into the target. This gives us a known
      //    PID to attach perf and the stats sampler to BEFORE the
      //    target starts, with no tracey_mctraceface code in the trace.
      std::vector<std::string> trampoline_args;
      trampoline_args.push_back(find_trampoline());
      trampoline_args.push_back(program);
      for (const auto& a : program_args) {
        trampoline_args.push_back(a);
      }

      // Use Subprocess to get a pipe to the trampoline's stdin
      // Actually, Subprocess captures stdout. We need stdin control.
      // Use raw fork/pipe for the trampoline.
      int go_pipe[2];
      if (pipe(go_pipe) != 0) { throw std::runtime_error("pipe() failed"); }

      auto trampoline_pid = fork();
      if (trampoline_pid < 0) { throw std::runtime_error("fork() failed"); }

      if (trampoline_pid == 0) {
        // Child: wire pipe read end to stdin
        close(go_pipe[1]);
        dup2(go_pipe[0], STDIN_FILENO);
        close(go_pipe[0]);

        std::vector<char*> argv;
        for (auto& a : trampoline_args) {
          argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
      }

      // Parent: keep write end
      close(go_pipe[0]);
      auto target_pid = std::to_string(trampoline_pid);

      // 4. Attach perf to the trampoline PID (it will follow exec)
      auto record_args =
        build_perf_record_args(perf_config, caps, {target_pid});
      std::cerr << "Recording " << program << " (pid " << target_pid
                << ") ...\n";
      BackgroundProcess perf_record(record_args);

      // 5. Start stats sampler if requested
      auto counters_str = sub.value("counters", "");
      bool want_stats = counters_str.find("rss") != std::string::npos ||
                        counters_str.find("io") != std::string::npos;
      auto interval_ms = sub.value("counter-interval", 10);

      std::unique_ptr<StatsSampler> sampler;
      if (want_stats) {
        sampler = std::make_unique<StatsSampler>(
          trampoline_pid, std::chrono::milliseconds(interval_ms));
        sampler->start();
        std::cerr << "Stats sampler started (interval " << interval_ms
                  << " ms)\n";
      }

      // Brief pause for perf to attach
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // 6. Signal the trampoline to exec into the target
      char go = 1;
      write(go_pipe[1], &go, 1);
      close(go_pipe[1]);

      // 7. Wait for perf (and the target) to finish
      auto perf_exit = perf_record.wait();

      // Also reap the trampoline/target process
      waitpid(trampoline_pid, nullptr, WNOHANG);

      std::cerr << "perf record exited with code " << perf_exit << '\n';

      std::vector<StatsSampler::Sample> stats;
      if (sampler) {
        sampler->stop();
        stats = sampler->samples();
      }

      // 8. Decode the trace
      std::cerr << "Decoding trace ...\n";
      auto result = decode_perf_data(
        work_dir.string(),
        output,
        perf_config.sampling,
        build_filter_config(sub),
        stats);
      maybe_serve(sub, output);
      return result;
    }

    auto
    run_attach(const nlohmann::json& config) -> int {
      auto sub = config.value("attach", nlohmann::json::object());
      auto pid_str = sub.value("pid", "");
      auto output =
        std::filesystem::absolute(sub.value("output", "trace.fxt")).string();

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

      // 2. Create working directory
      auto work_dir = std::filesystem::temp_directory_path() /
                      ("tracey_mctraceface_" + std::to_string(getpid()));
      std::filesystem::create_directories(work_dir);
      perf_config.working_directory = work_dir.string();

      // 3. Start perf record
      auto record_args = build_perf_record_args(perf_config, caps, pids);
      std::cerr << "Attaching to PID(s) " << pid_str << " ...\n";
      BackgroundProcess perf_record(record_args);

      // Brief pause for perf to attach
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // 4. Start stats sampler if requested (attach knows the target PID)
      auto counters_str = sub.value("counters", "");
      bool want_stats = counters_str.find("rss") != std::string::npos ||
                        counters_str.find("io") != std::string::npos;
      auto interval_ms = sub.value("counter-interval", 10);

      std::unique_ptr<StatsSampler> sampler;
      if (want_stats && !pids.empty()) {
        auto target_pid = static_cast<pid_t>(std::stoi(pids[0]));
        sampler = std::make_unique<StatsSampler>(
          target_pid, std::chrono::milliseconds(interval_ms));
        sampler->start();
        std::cerr << "Stats sampler started for PID " << pids[0]
                  << " (interval " << interval_ms << " ms)\n";
      }

      // 5. Wait for Ctrl+C
      std::cerr << "Recording. Press Ctrl+C to stop.\n";
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
      std::cerr << "\nStopping recording ...\n";
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

      std::vector<StatsSampler::Sample> stats;
      if (sampler) {
        sampler->stop();
        stats = sampler->samples();
      }

      // 6. Decode
      std::cerr << "Decoding trace ...\n";
      auto result = decode_perf_data(
        work_dir.string(),
        output,
        perf_config.sampling,
        build_filter_config(sub),
        stats);
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

      std::cerr << "Decoding " << working_dir << "/perf.data -> " << output
                << '\n';

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
