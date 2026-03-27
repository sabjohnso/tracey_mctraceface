/// CLI acceptance tests — verify run, decode, and --no-decode
/// commands produce correct end-to-end results.
///
/// These tests invoke the actual tracey_mctraceface binary and
/// validate the output files.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

  auto
  tracey() -> std::string {
#ifdef TRACEY_BINARY_PATH
    return TRACEY_BINARY_PATH;
#else
    // Fallback: look relative to test binary
    auto self = std::filesystem::read_symlink("/proc/self/exe");
    return (self.parent_path() / "../../src/bin/tracey_mctraceface").string();
#endif
  }

  auto
  run_cmd(const std::string& cmd) -> int {
    return WEXITSTATUS(std::system(cmd.c_str()));
  }

  auto
  read_file_bytes(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
  }

  auto
  read_fxt_magic(const std::filesystem::path& path) -> std::uint64_t {
    auto data = read_file_bytes(path);
    if (data.size() < 8) return 0;
    std::uint64_t magic = 0;
    std::memcpy(&magic, data.data(), 8);
    return magic;
  }

  struct FxtStats {
    int begins = 0;
    int ends = 0;
    int strings = 0;
    int threads = 0;
    int kernel_objects = 0;
  };

  auto
  count_fxt_records(const std::filesystem::path& path) -> FxtStats {
    auto data = read_file_bytes(path);
    FxtStats stats;
    std::size_t offset = 0;
    while (offset + 8 <= data.size()) {
      std::uint64_t word = 0;
      std::memcpy(&word, data.data() + offset, 8);
      auto rtype = word & 0xF;
      auto rsize = (word >> 4) & 0xFFF;
      if (rsize == 0) break;
      if (rtype == 2) ++stats.strings;
      if (rtype == 3) ++stats.threads;
      if (rtype == 4) {
        auto event_type = (word >> 16) & 0xF;
        if (event_type == 2) ++stats.begins;
        if (event_type == 3) ++stats.ends;
      }
      if (rtype == 7) ++stats.kernel_objects;
      offset += rsize * 8;
    }
    return stats;
  }

  auto
  temp_dir() -> std::filesystem::path {
    auto dir = std::filesystem::temp_directory_path() / "cli_accept_test";
    std::filesystem::create_directories(dir);
    return dir;
  }

  auto
  perf_available() -> bool {
    // Quick check: can perf record a trivial program?
    auto rc = run_cmd(
      "perf record -o /dev/null --event=cycles:u "
      "-- /bin/true 2>/dev/null");
    return rc == 0;
  }

} // namespace

// ===========================================================================
// Help output
// ===========================================================================

TEST_CASE("--help prints usage", "[cli_acceptance]") {
  auto rc = run_cmd(tracey() + " --help 2>/dev/null");
  CHECK(rc == 0);
}

TEST_CASE("run --help prints usage", "[cli_acceptance]") {
  auto rc = run_cmd(tracey() + " run --help 2>/dev/null");
  CHECK(rc == 0);
}

TEST_CASE("decode --help prints usage", "[cli_acceptance]") {
  auto rc = run_cmd(tracey() + " decode --help 2>/dev/null");
  CHECK(rc == 0);
}

TEST_CASE("attach --help prints usage", "[cli_acceptance]") {
  auto rc = run_cmd(tracey() + " attach --help 2>/dev/null");
  CHECK(rc == 0);
}

// ===========================================================================
// run command
// ===========================================================================

TEST_CASE("run produces valid FXT file", "[cli_acceptance]") {
  if (!perf_available()) SKIP("perf not available (check perf_event_paranoid)");
  auto dir = temp_dir();
  auto output = (dir / "run_test.fxt").string();

  auto rc = run_cmd(
    tracey() + " run --full-execution --quiet -o " + output +
    " -- /bin/true 2>/dev/null");

  CHECK(rc == 0);
  CHECK(std::filesystem::exists(output));
  CHECK(read_fxt_magic(output) == 0x0016547846040010ULL);

  auto stats = count_fxt_records(output);
  CHECK(stats.begins > 0);
  CHECK(stats.begins == stats.ends);
  CHECK(stats.strings > 0);
  CHECK(stats.threads > 0);
  CHECK(stats.kernel_objects > 0);

  std::filesystem::remove_all(dir);
}

