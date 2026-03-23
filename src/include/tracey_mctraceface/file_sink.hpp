#pragma once

#include <tracey_mctraceface/output_sink.hpp>

#include <cstdio>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

namespace tracey_mctraceface {

  /**
   * @brief OutputSink that writes to a file via fwrite().
   */
  class FileSink : public OutputSink {
  public:
    /** @brief Open a file for writing. Throws on failure. */
    explicit FileSink(const std::filesystem::path& path);

    ~FileSink() override;

    FileSink(const FileSink&) = delete;
    auto
    operator=(const FileSink&) -> FileSink& = delete;
    FileSink(FileSink&&) noexcept;
    auto
    operator=(FileSink&&) noexcept -> FileSink&;

    void
    write(std::span<const std::byte> data) override;

    /** @brief Flush and close the file. */
    void
    close();

  private:
    std::FILE* file_ = nullptr;
  };

} // namespace tracey_mctraceface
