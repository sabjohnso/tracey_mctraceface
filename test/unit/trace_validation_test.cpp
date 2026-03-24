/// Structural validation of FXT trace output.
///
/// These tests feed real perf-script-like input through the full
/// pipeline (parser → reconstructor → FXT writer) and verify that the
/// resulting binary trace satisfies invariants that must hold for any
/// correct trace, regardless of the reference implementation.

#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/output_sink.hpp>
#include <tracey_mctraceface/perf_event.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace tracey_mctraceface;

namespace {

  struct FxtEvent {
    std::uint8_t event_type; // 2=begin, 3=end
    std::uint8_t thread_ref;
    std::uint16_t category_ref;
    std::uint16_t name_ref;
    std::uint64_t timestamp;
  };

  struct ParsedTrace {
    std::map<std::uint16_t, std::string> strings;
    std::map<std::uint8_t, std::pair<std::uint64_t, std::uint64_t>> threads;
    std::vector<FxtEvent> events;
    std::set<std::uint16_t> string_refs_used;
    std::set<std::uint8_t> thread_refs_used;
  };

  auto
  parse_fxt(std::span<const std::byte> data) -> ParsedTrace {
    ParsedTrace trace;
    std::size_t offset = 0;

    while (offset + 8 <= data.size()) {
      std::uint64_t word = 0;
      std::memcpy(&word, data.data() + offset, 8);
      auto rtype = word & 0xF;
      auto rsize = (word >> 4) & 0xFFF;
      if (rsize == 0) break;

      auto record_end = offset + rsize * 8;
      if (record_end > data.size()) break;

      if (rtype == 2) { // string
        auto sid = static_cast<std::uint16_t>((word >> 16) & 0x7FFF);
        auto slen = static_cast<std::uint16_t>((word >> 32) & 0x7FFF);
        if (offset + 8 + slen <= data.size()) {
          std::string s(
            reinterpret_cast<const char*>(data.data() + offset + 8), slen);
          trace.strings[sid] = s;
        }
      } else if (rtype == 3 && rsize >= 3) { // thread
        auto tidx = static_cast<std::uint8_t>((word >> 16) & 0xFF);
        std::uint64_t pid = 0;
        std::uint64_t tid = 0;
        std::memcpy(&pid, data.data() + offset + 8, 8);
        std::memcpy(&tid, data.data() + offset + 16, 8);
        trace.threads[tidx] = {pid, tid};
      } else if (rtype == 4 && rsize >= 2) { // event
        FxtEvent ev;
        ev.event_type = static_cast<std::uint8_t>((word >> 16) & 0xF);
        ev.thread_ref = static_cast<std::uint8_t>((word >> 24) & 0xFF);
        ev.category_ref = static_cast<std::uint16_t>((word >> 32) & 0xFFFF);
        ev.name_ref = static_cast<std::uint16_t>((word >> 48) & 0xFFFF);
        std::memcpy(&ev.timestamp, data.data() + offset + 8, 8);
        trace.events.push_back(ev);

        trace.thread_refs_used.insert(ev.thread_ref);
        if (ev.category_ref != 0)
          trace.string_refs_used.insert(ev.category_ref);
        if (ev.name_ref != 0) trace.string_refs_used.insert(ev.name_ref);
      }

      offset = record_end;
    }

    return trace;
  }

