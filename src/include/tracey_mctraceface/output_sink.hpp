#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief Abstract output interface for binary trace data.
   */
  class OutputSink {
  public:
    virtual ~OutputSink() = default;

    /** @brief Write raw bytes to the output. */
    virtual void
    write(std::span<const std::byte> data) = 0;
  };

  /**
   * @brief In-memory sink for testing — appends to a byte vector.
   */
  class VectorSink : public OutputSink {
    std::vector<std::byte> buf_;

  public:
    void
    write(std::span<const std::byte> data) override {
      buf_.insert(buf_.end(), data.begin(), data.end());
    }

    /** @brief Access the accumulated bytes. */
    auto
    data() const -> std::span<const std::byte> {
      return buf_;
    }

    /** @brief Number of bytes written so far. */
    auto
    size() const -> std::size_t {
      return buf_.size();
    }

    /** @brief Discard all accumulated data. */
    void
    clear() {
      buf_.clear();
    }
  };

} // namespace tracey_mctraceface