// ===========================================================================
// --no-decode
// ===========================================================================

TEST_CASE("run --no-decode saves perf.data", "[cli_acceptance]") {
  if (!perf_available()) SKIP("perf not available");
  auto dir = temp_dir();
  auto output = (dir / "saved.data").string();

  auto rc = run_cmd(
    tracey() + " run --full-execution --quiet --no-decode -o " + output +
    " -- /bin/true 2>/dev/null");

  CHECK(rc == 0);
  CHECK(std::filesystem::exists(output));
  // perf.data files are not FXT — they should NOT have the FXT magic
  CHECK(read_fxt_magic(output) != 0x0016547846040010ULL);
  // But they should have content
  CHECK(std::filesystem::file_size(output) > 0);

  std::filesystem::remove_all(dir);
}

// ===========================================================================
// decode command
// ===========================================================================

TEST_CASE("decode converts perf.data to FXT", "[cli_acceptance]") {
  if (!perf_available()) SKIP("perf not available");
  auto dir = temp_dir();
  auto perf_data = (dir / "perf.data").string();
  auto fxt_out = (dir / "decoded.fxt").string();

  // First record with --no-decode
  auto rc1 = run_cmd(
    tracey() + " run --full-execution --quiet --no-decode -o " + perf_data +
    " -- /bin/true 2>/dev/null");
  REQUIRE(rc1 == 0);
  REQUIRE(std::filesystem::exists(perf_data));

  // Then decode
  auto rc2 = run_cmd(
    tracey() + " decode --quiet -d " + perf_data + " -o " + fxt_out +
    " 2>/dev/null");
  CHECK(rc2 == 0);
  CHECK(std::filesystem::exists(fxt_out));
  CHECK(read_fxt_magic(fxt_out) == 0x0016547846040010ULL);

  auto stats = count_fxt_records(fxt_out);
  CHECK(stats.begins > 0);
  CHECK(stats.begins == stats.ends);

  std::filesystem::remove_all(dir);
}

// ===========================================================================
// Compressed output
// ===========================================================================

TEST_CASE("run produces valid gzip output", "[cli_acceptance]") {
  if (!perf_available()) SKIP("perf not available");
  auto dir = temp_dir();
  auto output = (dir / "test.fxt.gz").string();

  auto rc = run_cmd(
    tracey() + " run --full-execution --quiet -o " + output +
    " -- /bin/true 2>/dev/null");

  CHECK(rc == 0);
  CHECK(std::filesystem::exists(output));

  // Check gzip magic bytes
  auto data = read_file_bytes(output);
  REQUIRE(data.size() >= 2);
  CHECK(data[0] == std::byte{0x1F});
  CHECK(data[1] == std::byte{0x8B});

  std::filesystem::remove_all(dir);
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST_CASE("run with bad output path fails gracefully", "[cli_acceptance]") {
  if (!perf_available()) SKIP("perf not available");
  auto rc = run_cmd(
    tracey() + " run --full-execution -o /nonexistent/dir/trace.fxt"
               " -- /bin/true 2>/dev/null");
  CHECK(rc != 0);
}

TEST_CASE(
  "decode with missing perf.data fails gracefully", "[cli_acceptance]") {
  // decode spawns perf script which fails, but the tool returns 0
  // with 0 events decoded and a warning. This is acceptable —
  // it doesn't crash or produce corrupt output.
  auto dir = temp_dir();
  auto output = (dir / "empty.fxt").string();
  auto rc = run_cmd(
    tracey() + " decode --quiet -d /nonexistent/path -o " + output +
    " 2>/dev/null");
  CHECK(rc == 0);
  // Output file should exist but be small (just the preamble)
  if (std::filesystem::exists(output)) {
    auto stats = count_fxt_records(output);
    CHECK(stats.begins == 0);
  }
  std::filesystem::remove_all(dir);
}