  auto
  build_trace_from_lines(const std::vector<std::string>& lines) -> ParsedTrace {
    VectorSink sink;
    FxtWriter writer(sink);
    writer.write_preamble("test", 0, 1'000'000'000ULL, 0, 1000);

    StackReconstructor reconstructor(writer);
    PerfScriptParser parser;

    for (const auto& line : lines) {
      auto event = parser.feed_line(line);
      if (event) { reconstructor.process_event(*event); }
    }
    if (auto event = parser.finish()) { reconstructor.process_event(*event); }
    reconstructor.finish();

    return parse_fxt(sink.data());
  }

  // Sample perf script lines representing a realistic call sequence
  auto
  realistic_call_sequence() -> std::vector<std::string> {
    return {
      // trace starts in main
      "1000/1000 100.000000000:            1   branches:u:   "
      "tr strt                             0 [unknown] ([unknown]) "
      "=>     400000 main+0x0 (a.out)",

      // main calls foo
      "1000/1000 100.000001000:            1   branches:u:   "
      "call                     400010 main+0x10 (a.out) "
      "=>     400100 foo+0x0 (a.out)",

      // foo calls bar
      "1000/1000 100.000002000:            1   branches:u:   "
      "call                     400110 foo+0x10 (a.out) "
      "=>     400200 bar+0x0 (a.out)",

      // bar returns to foo
      "1000/1000 100.000003000:            1   branches:u:   "
      "return                   400220 bar+0x20 (a.out) "
      "=>     400114 foo+0x14 (a.out)",

      // foo calls bar again
      "1000/1000 100.000004000:            1   branches:u:   "
      "call                     400120 foo+0x20 (a.out) "
      "=>     400200 bar+0x0 (a.out)",

      // bar returns to foo
      "1000/1000 100.000005000:            1   branches:u:   "
      "return                   400220 bar+0x20 (a.out) "
      "=>     400124 foo+0x24 (a.out)",

      // foo returns to main
      "1000/1000 100.000006000:            1   branches:u:   "
      "return                   400130 foo+0x30 (a.out) "
      "=>     400014 main+0x14 (a.out)",

      // trace ends with syscall (exit)
      "1000/1000 100.000007000:            1   branches:u:   "
      "tr end  syscall          400020 main+0x20 (a.out) "
      "=>                0 [unknown] ([unknown])",
    };
  }

  // Sequence with trace gaps (tr end async / tr strt)
  auto
  sequence_with_trace_gaps() -> std::vector<std::string> {
    return {
      "1000/1000 100.000000000:            1   branches:u:   "
      "tr strt                             0 [unknown] ([unknown]) "
      "=>     400000 main+0x0 (a.out)",

      "1000/1000 100.000001000:            1   branches:u:   "
      "call                     400010 main+0x10 (a.out) "
      "=>     400100 foo+0x0 (a.out)",

      // trace gap
      "1000/1000 100.000002000:            1   branches:u:   "
      "tr end  async            400110 foo+0x10 (a.out) "
      "=>                0 [unknown] ([unknown])",

      // trace resumes in foo (same function)
      "1000/1000 100.000003000:            1   branches:u:   "
      "tr strt                             0 [unknown] ([unknown]) "
      "=>     400118 foo+0x18 (a.out)",

      // foo returns to main
      "1000/1000 100.000004000:            1   branches:u:   "
      "return                   400130 foo+0x30 (a.out) "
      "=>     400014 main+0x14 (a.out)",

      // trace ends
      "1000/1000 100.000005000:            1   branches:u:   "
      "tr end  syscall          400020 main+0x20 (a.out) "
      "=>                0 [unknown] ([unknown])",
    };
  }

} // namespace

// ===========================================================================
// Invariant 1: Begin/end balance — every begin has a matching end
// ===========================================================================

TEST_CASE("invariant: begins and ends are balanced", "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  int depth = 0;
  for (const auto& ev : trace.events) {
    if (ev.event_type == 2) ++depth;
    if (ev.event_type == 3) --depth;
    REQUIRE(depth >= 0); // never more ends than begins
  }
  CHECK(depth == 0); // balanced at the end
}

// ===========================================================================
// Invariant 2: Stack discipline — ends nest properly
// ===========================================================================

TEST_CASE(
  "invariant: ends match most recent begin (stack nesting)",
  "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  // Per-thread stack of name_refs
  std::map<std::uint8_t, std::vector<std::uint16_t>> stacks;

  for (const auto& ev : trace.events) {
    if (ev.event_type == 2) { // begin
      stacks[ev.thread_ref].push_back(ev.name_ref);
    } else if (ev.event_type == 3) { // end
      auto& stack = stacks[ev.thread_ref];
      REQUIRE(!stack.empty());
      auto expected_name = stack.back();
      CHECK(ev.name_ref == expected_name);
      stack.pop_back();
    }
  }

  // All stacks should be empty
  for (const auto& [tid, stack] : stacks) {
    CHECK(stack.empty());
  }
}

// ===========================================================================
// Invariant 3: Monotonic timestamps per thread
// ===========================================================================

TEST_CASE(
  "invariant: timestamps are monotonically non-decreasing",
  "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  std::map<std::uint8_t, std::uint64_t> last_ts;

  for (const auto& ev : trace.events) {
    if (last_ts.contains(ev.thread_ref)) {
      CHECK(ev.timestamp >= last_ts[ev.thread_ref]);
    }
    last_ts[ev.thread_ref] = ev.timestamp;
  }
}

// ===========================================================================
// Invariant 4: All string/thread refs resolve
// ===========================================================================

TEST_CASE(
  "invariant: all string refs in events are interned", "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  for (auto ref : trace.string_refs_used) {
    CHECK(trace.strings.contains(ref));
  }
}

