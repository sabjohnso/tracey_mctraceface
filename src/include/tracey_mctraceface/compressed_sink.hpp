#pragma once

#include <tracey_mctraceface/output_sink.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace tracey_mctraceface {

  /**
   * @brief OutputSink that writes gzip-compressed data (.fxt.gz).
   */
  class GzipSink : public OutputSink {
  public:
    explicit GzipSink(const std::filesystem::path& path);
    ~GzipSink() override;

    GzipSink(const GzipSink&) = delete;
    auto
    operator=(const GzipSink&) -> GzipSink& = delete;

    void
    write(std::span<const std::byte> data) override;

    void
    close();

  private:
    void* gz_file_ = nullptr; // gzFile (opaque to avoid zlib.h in header)
  };

  /**
   * @brief OutputSink that writes zstd-compressed data (.fxt.zst).
   */
  class ZstdSink : public OutputSink {
  public:
    explicit ZstdSink(const std::filesystem::path& path);
    ~ZstdSink() override;

    ZstdSink(const ZstdSink&) = delete;
    auto
    operator=(const ZstdSink&) -> ZstdSink& = delete;

    void
    write(std::span<const std::byte> data) override;

    void
    close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

  /**
   * @brief Create the appropriate sink based on file extension.
   *
   * .fxt.gz → GzipSink, .fxt.zst → ZstdSink, otherwise → FileSink.
   */
  auto
  make_sink(const std::filesystem::path& path) -> std::unique_ptr<OutputSink>;

} // namespace tracey_mctraceface
