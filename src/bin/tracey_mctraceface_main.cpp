#include "tracey_mctraceface_main.hpp"

#include <tracey_mctraceface/file_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/output_sink.hpp>
#include <tracey_mctraceface/perf_capabilities.hpp>
#include <tracey_mctraceface/perf_driver.hpp>

#include <iostream>
#include <string>

namespace tracey_mctraceface {

  auto
  run(const nlohmann::json& config) -> int {
    auto command = config.value("command", "");

    if (command == "run") {
      auto program = config.value("program", "");
      auto output = config.value("output", "trace.fxt");

      std::cout << "run: " << program << " -> " << output << '\n';

      // TODO: implement run command
      //  1. Detect capabilities
      //  2. Fork/exec the program with ptrace
      //  3. Start perf record
      //  4. Wait for trigger or exit
      //  5. Run perf script
      //  6. Parse events, reconstruct stacks, write FXT

      return 0;
    }

    if (command == "attach") {
      auto pid = config.value("pid", "");
      auto output = config.value("output", "trace.fxt");

      std::cout << "attach: pid=" << pid << " -> " << output << '\n';

      // TODO: implement attach command

      return 0;
    }

    if (command == "decode") {
      auto working_dir = config.value("working-directory", "");
      auto output = config.value("output", "trace.fxt");

      std::cout << "decode: " << working_dir << " -> " << output << '\n';

      // TODO: implement decode command
      //  1. Build perf script args
      //  2. Spawn perf script
      //  3. Parse output
      //  4. Reconstruct stacks
      //  5. Write FXT

      return 0;
    }

    std::cerr << "error: no command specified\n";
    return 1;
  }

} // namespace tracey_mctraceface
