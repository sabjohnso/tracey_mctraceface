#include <tracey_mctraceface/trace_filter.hpp>

#include <variant>

namespace tracey_mctraceface {

  TraceFilter::TraceFilter(Config config)
      : config_(std::move(config)) {
    // If no start_symbol, start recording immediately
    state_ = config_.start_symbol.empty() ? State::Recording : State::Waiting;
  }

  auto
  TraceFilter::should_pass(const Event& event) -> bool {
    // Non-branch events (decode errors, etc.) pass through if recording
    auto* ok = std::get_if<OkEvent>(&event);
    if (!ok) return state_ == State::Recording;

    auto* br = std::get_if<BranchData>(&ok->data);
    if (!br) return state_ == State::Recording;

    switch (state_) {
      case State::Waiting:
        if (matches_start(*br)) {
          saw_start_ = true;
          state_ = State::Recording;
          return true;
        }
        return false;

      case State::Recording:
        if (!config_.end_symbol.empty() && matches_end(*br)) {
          ++slice_count_;
          if (config_.multi_slice) {
            state_ =
              config_.start_symbol.empty() ? State::Recording : State::Waiting;
          } else {
            state_ = State::Done;
          }
          return true; // include the end event itself
        }
        return true;

      case State::Done:
        return false;
    }

    return false;
  }

  auto
  TraceFilter::slice_count() const -> int {
    return slice_count_;
  }

  auto
  TraceFilter::start_symbol_missing() const -> bool {
    return !config_.start_symbol.empty() && !saw_start_;
  }

  auto
  TraceFilter::matches_start(const BranchData& br) const -> bool {
    return symbol_matches(br, config_.start_symbol);
  }

  auto
  TraceFilter::matches_end(const BranchData& br) const -> bool {
    return symbol_matches(br, config_.end_symbol);
  }

  auto
  TraceFilter::symbol_matches(const BranchData& br, const std::string& target)
    -> bool {
    if (target.empty()) return false;
    return br.src.symbol.starts_with(target) ||
           br.dst.symbol.starts_with(target);
  }

} // namespace tracey_mctraceface
