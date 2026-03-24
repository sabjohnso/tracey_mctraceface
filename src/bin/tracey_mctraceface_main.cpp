#include "tracey_mctraceface_main.hpp"

#include <tracey_mctraceface/file_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/perf_driver.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>
#include <tracey_mctraceface/subprocess.hpp>

#include <cstdint>
#include <iostream>
#include <string>

namespace tracey_mctraceface {

  namespace {

    auto
    run_decode(const nlohmann::json& config) -> int {
      auto sub = config.value("decode", nlohmann::json::object());
      auto working_dir = sub.value("working-directory", "");
      auto output = sub.value("output", "trace.fxt");
      auto sampling = sub.value("sampling", false);

      // 1. Build perf script arguments
      PerfConfig perf_config;
      perf_config.sampling = sampling;
      auto script_args = build_perf_script_args(perf_config, working_dir);

      std::cerr << "Decoding " << working_dir << "/perf.data -> " << output
                << '\n';

      // 2. Spawn perf script
      Subprocess perf_script(script_args);

      // 3. Set up the output pipeline
      FileSink sink(output);
      FxtWriter writer(sink);
      writer.write_preamble(
        "tracey_mctraceface",
        1,
        1'000'000'000ULL, // ticks_per_second (ns)
        0,
        0);

      StackReconstructor reconstructor(writer);
      PerfScriptParser parser;

      // 4. Read lines, parse events, reconstruct stacks
      std::string line;
      std::uint64_t event_count = 0;

      while (perf_script.read_line(line)) {
        auto event = parser.feed_line(line);
        if (event) {
          reconstructor.process_event(*event);
          ++event_count;
        }
      }

      // Flush remaining
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

  } // namespace

  auto
  run(const nlohmann::json& config) -> int {
    auto command = config.value("command", "");

    if (command == "run") {
      auto sub = config.value("run", nlohmann::json::object());
      auto program = sub.value("program", "");
      auto output_path = sub.value("output", "trace.fxt");

      std::cerr << "run: " << program << " -> " << output_path << '\n';
      std::cerr << "error: run command not yet implemented\n";
      return 1;
    }

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
