#include <tracey_mctraceface/perf_event.hpp>
#include <tracey_mctraceface/trace_filter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tracey_mctraceface;

namespace {

  auto
  make_branch_event(
    std::string src_sym, std::string dst_sym, std::uint64_t time = 1000)
    -> Event {
    BranchData br;
    br.kind = BranchKind::Call;
    br.src = {0, std::move(src_sym), 0};
    br.dst = {0, std::move(dst_sym), 0};
    return OkEvent{1, 1, time, std::move(br)};
  }

  auto
  make_power_event(std::uint64_t time = 1000) -> Event {
    return OkEvent{1, 1, time, PowerData{4600}};
  }

} // namespace

// ===========================================================================
// No config — pass everything
// ===========================================================================

TEST_CASE("no config passes all events", "[trace_filter]") {
  TraceFilter filter(
    {.start_symbol = "", .end_symbol = "", .multi_slice = false});
  CHECK(filter.should_pass(make_branch_event("a", "b")));
  CHECK(filter.should_pass(make_branch_event("c", "d")));
  CHECK(filter.should_pass(make_power_event()));
}

// ===========================================================================
// End symbol only — stop at end
// ===========================================================================

TEST_CASE("end_symbol stops recording", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "", .end_symbol = "target"});

  CHECK(filter.should_pass(make_branch_event("a", "b")));
  CHECK(filter.should_pass(make_branch_event("b", "c")));

  // Event with end symbol — passes (included) but transitions to Done
  CHECK(filter.should_pass(make_branch_event("c", "target")));

  // After end — blocked
  CHECK_FALSE(filter.should_pass(make_branch_event("d", "e")));
  CHECK(filter.slice_count() == 1);
}

// ===========================================================================
// Start symbol only — wait for start
// ===========================================================================

TEST_CASE("start_symbol waits before recording", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "main", .end_symbol = ""});

  CHECK_FALSE(filter.should_pass(make_branch_event("_start", "_dl_start")));
  CHECK_FALSE(filter.should_pass(make_branch_event("_dl_start", "foo")));

  // Start symbol appears — starts recording
  CHECK(filter.should_pass(make_branch_event("_dl_start", "main")));

  // Subsequent events pass
  CHECK(filter.should_pass(make_branch_event("main", "foo")));
  CHECK(filter.should_pass(make_branch_event("foo", "bar")));
}

// ===========================================================================
// Both start and end — extract slice
// ===========================================================================

TEST_CASE("start + end extracts a slice", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "main", .end_symbol = "cleanup"});

  // Before start
  CHECK_FALSE(filter.should_pass(make_branch_event("_start", "init")));

  // Start
  CHECK(filter.should_pass(make_branch_event("init", "main")));
  CHECK(filter.should_pass(make_branch_event("main", "foo")));

  // End (included)
  CHECK(filter.should_pass(make_branch_event("foo", "cleanup")));

  // After end
  CHECK_FALSE(filter.should_pass(make_branch_event("cleanup", "exit")));
  CHECK(filter.slice_count() == 1);
}

// ===========================================================================
// Multi-slice
// ===========================================================================

TEST_CASE("multi_slice collects multiple regions", "[trace_filter]") {
  TraceFilter filter(
    {.start_symbol = "begin", .end_symbol = "end", .multi_slice = true});

  // First slice
  CHECK_FALSE(filter.should_pass(make_branch_event("a", "b")));
  CHECK(filter.should_pass(make_branch_event("a", "begin")));
  CHECK(filter.should_pass(make_branch_event("begin", "work")));
  CHECK(filter.should_pass(make_branch_event("work", "end")));
  CHECK(filter.slice_count() == 1);

  // Between slices — waiting
  CHECK_FALSE(filter.should_pass(make_branch_event("end", "idle")));

  // Second slice
  CHECK(filter.should_pass(make_branch_event("idle", "begin")));
  CHECK(filter.should_pass(make_branch_event("begin", "more_work")));
  CHECK(filter.should_pass(make_branch_event("more_work", "end")));
  CHECK(filter.slice_count() == 2);
}

// ===========================================================================
// Start symbol missing
// ===========================================================================

TEST_CASE("start_symbol_missing when never seen", "[trace_filter]") {
  TraceFilter filter(
    {.start_symbol = "nonexistent", .end_symbol = "end", .multi_slice = false});

  filter.should_pass(make_branch_event("a", "b"));
  filter.should_pass(make_branch_event("c", "d"));

  CHECK(filter.start_symbol_missing());
}

TEST_CASE("start_symbol_missing false when seen", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "main", .end_symbol = ""});

  filter.should_pass(make_branch_event("a", "main"));

  CHECK_FALSE(filter.start_symbol_missing());
}

// ===========================================================================
// Non-branch events follow recording state
// ===========================================================================

TEST_CASE("non-branch events pass when recording", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "main", .end_symbol = ""});

  // Before start — non-branch blocked
  CHECK_FALSE(filter.should_pass(make_power_event()));

  // After start — non-branch passes
  filter.should_pass(make_branch_event("a", "main"));
  CHECK(filter.should_pass(make_power_event()));
}

// ===========================================================================
// Symbol matching is prefix-based
// ===========================================================================

TEST_CASE("symbol matching is prefix-based", "[trace_filter]") {
  TraceFilter filter({.start_symbol = "main", .end_symbol = ""});

  // "main+0x10" starts with "main"
  CHECK(filter.should_pass(make_branch_event("foo", "main+0x10")));
}
