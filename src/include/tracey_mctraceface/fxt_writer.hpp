#pragma once

#include <tracey_mctraceface/output_sink.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief Writes Fuchsia Trace Format (FXT) binary trace files.
   *
   * Handles string interning, thread slot management, and record
   * assembly on top of BES-generated wire types.
   */
  class FxtWriter {
  public:
    /** @brief Construct a writer that emits to the given sink. */
    explicit FxtWriter(OutputSink& sink);

    /**
     * @brief Write the FXT file preamble.
     *
     * Emits magic number, provider info, provider section, and
     * initialization records.
     */
    void
    write_preamble(
      std::string_view provider_name,
      std::uint32_t provider_id,
      std::uint64_t ticks_per_second,
      std::uint64_t base_ticks,
      std::uint64_t base_time_ns);

    /**
     * @brief Intern a string and return its 15-bit ID.
     *
     * Returns 0 for empty strings. Emits a string record on first
     * use of a given string.
     */
    auto
    intern_string(std::string_view s) -> std::uint16_t;

    /**
     * @brief Ensure a thread slot is assigned and return its 8-bit index.
     *
     * Emits a thread record when a new slot is assigned.
     */
    auto
    ensure_thread(std::uint64_t pid, std::uint64_t tid) -> std::uint8_t;

    /** @brief Write a duration begin event. */
    void
    write_duration_begin(
      std::uint64_t pid,
      std::uint64_t tid,
      std::string_view category,
      std::string_view name,
      std::uint64_t timestamp);

    /** @brief Write a duration end event. */
    void
    write_duration_end(
      std::uint64_t pid,
      std::uint64_t tid,
      std::string_view category,
      std::string_view name,
      std::uint64_t timestamp);

    /** @brief Write a process name kernel object record. */
    void
    write_process_name(std::uint64_t pid, std::string_view name);

    /** @brief Write a thread name kernel object record. */
    void
    write_thread_name(
      std::uint64_t pid, std::uint64_t tid, std::string_view name);

  private:
    OutputSink& sink_;

    // String interning: string -> 15-bit ID (1..32767), 0 = empty
    std::unordered_map<std::string, std::uint16_t> string_table_;
    std::uint16_t next_string_id_ = 1;

    // Thread slots: circular buffer of 255 entries
    struct ThreadKey {
      std::uint64_t pid;
      std::uint64_t tid;
      auto
      operator==(const ThreadKey&) const -> bool = default;
    };

    struct ThreadKeyHash {
      auto
      operator()(const ThreadKey& k) const -> std::size_t;
    };

    std::unordered_map<ThreadKey, std::uint8_t, ThreadKeyHash> thread_table_;
    std::unordered_set<std::uint64_t> named_processes_;
    std::uint8_t next_thread_slot_ = 0;
    static constexpr std::uint8_t max_thread_slots_ = 255;

    void
    emit_string_record(std::uint16_t id, std::string_view s);
    void
    emit_thread_record(std::uint8_t slot, std::uint64_t pid, std::uint64_t tid);
    void
    emit_event(
      std::uint8_t event_type,
      std::uint64_t pid,
      std::uint64_t tid,
      std::string_view category,
      std::string_view name,
      std::uint64_t timestamp);
    void
    pad_to_word(std::size_t byte_count);
  };

} // namespace tracey_mctraceface
