#include <tracey_mctraceface/perf_event.hpp>
#include <tracey_mctraceface/perf_script_parser.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace tracey_mctraceface;

namespace {

  auto
  parse_single(std::string_view line) -> std::optional<Event> {
    PerfScriptParser parser;
    parser.feed_line(line);
    return parser.finish();
  }

  auto
  as_ok(const Event& e) -> const OkEvent& {
    return std::get<OkEvent>(e);
  }

  auto
  as_branch(const Event& e) -> const BranchData& {
    return std::get<BranchData>(as_ok(e).data);
  }

  auto
  as_power(const Event& e) -> const PowerData& {
    return std::get<PowerData>(as_ok(e).data);
  }

  auto
  as_error(const Event& e) -> const DecodeError& {
    return std::get<DecodeError>(e);
  }

} // namespace

// ===========================================================================
// Time parsing
// ===========================================================================

TEST_CASE("parse_time: 9-digit nanoseconds", "[perf_parser]") {
  PerfScriptParser parser;
  auto line = "25375/25375 4509191.343298468:          "
              "  1   branches:uH:   call  "
              "7f6fce0b71f4 sym+0x24 (foo.so) =>  "
              "7ffd193838e0 dst+0x0 (foo.so)";
  parser.feed_line(line);
  auto event = parser.finish();
  REQUIRE(event.has_value());
  CHECK(as_ok(*event).time_ns == 4509191343298468ULL);
}

// ===========================================================================
// Branch event parsing
// ===========================================================================

TEST_CASE("parse call branch event", "[perf_parser]") {
  auto event = parse_single(
    "25375/25375 4509191.343298468:            1   branches:uH:   "
    "call                     7f6fce0b71f4 __clock_gettime+0x24 "
    "(foo.so) =>     7ffd193838e0 __vdso_clock_gettime+0x0 (foo.so)");

  REQUIRE(event.has_value());
  auto& ok = as_ok(*event);
  CHECK(ok.pid == 25375);
  CHECK(ok.tid == 25375);

  auto& br = as_branch(*event);
  CHECK(br.kind == BranchKind::Call);
  CHECK(br.trace_state == TraceState::None);
  CHECK(br.in_transaction == false);
  CHECK(br.src.symbol == "__clock_gettime");
  CHECK(br.src.symbol_offset == 0x24);
  CHECK(br.dst.symbol == "__vdso_clock_gettime");
  CHECK(br.dst.symbol_offset == 0);
}

TEST_CASE("parse return branch event", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "return                   7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).kind == BranchKind::Return);
}

TEST_CASE("parse tr strt branch event", "[perf_parser]") {
  auto event = parse_single(
    "25375/25375 4509191.343298468:            1   branches:uH:   "
    "tr strt                             0 [unknown] (foo.so) =>  "
    "   7f6fce0b71d0 __clock_gettime+0x0 (foo.so)");

  REQUIRE(event.has_value());
  auto& br = as_branch(*event);
  CHECK(br.trace_state == TraceState::Start);
  CHECK(!br.kind.has_value());
}

TEST_CASE("parse tr strt jmp branch event", "[perf_parser]") {
  auto event = parse_single(
    "25375/25375 4509191.343298468:            1   branches:uH:   "
    "tr strt jmp                         0 [unknown] (foo.so) =>  "
    "   7f6fce0b71d0 __clock_gettime+0x0 (foo.so)");

  REQUIRE(event.has_value());
  auto& br = as_branch(*event);
  CHECK(br.trace_state == TraceState::Start);
  CHECK(br.kind == BranchKind::Jump);
}

TEST_CASE("parse tr end branch event", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "tr end                   7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).trace_state == TraceState::End);
}

TEST_CASE("parse tr end async branch event", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "tr end  async            7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  auto& br = as_branch(*event);
  CHECK(br.trace_state == TraceState::End);
  CHECK(br.kind == BranchKind::Async);
}

TEST_CASE("parse transaction flag (x)", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "call (x)                 7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).in_transaction == true);
}

TEST_CASE("parse syscall branch event", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "syscall                  7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).kind == BranchKind::Syscall);
}

TEST_CASE("parse jcc branch event", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "jcc                      7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).kind == BranchKind::Jump);
}

// ===========================================================================
// Symbol parsing edge cases
// ===========================================================================

