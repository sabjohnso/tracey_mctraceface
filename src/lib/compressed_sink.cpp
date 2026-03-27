#include <tracey_mctraceface/compressed_sink.hpp>
#include <tracey_mctraceface/file_sink.hpp>

#include <zlib.h>
#include <zstd.h>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  // =========================================================================
  // GzipSink
  // =========================================================================

  GzipSink::GzipSink(const std::filesystem::path& path) {
    gz_file_ = gzopen(path.c_str(), "wb");
    if (!gz_file_) {
      throw std::runtime_error("GzipSink: failed to open " + path.string());
    }
  }

  GzipSink::~GzipSink() { close(); }

  void
  GzipSink::write(std::span<const std::byte> data) {
    if (gz_file_ && !data.empty()) {
      auto written = gzwrite(
        static_cast<gzFile>(gz_file_),
        data.data(),
        static_cast<unsigned>(data.size()));
      if (written <= 0) { throw std::runtime_error("GzipSink: write failed"); }
    }
  }

  void
  GzipSink::close() {
    if (gz_file_) {
      gzclose(static_cast<gzFile>(gz_file_));
      gz_file_ = nullptr;
    }
  }

  // =========================================================================
  // ZstdSink
  // =========================================================================

  struct ZstdSink::Impl {
    std::FILE* file = nullptr;
    ZSTD_CCtx* ctx = nullptr;
    std::vector<std::byte> out_buf;

    Impl(const std::filesystem::path& path) {
      file = std::fopen(path.c_str(), "wb");
      if (!file) {
        throw std::runtime_error("ZstdSink: failed to open " + path.string());
      }

      ctx = ZSTD_createCCtx();
      if (!ctx) {
        std::fclose(file);
        file = nullptr;
        throw std::runtime_error("ZstdSink: failed to create context");
      }

      out_buf.resize(ZSTD_CStreamOutSize());
    }

    ~Impl() { close(); }

    void
    write(std::span<const std::byte> data) {
      if (!ctx || !file) return;

      ZSTD_inBuffer input = {data.data(), data.size(), 0};
      while (input.pos < input.size) {
        ZSTD_outBuffer output = {out_buf.data(), out_buf.size(), 0};
        auto rc = ZSTD_compressStream2(ctx, &output, &input, ZSTD_e_continue);
        if (ZSTD_isError(rc)) {
          throw std::runtime_error(
            std::string("ZstdSink: compress failed: ") + ZSTD_getErrorName(rc));
        }
        if (output.pos > 0) {
          auto written = std::fwrite(out_buf.data(), 1, output.pos, file);
          if (written != output.pos) {
            throw std::runtime_error("ZstdSink: write failed (disk full?)");
          }
        }
      }
    }

    void
    close() {
      if (ctx && file) {
        // Flush remaining compressed data
        ZSTD_inBuffer input = {nullptr, 0, 0};
        std::size_t remaining = 0;
        do {
          ZSTD_outBuffer output = {out_buf.data(), out_buf.size(), 0};
          remaining = ZSTD_compressStream2(ctx, &output, &input, ZSTD_e_end);
          if (ZSTD_isError(remaining)) break;
          if (output.pos > 0) {
            std::fwrite(out_buf.data(), 1, output.pos, file);
          }
        } while (remaining > 0);
      }
      if (ctx) {
        ZSTD_freeCCtx(ctx);
        ctx = nullptr;
      }
      if (file) {
        std::fclose(file);
        file = nullptr;
      }
    }
  };

  ZstdSink::ZstdSink(const std::filesystem::path& path)
      : impl_(std::make_unique<Impl>(path)) {}

  ZstdSink::~ZstdSink() = default;

  void
  ZstdSink::write(std::span<const std::byte> data) {
    impl_->write(data);
  }

  void
  ZstdSink::close() {
    impl_->close();
  }

  // =========================================================================
  // Factory
  // =========================================================================

  auto
  make_sink(const std::filesystem::path& path) -> std::unique_ptr<OutputSink> {
    auto name = path.string();
    if (name.ends_with(".gz")) { return std::make_unique<GzipSink>(path); }
    if (name.ends_with(".zst")) { return std::make_unique<ZstdSink>(path); }
    return std::make_unique<FileSink>(path);
  }

} // namespace tracey_mctraceface
