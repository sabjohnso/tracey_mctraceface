#include <tracey_mctraceface/stack_reconstructor.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

namespace tracey_mctraceface {

  StackReconstructor::StackReconstructor(FxtWriter& writer)
      : writer_(writer) {
    category_id_ = writer_.intern_string("trace");
  }

  auto
  StackReconstructor::ThreadKeyHash::operator()(const ThreadKey& k) const
    -> std::size_t {
    auto h1 = std::hash<std::uint32_t>{}(k.pid);
    auto h2 = std::hash<std::uint32_t>{}(k.tid);
    return h1 ^ (h2 << 1);
  }

  auto
  StackReconstructor::get_thread(std::uint32_t pid, std::uint32_t tid)
    -> ThreadInfo& {
    ThreadKey key{pid, tid};
    auto [it, inserted] = threads_.try_emplace(key);
    return it->second;
  }

  auto
  StackReconstructor::map_time(std::uint64_t time_ns) -> std::uint64_t {
    if (!base_time_set_) {
      base_time_ = time_ns;
      base_time_set_ = true;
    }
    return time_ns - base_time_;
  }

  void
  StackReconstructor::call(
    ThreadInfo& ti,
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t time,
    const Location& loc,
    bool from_untraced) {
    PendingEvent ev;
    ev.symbol = loc.symbol;
    ev.kind = PendingKind::Call;
    ev.call_info = {loc, from_untraced};
    add_event(ti, pid, tid, time, std::move(ev));
    ti.callstack.stack.push_back(loc);
  }

  void
  StackReconstructor::ret(
    ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time) {
    if (!ti.callstack.stack.empty()) {
      PendingEvent ev;
      ev.symbol = ti.callstack.stack.back().symbol;
      ev.kind = PendingKind::Ret;
      add_event(ti, pid, tid, time, std::move(ev));
      ti.callstack.stack.pop_back();
    } else {
      // Return from code that wasn't traced — emit a complete duration
      PendingEvent ev;
      ev.symbol = "[unknown]";
      ev.kind = PendingKind::RetFromUntraced;
      ev.reset_time = ti.callstack.create_time;
      add_event(ti, pid, tid, time, std::move(ev));
    }
  }

  void
  StackReconstructor::check_current_symbol(
    ThreadInfo& ti,
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t time,
    const Location& loc) {
    if (ti.callstack.stack.empty()) {
      // Stack is empty but we're executing code — inferred call
      call(ti, pid, tid, ti.callstack.create_time, loc, true);
      return;
    }

    auto& top = ti.callstack.stack.back();
    if (top.symbol != loc.symbol) {
      // Mismatch: pop the wrong frame, push the right one
      ret(ti, pid, tid, time);
      call(ti, pid, tid, time, loc);
    }
  }

  void
  StackReconstructor::clear_callstack(
    ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time) {
    while (!ti.callstack.stack.empty()) {
      ret(ti, pid, tid, time);
    }
  }

  void
  StackReconstructor::clear_all_callstacks(
    ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time) {
    clear_callstack(ti, pid, tid, time);
    while (!ti.inactive_callstacks.empty()) {
      ti.callstack = std::move(ti.inactive_callstacks.back());
      ti.inactive_callstacks.pop_back();
      clear_callstack(ti, pid, tid, time);
    }
  }

  void
  StackReconstructor::add_event(
    ThreadInfo& ti,
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t time,
    PendingEvent ev) {
    if (time != ti.pending_time && !ti.pending_events.empty()) {
      flush(ti, pid, tid, time);
    }
    ti.pending_time = time;
    ti.pending_events.push_back(std::move(ev));
  }

  void
  StackReconstructor::flush(
    ThreadInfo& ti,
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t to_time) {
    if (ti.pending_events.empty()) return;

    auto total_ns =
      (to_time > ti.pending_time) ? (to_time - ti.pending_time) : 0ULL;

    // Count time-consuming events (only Calls consume time)
    int time_consumers = 0;
    for (const auto& ev : ti.pending_events) {
      if (ev.kind == PendingKind::Call) ++time_consumers;
    }

    // Distribute time evenly across time-consuming events
    std::uint64_t ns_offset = 0;
    int shares_consumed = 0;

    for (auto& ev : ti.pending_events) {
      std::uint64_t ns_share = 0;
      if (ev.kind == PendingKind::Call) {
        ++shares_consumed;
        auto remaining = time_consumers - shares_consumed + 1;
        ns_share = (remaining > 0) ? (total_ns - ns_offset) / remaining : 0;
      }

      auto event_time = ti.pending_time + ns_offset;
      write_pending(pid, tid, event_time, ev);
      ns_offset += ns_share;
    }

    ti.pending_time = to_time;
    ti.pending_events.clear();
  }

  auto
  StackReconstructor::intern_symbol(const std::string& raw_symbol)
    -> std::uint16_t {
    auto it = symbol_id_cache_.find(raw_symbol);
    if (it != symbol_id_cache_.end()) return it->second;

    auto shortened = shorten_symbol(raw_symbol);
    auto id = writer_.intern_string(shortened);
    symbol_id_cache_.emplace(raw_symbol, id);
    return id;
  }

