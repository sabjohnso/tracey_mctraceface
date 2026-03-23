#pragma once

#include <tracey_mctraceface/perf_event.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief Parses `perf script` text output into structured events.
   *
   * Feed lines one at a time. Multi-line events (stacktraces) are
   * accumulated and yielded when the next event header arrives.
   */
  class PerfScriptParser {
  public:
    /** @brief Feed a line of perf script output. */
    auto
    feed_line(std::string_view line) -> std::optional<Event>;

    /** @brief Signal end of input; flush any accumulated event. */
    auto
    finish() -> std::optional<Event>;

  private:
    std::vector<std::string> accumulated_lines_;

    auto
    flush_accumulated() -> std::optional<Event>;
    static auto
    parse_group(const std::vector<std::string>& lines) -> std::optional<Event>;
    static auto
    parse_decode_error(std::string_view line) -> std::optional<DecodeError>;
    static auto
    parse_time(std::string_view hi, std::string_view lo) -> std::uint64_t;
    static auto
    parse_location(std::string_view ip_hex, std::string_view sym_str)
      -> Location;
    static auto
    parse_symbol_and_offset(std::string_view str)
      -> std::pair<std::string, std::int32_t>;
  };

} // namespace tracey_mctraceface