TEST_CASE("parse unknown symbol", "[perf_parser]") {
  auto event = parse_single(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "call                     0 [unknown] (/lib/x.so) => "
    "    7f0002000 bar+0x20 (b.so)");

  REQUIRE(event.has_value());
  CHECK(as_branch(*event).src.symbol == "[unknown]");
  CHECK(as_branch(*event).src.symbol_offset == 0);
}

// ===========================================================================
// CBR / Power events
// ===========================================================================

TEST_CASE("parse cbr power event", "[perf_parser]") {
  auto event = parse_single(
    "2937048/2937048 448556.689322817:                        "
    "           1    cbr:  "
    "                      cbr: 46 freq: 4606 MHz (159%)      "
    "             0                0 [unknown] ([unknown])");

  REQUIRE(event.has_value());
  CHECK(as_power(*event).freq_mhz == 4606);
}

// ===========================================================================
// Decode errors
// ===========================================================================

TEST_CASE("parse decode error with timestamp", "[perf_parser]") {
  auto event = parse_single(
    " instruction trace error type 1 time 47170.086912826 "
    "cpu -1 pid 293415 tid 293415 ip 0x7ffff7327730 code 7: "
    "Overflow packet");

  REQUIRE(event.has_value());
  auto& err = as_error(*event);
  CHECK(err.pid == 293415);
  CHECK(err.tid == 293415);
  REQUIRE(err.time_ns.has_value());
  CHECK(err.ip.has_value());
  CHECK(err.message == "Overflow packet");
}

TEST_CASE("parse decode error without timestamp", "[perf_parser]") {
  auto event = parse_single(
    " instruction trace error type 1 "
    "cpu -1 pid 293415 tid 293415 ip 0x7ffff7327730 code 7: "
    "Overflow packet");

  REQUIRE(event.has_value());
  auto& err = as_error(*event);
  CHECK(!err.time_ns.has_value());
}

// ===========================================================================
// PSB events (ignored)
// ===========================================================================

TEST_CASE("psb events return nullopt", "[perf_parser]") {
  auto event = parse_single("1000/1001 100.000000000:            1   psb:   ");

  CHECK(!event.has_value());
}

// ===========================================================================
// Multi-line events (stacktraces)
// ===========================================================================

TEST_CASE("parse cycles event with callstack", "[perf_parser]") {
  PerfScriptParser parser;

  // Header line
  parser.feed_line("2060126/2060126 178090.391624068:     555555 cycles:u:");

  // Callstack (innermost first in perf output)
  parser.feed_line("\tffffffff97201100 [unknown] ([unknown])");
  parser.feed_line("\t7f9bd48c1d80 _dl_setup_hash+0x0 (/usr/lib64/ld-2.28.so)");
  parser.feed_line("\t4008de main+0x87 (/home/demo)");

  auto event = parser.finish();
  REQUIRE(event.has_value());

  auto& ok = as_ok(*event);
  auto& stack = std::get<StacktraceSampleData>(ok.data);

  // Reversed: outermost first
  REQUIRE(stack.callstack.size() == 3);
  CHECK(stack.callstack[0].symbol == "main");
  CHECK(stack.callstack[2].symbol == "[unknown]");
}

// ===========================================================================
// Multi-line feed_line sequencing
// ===========================================================================

TEST_CASE("feed_line yields previous event on new header", "[perf_parser]") {
  PerfScriptParser parser;

  // First event
  auto r1 = parser.feed_line(
    "1000/1001 100.000000000:            1   branches:uH:   "
    "call                     7f0001000 foo+0x10 (a.so) => "
    "    7f0002000 bar+0x20 (b.so)");
  CHECK(!r1.has_value()); // not yet — need next line to know it's complete

  // Second event header triggers first event
  auto r2 = parser.feed_line(
    "1000/1001 100.000000001:            1   branches:uH:   "
    "return                   7f0002000 bar+0x20 (b.so) => "
    "    7f0001000 foo+0x14 (a.so)");
  REQUIRE(r2.has_value());
  CHECK(as_branch(*r2).kind == BranchKind::Call);

  // finish yields the second event
  auto r3 = parser.finish();
  REQUIRE(r3.has_value());
  CHECK(as_branch(*r3).kind == BranchKind::Return);
}
