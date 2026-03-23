#pragma once

#include <nlohmann/json.hpp>

namespace tracey_mctraceface {

  /**
   * @brief Main entry point after CLI argument parsing.
   *
   * Receives the parsed configuration as JSON and dispatches to the
   * appropriate command (run, attach, or decode).
   */
  auto
  run(const nlohmann::json& config) -> int;

} // namespace tracey_mctraceface
