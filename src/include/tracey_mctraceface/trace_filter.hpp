#pragma once

#include <tracey_mctraceface/perf_event.hpp>

#include <string>

namespace tracey_mctraceface {

  /**
   * @brief Filters trace events to extract regions between start and
   *        end symbol markers.
   *
   * Sits between the parser and the stack reconstructor. Observes
   * symbol names in branch events and controls which events pass
   * through based on a start/end symbol state machine.
   */
  class TraceFilter {
  public:
    struct Config {
      std::string start_symbol; // empty = start from beginning
      std::string end_symbol;   // empty = record until trace ends
      bool multi_slice = false; // collect multiple start→end regions
    };

    explicit TraceFilter(Config config);

    /**
     * @brief Check whether an event should be passed to the reconstructor.
     *
     * Also updates internal state when start/end symbols are observed.
     * Returns true if the event is within a recording region.
     */
    auto
    should_pass(const Event& event) -> bool;

    /** @brief Number of complete slices recorded so far. */
    auto
    slice_count() const -> int;

    /** @brief True if a start_symbol was configured but never seen. */
    auto
    start_symbol_missing() const -> bool;

  private:
    enum class State { Waiting, Recording, Done };

    Config config_;
    State state_;
    int slice_count_ = 0;
    bool saw_start_ = false;

    auto
    matches_start(const BranchData& br) const -> bool;
    auto
    matches_end(const BranchData& br) const -> bool;
    static auto
    symbol_matches(const BranchData& br, const std::string& target) -> bool;
  };

} // namespace tracey_mctraceface
