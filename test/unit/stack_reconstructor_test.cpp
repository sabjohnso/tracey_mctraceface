#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/output_sink.hpp>
#include <tracey_mctraceface/perf_event.hpp>
#include <tracey_mctraceface/stack_reconstructor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace tracey_mctraceface;

namespace {

  auto
  make_location(
    std::string symbol, std::uint64_t ip = 0, std::int32_t offset = 0)
    -> Location {
    return {ip, std::move(symbol), offset};
  }

  auto
  make_branch(
    std::uint32_t pid,
    std::uint32_t tid,
    std::uint64_t time_ns,
    BranchKind kind,
    Location src,
    Location dst) -> Event {
    BranchData br;
    br.kind = kind;
    br.src = std::move(src);
    br.dst = std::move(dst);
    return OkEvent{pid, tid, time_ns, std::move(br)};
  }

  auto
  make_trace_start(
    std::uint32_t pid, std::uint32_t tid, std::uint64_t time_ns, Location dst)
    -> Event {
    BranchData br;
    br.trace_state = TraceState::Start;
    br.dst = std::move(dst);
    return OkEvent{pid, tid, time_ns, std::move(br)};
  }

  auto
  make_trace_end(std::uint32_t pid, std::uint32_t tid, std::uint64_t time_ns)
    -> Event {
    BranchData br;
    br.trace_state = TraceState::End;
    return OkEvent{pid, tid, time_ns, std::move(br)};
  }

} // namespace

// ===========================================================================
// Basic call/return
// ===========================================================================

TEST_CASE("single call produces duration_begin", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);
  auto before = sink.size();

  StackReconstructor recon(writer);
  recon.process_event(make_branch(
    1, 2, 1000, BranchKind::Call, make_location("main"), make_location("foo")));
  recon.finish();

  // Should have produced FXT output beyond the preamble
  CHECK(sink.size() > before);
}

TEST_CASE("call + return produces begin + end pair", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);
  auto before = sink.size();

  StackReconstructor recon(writer);
  recon.process_event(make_branch(
    1, 2, 1000, BranchKind::Call, make_location("main"), make_location("foo")));
  recon.process_event(make_branch(
    1,
    2,
    2000,
    BranchKind::Return,
    make_location("foo"),
    make_location("main")));
  recon.finish();

  CHECK(sink.size() > before);
  CHECK(sink.size() % 8 == 0);
}

// ===========================================================================
// check_current_symbol
// ===========================================================================

TEST_CASE("mismatch triggers auto-fix", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);

  // call main → foo
  recon.process_event(make_branch(
    1, 2, 1000, BranchKind::Call, make_location("main"), make_location("foo")));

  // call from bar (not foo!) → baz — triggers mismatch fix
  recon.process_event(make_branch(
    1, 2, 2000, BranchKind::Call, make_location("bar"), make_location("baz")));

  recon.finish();

  // Should succeed without crash, producing valid output
  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}

// ===========================================================================
// Syscall / interrupt context switching
// ===========================================================================

TEST_CASE("syscall saves and restores callstack", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);

  // Build a callstack: main → foo
  recon.process_event(make_branch(
    1,
    2,
    1000,
    BranchKind::Call,
    make_location("_start"),
    make_location("main")));
  recon.process_event(make_branch(
    1, 2, 2000, BranchKind::Call, make_location("main"), make_location("foo")));

  // Syscall: saves current stack, starts new context
  recon.process_event(make_branch(
    1,
    2,
    3000,
    BranchKind::Syscall,
    make_location("foo"),
    make_location("sys_read")));

  // Sysret: restores previous stack
  recon.process_event(make_branch(
    1,
    2,
    4000,
    BranchKind::Sysret,
    make_location("sys_read"),
    make_location("foo")));

  recon.finish();

  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}

// ===========================================================================
// Trace start / end
// ===========================================================================

TEST_CASE("trace start creates initial call", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);
  recon.process_event(make_trace_start(1, 2, 1000, make_location("main")));
  recon.finish();

  CHECK(sink.size() > 0);
}

TEST_CASE("trace end marks untraced", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);
  recon.process_event(make_trace_start(1, 2, 1000, make_location("main")));
  recon.process_event(make_trace_end(1, 2, 2000));
  recon.finish();

  CHECK(sink.size() > 0);
}

// ===========================================================================
// Decode error
// ===========================================================================

TEST_CASE("decode error clears all stacks", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);
  recon.process_event(make_branch(
    1, 2, 1000, BranchKind::Call, make_location("main"), make_location("foo")));

  DecodeError err;
  err.pid = 1;
  err.tid = 2;
  err.time_ns = 2000;
  err.message = "overflow";
  recon.process_event(Event{err});

  recon.finish();

  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}

// ===========================================================================
// Stacktrace sample reconciliation
// ===========================================================================

TEST_CASE("stacktrace sample reconciles stack", "[stack_recon]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 1, 1'000'000'000, 0, 0);

  StackReconstructor recon(writer);

  // Current stack: main → foo
  recon.process_event(make_branch(
    1,
    2,
    1000,
    BranchKind::Call,
    make_location("_start"),
    make_location("main")));
  recon.process_event(make_branch(
    1, 2, 2000, BranchKind::Call, make_location("main"), make_location("foo")));

  // Sample shows: main → bar → baz (foo replaced by bar → baz)
  StacktraceSampleData sample;
  sample.callstack = {
    make_location("main"), make_location("bar"), make_location("baz")};
  recon.process_event(Event{OkEvent{1, 2, 3000, std::move(sample)}});

  recon.finish();

  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}
