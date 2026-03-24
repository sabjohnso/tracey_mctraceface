#include <tracey_mctraceface/compressed_sink.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace tracey_mctraceface;

namespace {

  auto
  read_file(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream f(path, std::ios::binary);
    auto begin = std::istreambuf_iterator<char>(f);
    auto end = std::istreambuf_iterator<char>();
    std::vector<char> chars(begin, end);
    std::vector<std::byte> result(chars.size());
    std::transform(chars.begin(), chars.end(), result.begin(), [](char c) {
      return static_cast<std::byte>(c);
    });
    return result;
  }

} // namespace

TEST_CASE("make_sink creates FileSink for .fxt", "[compressed_sink]") {
  auto path = std::filesystem::temp_directory_path() / "test_plain.fxt";
  auto sink = make_sink(path);
  std::array<std::byte, 4> data = {
    std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  sink->write(data);
  sink.reset();

  auto content = read_file(path);
  CHECK(content.size() == 4);
  CHECK(content[0] == std::byte{0xDE});
  std::filesystem::remove(path);
}

TEST_CASE("make_sink creates GzipSink for .fxt.gz", "[compressed_sink]") {
  auto path = std::filesystem::temp_directory_path() / "test_comp.fxt.gz";
  auto sink = make_sink(path);

  // Write some data
  std::array<std::byte, 8> data{};
  for (int i = 0; i < 100; ++i) {
    sink->write(data);
  }
  sink.reset();

  // File should exist and be smaller than 800 bytes (uncompressed)
  auto compressed = read_file(path);
  CHECK(!compressed.empty());
  CHECK(compressed.size() < 800);

  // gzip magic bytes
  CHECK(compressed[0] == std::byte{0x1F});
  CHECK(compressed[1] == std::byte{0x8B});

  std::filesystem::remove(path);
}

TEST_CASE("make_sink creates ZstdSink for .fxt.zst", "[compressed_sink]") {
  auto path = std::filesystem::temp_directory_path() / "test_comp.fxt.zst";
  auto sink = make_sink(path);

  std::array<std::byte, 8> data{};
  for (int i = 0; i < 100; ++i) {
    sink->write(data);
  }
  sink.reset();

  auto compressed = read_file(path);
  CHECK(!compressed.empty());
  CHECK(compressed.size() < 800);

  // zstd magic bytes (0xFD2FB528 LE)
  CHECK(compressed[0] == std::byte{0x28});
  CHECK(compressed[1] == std::byte{0xB5});
  CHECK(compressed[2] == std::byte{0x2F});
  CHECK(compressed[3] == std::byte{0xFD});

  std::filesystem::remove(path);
}
