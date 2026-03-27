#include <tracey_mctraceface/file_sink.hpp>

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace tracey_mctraceface {

  FileSink::FileSink(const std::filesystem::path& path) {
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) {
      throw std::runtime_error("FileSink: failed to open " + path.string());
    }
  }

  FileSink::~FileSink() { close(); }

  FileSink::FileSink(FileSink&& other) noexcept
      : file_(std::exchange(other.file_, nullptr)) {}

  auto
  FileSink::operator=(FileSink&& other) noexcept -> FileSink& {
    if (this != &other) {
      close();
      file_ = std::exchange(other.file_, nullptr);
    }
    return *this;
  }

  void
  FileSink::write(std::span<const std::byte> data) {
    if (file_ && !data.empty()) {
      auto written = std::fwrite(data.data(), 1, data.size(), file_);
      if (written != data.size()) {
        throw std::runtime_error("FileSink: write failed (disk full?)");
      }
    }
  }

  void
  FileSink::close() {
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

} // namespace tracey_mctraceface
