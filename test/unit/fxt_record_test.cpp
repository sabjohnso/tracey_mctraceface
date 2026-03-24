#include <wire_types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <cstring>

using namespace fxt;

// ===========================================================================
// Helper: read a little-endian uint64_t from a buffer at word offset
// ===========================================================================

static std::uint64_t
read_word(std::span<const std::byte> buf, std::size_t word_index) {
  std::uint64_t v = 0;
  std::memcpy(&v, buf.data() + word_index * 8, 8);
  return v;
}

// ===========================================================================
// MagicNumberRecord
// ===========================================================================

TEST_CASE("MagicNumberRecord wire size and round-trip", "[fxt_record]") {
  constexpr std::uint64_t fxt_magic = 0x0016547846040010ULL;

  CHECK(MagicNumberRecord_owned::wire_size == 8);

  MagicNumberRecord_owned record;
  record.set_magic(fxt_magic);

  MagicNumberRecord_view view(record.buffer());
  CHECK(view.magic() == fxt_magic);
  CHECK(read_word(record.buffer(), 0) == fxt_magic);
}

// ===========================================================================
// ProviderSectionMetadata — 1-word record, rtype=0, mtype=2
// ===========================================================================

TEST_CASE("ProviderSectionMetadata wire layout", "[fxt_record]") {
  CHECK(ProviderSectionMetadata_owned::wire_size == 8);

  ProviderSectionMetadata_owned record;
  record.set_provider_id(0x12345678);

  ProviderSectionMetadata_view view(record.buffer());
  CHECK(view.provider_id() == 0x12345678);

  // Verify bit-level layout of the header word:
  // bits [0:4)   = rtype (wire-field)
  // bits [4:16)  = rsize (wire-field)
  // bits [16:20) = mtype (wire-field)
  // bits [20:52) = provider_id (32 bits)
  // bits [52:64) = padding (12 bits)
  std::uint64_t word = read_word(record.buffer(), 0);
  CHECK(((word >> 20) & 0xFFFFFFFF) == 0x12345678);
}

// ===========================================================================
// InitRecord — 4-word record, rtype=1, rsize=4
// ===========================================================================

