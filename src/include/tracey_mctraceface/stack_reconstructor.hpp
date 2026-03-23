#pragma once

#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/perf_event.hpp>
#include <tracey_mctraceface/shorten_symbol.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief Reconstructs per-thread call stacks from parsed events and
   *        emits FXT duration events.
   *
   * Maintains per-thread state machines that track call stacks through
   * calls, returns, jumps, syscalls, and trace discontinuities.
   */
  class StackReconstructor {
  public:
    explicit StackReconstructor(FxtWriter& writer);

    /** @brief Process a single parsed event. */
    void
    process_event(const Event& event);

    /** @brief Flush all pending events and finalize. */
    void
    finish();

  private:
    struct CallInfo {
      Location location;
      bool from_untraced = false;
    };

    enum class PendingKind { Call, Ret, RetFromUntraced };

    struct PendingEvent {
      std::string symbol;
      PendingKind kind;
      CallInfo call_info;           // only valid for Call
      std::uint64_t reset_time = 0; // only valid for RetFromUntraced
    };

    struct Callstack {
      std::vector<Location> stack;
      std::uint64_t create_time = 0;
    };

    struct ThreadKey {
      std::uint32_t pid;
      std::uint32_t tid;
      auto
      operator==(const ThreadKey&) const -> bool = default;
    };

    struct ThreadKeyHash {
      auto
      operator()(const ThreadKey& k) const -> std::size_t;
    };

    struct ThreadInfo {
      Callstack callstack;
      std::vector<Callstack> inactive_callstacks;
      std::vector<PendingEvent> pending_events;
      std::uint64_t pending_time = 0;
      std::uint64_t last_event_time = 0;
    };

    FxtWriter& writer_;
    std::uint64_t base_time_ = 0;
    bool base_time_set_ = false;
    std::unordered_map<ThreadKey, ThreadInfo, ThreadKeyHash> threads_;

    auto
    get_thread(std::uint32_t pid, std::uint32_t tid) -> ThreadInfo&;

    auto
    map_time(std::uint64_t time_ns) -> std::uint64_t;

    void
    call(
      ThreadInfo& ti,
      std::uint32_t pid,
      std::uint32_t tid,
      std::uint64_t time,
      const Location& loc,
      bool from_untraced = false);

    void
    ret(
      ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time);

    void
    check_current_symbol(
      ThreadInfo& ti,
      std::uint32_t pid,
      std::uint32_t tid,
      std::uint64_t time,
      const Location& loc);

    void
    clear_callstack(
      ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time);

    void
    clear_all_callstacks(
      ThreadInfo& ti, std::uint32_t pid, std::uint32_t tid, std::uint64_t time);

    void
    add_event(
      ThreadInfo& ti,
      std::uint32_t pid,
      std::uint32_t tid,
      std::uint64_t time,
      PendingEvent ev);

    void
    flush(
      ThreadInfo& ti,
      std::uint32_t pid,
      std::uint32_t tid,
      std::uint64_t to_time);

    void
    write_pending(
      std::uint32_t pid,
      std::uint32_t tid,
      std::uint64_t time,
      const PendingEvent& ev);

    void
    process_branch(const OkEvent& ok, const BranchData& br);

    void
    process_stacktrace(const OkEvent& ok, const StacktraceSampleData& st);
  };

} // namespace tracey_mctraceface
