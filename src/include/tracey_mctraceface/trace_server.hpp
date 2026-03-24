#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace tracey_mctraceface {

  /**
   * @brief Serve a trace file via HTTP and open Perfetto UI in the browser.
   *
   * Starts a minimal HTTP server on localhost that serves the trace file
   * with CORS headers. Opens `https://ui.perfetto.dev/#!/?url=...` in the
   * default browser. Blocks until Ctrl+C or the server is stopped.
   *
   * @param trace_path Path to the .fxt trace file to serve.
   * @param port TCP port to listen on (default 8080).
   */
  void
  serve_trace(
    const std::filesystem::path& trace_path, std::uint16_t port = 8080);

} // namespace tracey_mctraceface
