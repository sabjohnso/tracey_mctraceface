/// Error path tests — verify graceful handling of failures
/// in I/O, process management, and compression.

#include <tracey_mctraceface/background_process.hpp>
#include <tracey_mctraceface/compressed_sink.hpp>
#include <tracey_mctraceface/file_sink.hpp>
#include <tracey_mctraceface/fxt_writer.hpp>
#include <tracey_mctraceface/output_sink.hpp>
#include <tracey_mctraceface/subprocess.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

using namespace tracey_mctraceface;

// ===========================================================================
// FileSink error paths
// ===========================================================================

TEST_CASE("FileSink throws on nonexistent directory", "[error_path]") {
  CHECK_THROWS_AS(
    FileSink("/nonexistent/path/to/file.fxt"), std::runtime_error);
}

TEST_CASE("FileSink throws on permission denied", "[error_path]") {
  // /proc/1/root is not writable by non-root
  CHECK_THROWS_AS(FileSink("/proc/1/root/test.fxt"), std::runtime_error);
}

TEST_CASE("FileSink write to /dev/full throws", "[error_path]") {
  if (!std::filesystem::exists("/dev/full")) return;

  FileSink sink("/dev/full");
  // /dev/full may buffer the first small writes; write enough to
  // overflow the buffer and trigger ENOSPC.
  std::array<std::byte, 65536> data{};
  CHECK_THROWS_AS(
    [&] {
      for (int i = 0; i < 1000; ++i) {
        sink.write(data);
      }
    }(),
    std::runtime_error);
}

// ===========================================================================
// GzipSink error paths
// ===========================================================================

TEST_CASE("GzipSink throws on nonexistent directory", "[error_path]") {
  CHECK_THROWS_AS(
    GzipSink("/nonexistent/path/to/file.fxt.gz"), std::runtime_error);
}

// ===========================================================================
// ZstdSink error paths
// ===========================================================================

TEST_CASE("ZstdSink throws on nonexistent directory", "[error_path]") {
  CHECK_THROWS_AS(
    ZstdSink("/nonexistent/path/to/file.fxt.zst"), std::runtime_error);
}

// ===========================================================================
// Subprocess error paths
// ===========================================================================

TEST_CASE("Subprocess throws on empty args", "[error_path]") {
  CHECK_THROWS_AS(Subprocess({}), std::runtime_error);
}

TEST_CASE("Subprocess handles nonexistent program", "[error_path]") {
  // execvp fails, child exits 127
  Subprocess proc({"nonexistent_program_xyz"});
  std::string line;
  // Should return false (EOF) immediately since child exited
  while (proc.read_line(line)) {}
  CHECK(proc.wait() == 127);
}

// ===========================================================================
// BackgroundProcess error paths
// ===========================================================================

TEST_CASE("BackgroundProcess throws on empty args", "[error_path]") {
  CHECK_THROWS_AS(BackgroundProcess({}), std::runtime_error);
}

TEST_CASE("BackgroundProcess handles nonexistent program", "[error_path]") {
  BackgroundProcess proc({"nonexistent_program_xyz"});
  CHECK(proc.wait() == 127);
}

// ===========================================================================
// FxtWriter with failing sink
// ===========================================================================

namespace {

  class FailingSink : public OutputSink {
    int writes_before_fail_;
    int write_count_ = 0;

  public:
    explicit FailingSink(int writes_before_fail)
        : writes_before_fail_(writes_before_fail) {}

    void
    write(std::span<const std::byte> /*data*/) override {
      if (++write_count_ > writes_before_fail_) {
        throw std::runtime_error("FailingSink: simulated write failure");
      }
    }
  };

} // namespace

TEST_CASE("FxtWriter propagates sink write failure", "[error_path]") {
  // Fail after the preamble writes
  FailingSink sink(100);
  FxtWriter writer(sink);

  // Preamble should succeed (uses ~10 writes)
  writer.write_preamble("test", 0, 1'000'000'000ULL, 0, 0);

  // Eventually a write will fail
  CHECK_THROWS_AS(
    [&] {
      for (int i = 0; i < 10000; ++i) {
        writer.write_duration_begin(1, 1, "cat", "func" + std::to_string(i), i);
      }
    }(),
    std::runtime_error);
}

// ===========================================================================
// make_sink factory
// ===========================================================================

TEST_CASE("make_sink returns FileSink for unknown extension", "[error_path]") {
  auto path = std::filesystem::temp_directory_path() / "test.unknown";
  auto sink = make_sink(path);
  // Should not throw — creates a plain file
  std::array<std::byte, 4> data{};
  sink->write(data);
  sink.reset();
  std::filesystem::remove(path);
}

TEST_CASE("make_sink throws for bad gz path", "[error_path]") {
  CHECK_THROWS_AS(
    make_sink("/nonexistent/dir/file.fxt.gz"), std::runtime_error);
}

TEST_CASE("make_sink throws for bad zst path", "[error_path]") {
  CHECK_THROWS_AS(
    make_sink("/nonexistent/dir/file.fxt.zst"), std::runtime_error);
}
