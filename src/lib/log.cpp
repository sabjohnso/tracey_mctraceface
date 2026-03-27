#include <tracey_mctraceface/log.hpp>

#include <iostream>

namespace tracey_mctraceface::log {

  namespace {

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    Level current_level = Level::Normal;

  } // namespace

  void
  set_level(Level level) {
    current_level = level;
  }

  auto
  get_level() -> Level {
    return current_level;
  }

  void
  error(std::string_view msg) {
    std::cerr << "error: " << msg << '\n';
  }

  void
  warning(std::string_view msg) {
    std::cerr << "warning: " << msg << '\n';
  }

  void
  info(std::string_view msg) {
    if (current_level >= Level::Normal) { std::cerr << msg << '\n'; }
  }

  void
  debug(std::string_view msg) {
    if (current_level >= Level::Verbose) { std::cerr << msg << '\n'; }
  }

} // namespace tracey_mctraceface::log
