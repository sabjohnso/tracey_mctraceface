#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tracey_mctraceface {

  /** @brief Branch event kind from Intel PT trace. */
  enum class BranchKind : std::uint8_t {
    Call,
    Return,
    Jump,
    Syscall,
    Sysret,
    HardwareInterrupt,
    Interrupt,
    Iret,
    Async,
    TxAbort
  };

  /** @brief Trace state change from Intel PT. */
  enum class TraceState : std::uint8_t { None, Start, End };

  /** @brief Named sampling event type. */
  enum class SampleEventName : std::uint8_t { BranchMisses, CacheMisses };

  /** @brief A resolved code location from perf script output. */
  struct Location {
    std::uint64_t ip = 0;
    std::string symbol;
    std::int32_t symbol_offset = 0;
  };

  /** @brief Branch trace data: src -> dst with kind and flags. */
  struct BranchData {
    std::optional<BranchKind> kind;
    TraceState trace_state = TraceState::None;
    bool in_transaction = false;
    Location src;
    Location dst;
  };

  /** @brief CPU frequency change event. */
  struct PowerData {
    std::uint32_t freq_mhz = 0;
  };

  /** @brief Sampled callstack from perf. */
  struct StacktraceSampleData {
    std::vector<Location> callstack;
  };

  /** @brief Sampled event (branch-misses, cache-misses). */
  struct EventSampleData {
    Location location;
    int count = 0;
    SampleEventName name = SampleEventName::BranchMisses;
  };

  /** @brief Discriminated union of all event data types. */
  using EventData =
    std::variant<BranchData, PowerData, StacktraceSampleData, EventSampleData>;

  /** @brief Successfully parsed event. */
  struct OkEvent {
    std::uint32_t pid = 0;
    std::uint32_t tid = 0;
    std::uint64_t time_ns = 0;
    EventData data;
  };

  /** @brief Trace decode error from perf. */
  struct DecodeError {
    std::uint32_t pid = 0;
    std::uint32_t tid = 0;
    std::optional<std::uint64_t> time_ns;
    std::optional<std::uint64_t> ip;
    std::string message;
  };

  /** @brief A parsed event: either Ok or DecodeError. */
  using Event = std::variant<OkEvent, DecodeError>;

} // namespace tracey_mctraceface