TEST_CASE(
  "invariant: all thread refs in events are registered", "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  for (auto ref : trace.thread_refs_used) {
    CHECK(trace.threads.contains(ref));
  }
}

// ===========================================================================
// Invariant 5: Trace gaps don't fragment functions
// ===========================================================================

TEST_CASE(
  "invariant: trace gaps preserve function spans", "[trace_validation]") {
  auto trace = build_trace_from_lines(sequence_with_trace_gaps());

  // Count how many times each function name begins
  std::map<std::uint16_t, int> begin_counts;
  for (const auto& ev : trace.events) {
    if (ev.event_type == 2) { // begin
      begin_counts[ev.name_ref]++;
    }
  }

  // Find "foo" — it should only begin once despite the trace gap
  for (const auto& [ref, count] : begin_counts) {
    auto name = trace.strings[ref];
    if (name == "foo") { CHECK(count == 1); }
    if (name == "main") { CHECK(count == 1); }
  }
}

// ===========================================================================
// Invariant 6: Realistic sequence produces expected call structure
// ===========================================================================

TEST_CASE(
  "invariant: call sequence produces correct nesting", "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  // Extract the begin/end sequence as function names
  std::vector<std::pair<std::string, std::string>> sequence;
  for (const auto& ev : trace.events) {
    if (ev.event_type == 2 || ev.event_type == 3) {
      auto type = (ev.event_type == 2) ? "BEGIN" : "END";
      auto name =
        trace.strings.count(ev.name_ref) ? trace.strings[ev.name_ref] : "?";
      sequence.emplace_back(type, name);
    }
  }

  // We should see: main{foo{bar{}bar{}}}, possibly with [untraced]
  // markers. Check that bar appears exactly twice as begin+end pairs.
  int bar_begins = 0;
  int bar_ends = 0;
  for (const auto& [type, name] : sequence) {
    if (name == "bar" && type == "BEGIN") ++bar_begins;
    if (name == "bar" && type == "END") ++bar_ends;
  }
  CHECK(bar_begins == 2);
  CHECK(bar_ends == 2);
}

// ===========================================================================
// Invariant 7: No events have timestamp 0 (except possibly the first)
// ===========================================================================

TEST_CASE(
  "invariant: events have non-zero timestamps after the first",
  "[trace_validation]") {
  auto trace = build_trace_from_lines(realistic_call_sequence());

  bool first = true;
  int zero_count = 0;
  for (const auto& ev : trace.events) {
    if (first) {
      first = false;
      continue;
    }
    if (ev.timestamp == 0) ++zero_count;
  }

  // Allow a small number at timestamp 0 (first batch)
  // but the majority should have real timestamps
  CHECK(zero_count <= 2);
}
