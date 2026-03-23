#pragma once

#include <string>
#include <string_view>

namespace tracey_mctraceface {

  /**
   * @brief Shorten a C++ symbol for trace display.
   *
   * Collapses template parameters (vector<int> → vector<...>) and
   * strips function parameter lists (foo(int) → foo), while preserving
   * operator< and operator().
   */
  auto
  shorten_symbol(std::string_view symbol) -> std::string;

} // namespace tracey_mctraceface