  void
  StackReconstructor::write_pending(
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t time,
    const PendingEvent& ev) {
    // intern_symbol caches shorten_symbol + intern_string results,
    // so each unique raw symbol is shortened and interned exactly once.
    auto name_id = intern_symbol(ev.symbol);
    auto thread_ref = writer_.ensure_thread(pid, tid);

    switch (ev.kind) {
      case PendingKind::Call:
        writer_.write_event_by_id(2, thread_ref, category_id_, name_id, time);
        break;
      case PendingKind::Ret:
        writer_.write_event_by_id(3, thread_ref, category_id_, name_id, time);
        break;
      case PendingKind::RetFromUntraced: {
        auto unknown_id = intern_symbol("[unknown]");
        writer_.write_event_by_id(
          2, thread_ref, category_id_, unknown_id, ev.reset_time);
        writer_.write_event_by_id(
          3, thread_ref, category_id_, unknown_id, time);
        break;
      }
    }
  }

  void
  StackReconstructor::process_event(const Event& event) {
    std::visit(
      [this](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, OkEvent>) {
          std::visit(
            [this, &e](const auto& data) {
              using D = std::decay_t<decltype(data)>;
              if constexpr (std::is_same_v<D, BranchData>) {
                process_branch(e, data);
              } else if constexpr (std::is_same_v<D, StacktraceSampleData>) {
                process_stacktrace(e, data);
              } else if constexpr (std::is_same_v<D, PowerData>) {
                // Power events don't affect call stacks
              } else if constexpr (std::is_same_v<D, EventSampleData>) {
                // Sample events don't affect call stacks (for now)
              }
            },
            e.data);
        } else if constexpr (std::is_same_v<T, DecodeError>) {
          if (e.time_ns.has_value()) {
            auto time = map_time(*e.time_ns);
            auto& ti = get_thread(e.pid, e.tid);
            ti.last_event_time = time;
            clear_all_callstacks(ti, e.pid, e.tid, time);
          }
        }
      },
      event);
  }

  void
  StackReconstructor::process_branch(const OkEvent& ok, const BranchData& br) {
    auto time = map_time(ok.time_ns);
    auto& ti = get_thread(ok.pid, ok.tid);
    ti.last_event_time = time;

    auto trace_state = br.trace_state;
    auto kind = br.kind;

    // Trace end: push an [untraced] marker. The callstack is preserved
    // so that when tracing resumes (tr strt), we can continue from
    // where we left off without fragmenting long-running functions.
    if (trace_state == TraceState::End) {
      Location untraced;
      untraced.symbol = "[untraced]";
      call(ti, ok.pid, ok.tid, time, untraced);
      return;
    }

    // Trace start: pop the [untraced] marker and resume.
    if (trace_state == TraceState::Start) {
      // Pop [untraced] marker if present
      if (
        !ti.callstack.stack.empty() &&
        ti.callstack.stack.back().symbol == "[untraced]") {
        ret(ti, ok.pid, ok.tid, time);
      }
      check_current_symbol(ti, ok.pid, ok.tid, time, br.dst);
      // If there's also a kind (e.g., tr strt jmp), process it below
      if (!kind.has_value()) return;
    }

    if (!kind.has_value()) return;

    switch (*kind) {
      case BranchKind::Call:
        check_current_symbol(ti, ok.pid, ok.tid, time, br.src);
        call(ti, ok.pid, ok.tid, time, br.dst);
        break;

      case BranchKind::Return:
        ret(ti, ok.pid, ok.tid, time);
        check_current_symbol(ti, ok.pid, ok.tid, time, br.dst);
        break;

      case BranchKind::Jump:
        check_current_symbol(ti, ok.pid, ok.tid, time, br.dst);
        break;

      case BranchKind::Syscall:
      case BranchKind::HardwareInterrupt:
        // Save current callstack, start a new one
        ti.inactive_callstacks.push_back(std::move(ti.callstack));
        ti.callstack = Callstack{};
        ti.callstack.create_time = time;
        call(ti, ok.pid, ok.tid, time, br.dst);
        break;

      case BranchKind::Iret:
      case BranchKind::Sysret:
        // Clear current callstack, restore previous
        clear_callstack(ti, ok.pid, ok.tid, time);
        if (!ti.inactive_callstacks.empty()) {
          ti.callstack = std::move(ti.inactive_callstacks.back());
          ti.inactive_callstacks.pop_back();
          check_current_symbol(ti, ok.pid, ok.tid, time, br.dst);
        } else {
          ti.callstack.create_time = time;
        }
        break;

      case BranchKind::Interrupt:
      case BranchKind::Async:
      case BranchKind::TxAbort:
        check_current_symbol(ti, ok.pid, ok.tid, time, br.dst);
        break;
    }
  }

  void
  StackReconstructor::process_stacktrace(
    const OkEvent& ok, const StacktraceSampleData& st) {
    auto time = map_time(ok.time_ns);
    auto& ti = get_thread(ok.pid, ok.tid);
    ti.last_event_time = time;

    auto& current = ti.callstack.stack;
    auto& sample = st.callstack;

    // Find how many frames match from the bottom up
    std::size_t match_count = 0;
    auto min_size = std::min(current.size(), sample.size());
    for (std::size_t i = 0; i < min_size; ++i) {
      if (current[i].symbol == sample[i].symbol)
        ++match_count;
      else
        break;
    }

    // Pop excess frames from current stack
    while (current.size() > match_count) {
      ret(ti, ok.pid, ok.tid, time);
    }

    // Push new frames from sample
    for (std::size_t i = match_count; i < sample.size(); ++i) {
      call(ti, ok.pid, ok.tid, time, sample[i]);
    }
  }

  void
  StackReconstructor::finish() {
    for (auto& [key, ti] : threads_) {
      flush(ti, key.pid, key.tid, ti.last_event_time);
      clear_all_callstacks(ti, key.pid, key.tid, ti.last_event_time);
      flush(ti, key.pid, key.tid, ti.last_event_time);
    }
  }

} // namespace tracey_mctraceface
