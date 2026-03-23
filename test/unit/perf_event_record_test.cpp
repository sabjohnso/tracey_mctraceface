#include <wire_types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace perf_event;

// ===========================================================================
// BranchEvent — 56-byte fixed-size record
// ===========================================================================

TEST_CASE("BranchEvent wire size", "[perf_event_record]") {
  CHECK(BranchEvent_owned::wire_size == 52);
}

TEST_CASE("BranchEvent round-trip", "[perf_event_record]") {
  BranchEvent_owned event;
  event.set_pid(1234);
  event.set_tid(5678);
  event.set_time_ns(999'999'999ULL);
  event.set_kind(1);        // Call
  event.set_trace_state(0); // None
  event.set_in_transaction(0);
  event.set_src_ip(0x7f0000001000ULL);
  event.set_src_symbol_id(42);
  event.set_src_symbol_offset(0x24);
  event.set_dst_ip(0x7f0000002000ULL);
  event.set_dst_symbol_id(43);
  event.set_dst_symbol_offset(0);

  BranchEvent_view view(event.buffer());
  CHECK(view.pid() == 1234);
  CHECK(view.tid() == 5678);
  CHECK(view.time_ns() == 999'999'999ULL);
  CHECK(view.kind() == 1);
  CHECK(view.trace_state() == 0);
  CHECK(view.in_transaction() == 0);
  CHECK(view.src_ip() == 0x7f0000001000ULL);
  CHECK(view.src_symbol_id() == 42);
  CHECK(view.src_symbol_offset() == 0x24);
  CHECK(view.dst_ip() == 0x7f0000002000ULL);
  CHECK(view.dst_symbol_id() == 43);
  CHECK(view.dst_symbol_offset() == 0);
}

TEST_CASE("BranchEvent negative symbol offset", "[perf_event_record]") {
  BranchEvent_owned event;
  event.set_src_symbol_offset(-1);

  BranchEvent_view view(event.buffer());
  CHECK(view.src_symbol_offset() == -1);
}

// ===========================================================================
// PowerEvent — 24-byte fixed-size record
// ===========================================================================

TEST_CASE("PowerEvent wire size", "[perf_event_record]") {
  CHECK(PowerEvent_owned::wire_size == 24);
}

TEST_CASE("PowerEvent round-trip", "[perf_event_record]") {
  PowerEvent_owned event;
  event.set_pid(100);
  event.set_tid(200);
  event.set_time_ns(12345678ULL);
  event.set_freq_mhz(4600);

  PowerEvent_view view(event.buffer());
  CHECK(view.pid() == 100);
  CHECK(view.tid() == 200);
  CHECK(view.time_ns() == 12345678ULL);
  CHECK(view.freq_mhz() == 4600);
}
