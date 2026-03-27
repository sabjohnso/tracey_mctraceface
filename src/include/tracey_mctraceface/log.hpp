#pragma once

#include <string_view>

namespace tracey_mctraceface::log {

  /** @brief Verbosity levels for diagnostic output. */
  enum class Level { Quiet, Normal, Verbose };

  /** @brief Set the global verbosity level. */
  void
  set_level(Level level);

  /** @brief Get the current verbosity level. */
  auto
  get_level() -> Level;

  /** @brief Log an error (always printed). */
  void
  error(std::string_view msg);

  /** @brief Log a warning (always printed). */
  void
  warning(std::string_view msg);

  /** @brief Log a status message (Normal and Verbose). */
  void
  info(std::string_view msg);

  /** @brief Log a debug message (Verbose only). */
  void
  debug(std::string_view msg);

} // namespace tracey_mctraceface::log
