#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/output_sink.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

using namespace tracey_mctraceface;

namespace {

  auto
  read_word(std::span<const std::byte> buf, std::size_t word_index)
    -> std::uint64_t {
    std::uint64_t v = 0;
    std::memcpy(&v, buf.data() + word_index * 8, 8);
    return v;
  }

} // namespace

// ===========================================================================
// Preamble
// ===========================================================================

TEST_CASE("FxtWriter preamble starts with magic number", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test_provider", 1, 1'000'000'000, 0, 0);

  REQUIRE(sink.size() >= 8);
  CHECK(read_word(sink.data(), 0) == 0x0016547846040010ULL);
}

TEST_CASE("FxtWriter preamble is 8-byte aligned", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("jane_tracing", 1, 1'000'000'000, 0, 0);

  CHECK(sink.size() % 8 == 0);
}

TEST_CASE(
  "FxtWriter preamble contains provider info, section, and init",
  "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_preamble("test", 42, 1'000'000'000, 100, 200);

  // Magic (8) + ProviderInfo header (8) + "test" padded (8) +
  // ProviderSection (8) + InitRecord (32) = 64 bytes
  CHECK(sink.size() == 64);

  // Word 0: magic
  CHECK(read_word(sink.data(), 0) == 0x0016547846040010ULL);

  // Word 1: ProviderInfo header — rtype=0, mtype=1, name_len=4
  std::uint64_t info_word = read_word(sink.data(), 1);
  CHECK((info_word & 0xF) == 0);           // rtype=0
  CHECK(((info_word >> 4) & 0xFFF) == 2);  // rsize=2 (1 header + 1 padded)
  CHECK(((info_word >> 16) & 0xF) == 1);   // mtype=1
  CHECK(((info_word >> 20) & 0xFFF) == 4); // name_len=4
  CHECK(((info_word >> 32)) == 42);        // provider_id

  // Word 2: "test" + padding
  std::array<char, 8> name_buf{};
  std::memcpy(name_buf.data(), sink.data().data() + 16, 8);
  CHECK(std::string_view(name_buf.data(), 4) == "test");

  // Word 3: ProviderSection — rtype=0, mtype=2, provider_id=42
  std::uint64_t section_word = read_word(sink.data(), 3);
  CHECK((section_word & 0xF) == 0);         // rtype=0
  CHECK(((section_word >> 16) & 0xF) == 2); // mtype=2
  CHECK(((section_word >> 32)) == 42);      // provider_id

  // Words 4-7: InitRecord (4 words)
  std::uint64_t init_header = read_word(sink.data(), 4);
  CHECK((init_header & 0xF) == 1);                      // rtype=1
  CHECK(((init_header >> 4) & 0xFFF) == 4);             // rsize=4
  CHECK(read_word(sink.data(), 5) == 1'000'000'000ULL); // ticks_per_second
  CHECK(read_word(sink.data(), 6) == 100);              // base_ticks
  CHECK(read_word(sink.data(), 7) == 200);              // base_time_ns
}

// ===========================================================================
// String interning
// ===========================================================================

TEST_CASE("intern_string returns 0 for empty string", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  CHECK(writer.intern_string("") == 0);
  CHECK(sink.size() == 0); // no record emitted
}

TEST_CASE("intern_string returns unique IDs", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto id1 = writer.intern_string("hello");
  auto id2 = writer.intern_string("world");
  CHECK(id1 != id2);
  CHECK(id1 >= 1);
  CHECK(id2 >= 1);
}

TEST_CASE("intern_string returns same ID for same string", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto id1 = writer.intern_string("hello");
  auto before = sink.size();
  auto id2 = writer.intern_string("hello");
  CHECK(id1 == id2);
  CHECK(sink.size() == before); // no duplicate record
}

TEST_CASE("intern_string emits string record", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto id = writer.intern_string("hello");

  // String record: 1-word header + "hello"(5 bytes) padded to 8 = 16 bytes
  REQUIRE(sink.size() == 16);

  std::uint64_t header = read_word(sink.data(), 0);
  CHECK((header & 0xF) == 2);             // rtype=2 (string)
  CHECK(((header >> 4) & 0xFFF) == 2);    // rsize=2
  CHECK(((header >> 16) & 0x7FFF) == id); // string_id
  CHECK(((header >> 32) & 0x7FFF) == 5);  // str_len=5

  // Verify string content
  std::array<char, 8> content{};
  std::memcpy(content.data(), sink.data().data() + 8, 8);
  CHECK(std::string_view(content.data(), 5) == "hello");
}

// ===========================================================================
// Thread slot management
// ===========================================================================

TEST_CASE(
  "ensure_thread returns slot and emits thread record", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto slot = writer.ensure_thread(100, 200);

  CHECK(slot >= 1);
  CHECK(slot <= 255);

  // Thread record (24 bytes) + kernel object records for process/thread
  // names + string records for the names
  REQUIRE(sink.size() > 24);
  CHECK(sink.size() % 8 == 0);

  // First record should be the thread record
  std::uint64_t header = read_word(sink.data(), 0);
  CHECK((header & 0xF) == 3);              // rtype=3 (thread)
  CHECK(((header >> 4) & 0xFFF) == 3);     // rsize=3
  CHECK(((header >> 16) & 0xFF) == slot);  // thread_index
  CHECK(read_word(sink.data(), 1) == 100); // pid
  CHECK(read_word(sink.data(), 2) == 200); // tid
}

TEST_CASE("ensure_thread returns same slot for same thread", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto slot1 = writer.ensure_thread(100, 200);
  auto before = sink.size();
  auto slot2 = writer.ensure_thread(100, 200);
  CHECK(slot1 == slot2);
  CHECK(sink.size() == before); // no duplicate record
}

TEST_CASE("ensure_thread assigns different slots", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  auto slot1 = writer.ensure_thread(100, 200);
  auto slot2 = writer.ensure_thread(100, 300);
  CHECK(slot1 != slot2);
}

// ===========================================================================
// Duration events
// ===========================================================================

TEST_CASE("write_duration_begin emits event record", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_duration_begin(1, 2, "cat", "func", 12345);

  // String records for "cat" and "func" + thread record + event header
  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0); // all records 8-byte aligned
}

TEST_CASE("write_duration_end emits event record", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_duration_end(1, 2, "cat", "func", 99999);

  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}

// ===========================================================================
// Kernel object records
// ===========================================================================

TEST_CASE("write_process_name emits kernel object record", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_process_name(1000, "my_process");

  // String record for "my_process" + kernel object header (2 words)
  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}

TEST_CASE(
  "write_thread_name emits kernel object with process arg", "[fxt_writer]") {
  VectorSink sink;
  FxtWriter writer(sink);
  writer.write_thread_name(1000, 2000, "worker");

  // String records + kernel object header (2 words) + koid arg (2 words)
  CHECK(sink.size() > 0);
  CHECK(sink.size() % 8 == 0);
}