TEST_CASE("InitRecord wire layout", "[fxt_record]") {
  CHECK(InitRecord_owned::wire_size == 32);

  InitRecord_owned record;
  record.set_ticks_per_second(1'000'000'000ULL);
  record.set_base_ticks(42ULL);
  record.set_base_time_ns(1'700'000'000'000'000'000ULL);

  InitRecord_view view(record.buffer());
  CHECK(view.ticks_per_second() == 1'000'000'000ULL);
  CHECK(view.base_ticks() == 42ULL);
  CHECK(view.base_time_ns() == 1'700'000'000'000'000'000ULL);

  CHECK(read_word(record.buffer(), 1) == 1'000'000'000ULL);
  CHECK(read_word(record.buffer(), 2) == 42ULL);
  CHECK(read_word(record.buffer(), 3) == 1'700'000'000'000'000'000ULL);
}

// ===========================================================================
// ThreadRecord — 3-word record, rtype=3, rsize=3
// ===========================================================================

TEST_CASE("ThreadRecord wire layout", "[fxt_record]") {
  CHECK(ThreadRecord_owned::wire_size == 24);

  ThreadRecord_owned record;
  record.set_thread_index(42);
  record.set_pid(1000);
  record.set_tid(1001);

  ThreadRecord_view view(record.buffer());
  CHECK(view.thread_index() == 42);
  CHECK(view.pid() == 1000);
  CHECK(view.tid() == 1001);

  CHECK(read_word(record.buffer(), 1) == 1000);
  CHECK(read_word(record.buffer(), 2) == 1001);
}

// ===========================================================================
// StringRecordHeader — 1-word header for variable-length string records
// ===========================================================================

TEST_CASE("StringRecordHeader wire layout", "[fxt_record]") {
  CHECK(StringRecordHeader_owned::wire_size == 8);

  StringRecordHeader_owned header;
  header.set_rsize(3);
  header.set_string_id(42);
  header.set_str_len(13);

  StringRecordHeader_view view(header.buffer());
  CHECK(view.rsize() == 3);
  CHECK(view.string_id() == 42);
  CHECK(view.str_len() == 13);
}

// ===========================================================================
// EventRecordHeader — 2-word header for event records
// ===========================================================================

TEST_CASE("EventRecordHeader wire layout", "[fxt_record]") {
  CHECK(EventRecordHeader_owned::wire_size == 16);

  EventRecordHeader_owned header;
  header.set_rsize(3);      // 2 words header + 1 word args
  header.set_event_type(2); // duration_begin
  header.set_num_args(1);
  header.set_thread_ref(5);
  header.set_category_ref(100);
  header.set_name_ref(200);
  header.set_timestamp(999'999'999ULL);

  EventRecordHeader_view view(header.buffer());
  CHECK(view.rsize() == 3);
  CHECK(view.event_type() == 2);
  CHECK(view.num_args() == 1);
  CHECK(view.thread_ref() == 5);
  CHECK(view.category_ref() == 100);
  CHECK(view.name_ref() == 200);
  CHECK(view.timestamp() == 999'999'999ULL);

  // Verify bit layout of word 0:
  // [0:4)=4 (rtype), [4:16)=rsize, [16:20)=event_type,
  // [20:24)=num_args, [24:32)=thread_ref,
  // [32:48)=category_ref, [48:64)=name_ref
  std::uint64_t word0 = read_word(header.buffer(), 0);
  CHECK((word0 & 0xF) == 0);              // rtype wire-field (default 0)
  CHECK(((word0 >> 4) & 0xFFF) == 3);     // rsize
  CHECK(((word0 >> 16) & 0xF) == 2);      // event_type
  CHECK(((word0 >> 20) & 0xF) == 1);      // num_args
  CHECK(((word0 >> 24) & 0xFF) == 5);     // thread_ref
  CHECK(((word0 >> 32) & 0xFFFF) == 100); // category_ref
  CHECK(((word0 >> 48) & 0xFFFF) == 200); // name_ref
}

// ===========================================================================
// KernelObjectHeader — 2-word header for kernel object records
// ===========================================================================

TEST_CASE("KernelObjectHeader wire layout", "[fxt_record]") {
  CHECK(KernelObjectHeader_owned::wire_size == 16);

  KernelObjectHeader_owned header;
  header.set_rsize(2);
  header.set_obj_type(1); // process
  header.set_name_ref(300);
  header.set_num_args(0);
  header.set_koid(12345);

  KernelObjectHeader_view view(header.buffer());
  CHECK(view.rsize() == 2);
  CHECK(view.obj_type() == 1);
  CHECK(view.name_ref() == 300);
  CHECK(view.num_args() == 0);
  CHECK(view.koid() == 12345);
}

// ===========================================================================
// ArgInt32 — 1-word argument
// ===========================================================================

TEST_CASE("ArgInt32 wire layout", "[fxt_record]") {
  CHECK(ArgInt32_owned::wire_size == 8);

  ArgInt32_owned arg;
  arg.set_arg_name(42);
  arg.set_value(-1);

  ArgInt32_view view(arg.buffer());
  CHECK(view.arg_name() == 42);
  CHECK(view.value() == -1);
}

// ===========================================================================
// ArgInt64 — 2-word argument
// ===========================================================================

TEST_CASE("ArgInt64 wire layout", "[fxt_record]") {
  CHECK(ArgInt64_owned::wire_size == 16);

  ArgInt64_owned arg;
  arg.set_arg_name(7);
  arg.set_value(-42);

  ArgInt64_view view(arg.buffer());
  CHECK(view.arg_name() == 7);
  CHECK(view.value() == -42);
}

// ===========================================================================
// ArgFloat64 — 2-word argument
// ===========================================================================

TEST_CASE("ArgFloat64 wire layout", "[fxt_record]") {
  CHECK(ArgFloat64_owned::wire_size == 16);

  ArgFloat64_owned arg;
  arg.set_arg_name(3);
  arg.set_value(std::bit_cast<std::uint64_t>(3.14159));

  ArgFloat64_view view(arg.buffer());
  CHECK(view.arg_name() == 3);
  CHECK(std::bit_cast<double>(view.value()) == 3.14159);
}

// ===========================================================================
// ArgString — 1-word argument
// ===========================================================================

TEST_CASE("ArgString wire layout", "[fxt_record]") {
  CHECK(ArgString_owned::wire_size == 8);

  ArgString_owned arg;
  arg.set_arg_name(10);
  arg.set_string_ref(500);

  ArgString_view view(arg.buffer());
  CHECK(view.arg_name() == 10);
  CHECK(view.string_ref() == 500);
}

// ===========================================================================
// ArgPointer — 2-word argument
// ===========================================================================

TEST_CASE("ArgPointer wire layout", "[fxt_record]") {
  CHECK(ArgPointer_owned::wire_size == 16);

  ArgPointer_owned arg;
  arg.set_arg_name(1);
  arg.set_value(0xDEADBEEFCAFEBABEULL);

  ArgPointer_view view(arg.buffer());
  CHECK(view.arg_name() == 1);
  CHECK(view.value() == 0xDEADBEEFCAFEBABEULL);
}

// ===========================================================================
// ArgKoid — 2-word argument
// ===========================================================================

TEST_CASE("ArgKoid wire layout", "[fxt_record]") {
  CHECK(ArgKoid_owned::wire_size == 16);

  ArgKoid_owned arg;
  arg.set_arg_name(1);
  arg.set_value(99999);

  ArgKoid_view view(arg.buffer());
  CHECK(view.arg_name() == 1);
  CHECK(view.value() == 99999);
}

// ===========================================================================
// ProviderInfoHeader — 1-word header for provider info metadata
// ===========================================================================

TEST_CASE("ProviderInfoHeader wire layout", "[fxt_record]") {
  CHECK(ProviderInfoHeader_owned::wire_size == 8);

  ProviderInfoHeader_owned header;
  header.set_rsize(3);
  header.set_name_len(12);
  header.set_provider_id(0xABCD);

  ProviderInfoHeader_view view(header.buffer());
  CHECK(view.rsize() == 3);
  CHECK(view.name_len() == 12);
  CHECK(view.provider_id() == 0xABCD);
}
