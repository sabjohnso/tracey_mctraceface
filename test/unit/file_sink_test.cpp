#include <tracey_mctraceface/file_sink.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>

using namespace tracey_mctraceface;

TEST_CASE("FileSink writes and reads back", "[file_sink]") {
  auto path = std::filesystem::temp_directory_path() / "fxt_test_output.bin";

  {
    FileSink sink(path);
    std::array<std::byte, 4> data = {
      std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    sink.write(data);
  }

  // Read back
  auto* f = std::fopen(path.c_str(), "rb");
  REQUIRE(f != nullptr);
  std::array<std::byte, 4> buf{};
  auto read = std::fread(buf.data(), 1, 4, f);
  std::fclose(f);

  CHECK(read == 4);
  CHECK(buf[0] == std::byte{0xDE});
  CHECK(buf[1] == std::byte{0xAD});
  CHECK(buf[2] == std::byte{0xBE});
  CHECK(buf[3] == std::byte{0xEF});

  std::filesystem::remove(path);
}

TEST_CASE("FileSink throws on invalid path", "[file_sink]") {
  CHECK_THROWS(FileSink("/nonexistent/dir/file.bin"));
}
