#include <tracey_mctraceface/fxt_writer.hpp>

#include <wire_types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace tracey_mctraceface {

  namespace {

    constexpr std::uint64_t fxt_magic = 0x0016547846040010ULL;

    constexpr std::uint8_t obj_type_process = 1;
    constexpr std::uint8_t obj_type_thread = 2;

    constexpr std::uint8_t event_duration_begin = 2;
    constexpr std::uint8_t event_duration_end = 3;

    auto
    words_for_bytes(std::size_t bytes) -> std::uint16_t {
      return static_cast<std::uint16_t>((bytes + 7) / 8);
    }

  } // namespace

  FxtWriter::FxtWriter(OutputSink& sink)
      : sink_(sink) {}

  void
  FxtWriter::write_preamble(
    std::string_view provider_name,
    std::uint32_t provider_id,
    std::uint64_t ticks_per_second,
    std::uint64_t base_ticks,
    std::uint64_t base_time_ns) {
    // 1. Magic number
    fxt::MagicNumberRecord_owned magic;
    magic.set_magic(fxt_magic);
    sink_.write(magic.buffer());

    // 2. Provider info metadata (variable-length)
    auto name_len = static_cast<std::uint16_t>(
      std::min(provider_name.size(), std::size_t{4095}));
    auto padded_words = words_for_bytes(name_len);
    auto rsize = static_cast<std::uint16_t>(1 + padded_words);

    fxt::ProviderInfoHeader_owned info_header;
    info_header.set_rtype(0); // metadata
    info_header.set_rsize(rsize);
    info_header.set_mtype(1); // provider info
    info_header.set_name_len(name_len);
    info_header.set_provider_id(provider_id);
    sink_.write(info_header.buffer());
    sink_.write(
      {reinterpret_cast<const std::byte*>(provider_name.data()), name_len});
    pad_to_word(name_len);

    // 3. Provider section metadata
    fxt::ProviderSectionMetadata_owned section;
    section.set_rtype(0); // metadata
    section.set_rsize(1);
    section.set_mtype(2); // provider section
    section.set_provider_id(provider_id);
    sink_.write(section.buffer());

    // 4. Initialization record (extended 4-word form)
    fxt::InitRecord_owned init;
    init.set_rtype(1); // initialization
    init.set_rsize(4); // 4-word extended form
    init.set_ticks_per_second(ticks_per_second);
    init.set_base_ticks(base_ticks);
    init.set_base_time_ns(base_time_ns);
    sink_.write(init.buffer());
  }

  auto
  FxtWriter::intern_string(std::string_view s) -> std::uint16_t {
    if (s.empty()) return 0;

    std::string key(s);
    auto it = string_table_.find(key);
    if (it != string_table_.end()) return it->second;

    auto id = next_string_id_++;
    string_table_.emplace(std::move(key), id);
    emit_string_record(id, s);
    return id;
  }

  auto
  FxtWriter::ensure_thread(std::uint64_t pid, std::uint64_t tid)
    -> std::uint8_t {
    ThreadKey key{pid, tid};
    auto it = thread_table_.find(key);
    if (it != thread_table_.end()) return it->second;

    // Assign next slot (1-based, wrapping at 255)
    auto slot =
      static_cast<std::uint8_t>((next_thread_slot_ % max_thread_slots_) + 1);
    ++next_thread_slot_;

    // If this slot was previously used, remove the old mapping
    for (auto jt = thread_table_.begin(); jt != thread_table_.end(); ++jt) {
      if (jt->second == slot) {
        thread_table_.erase(jt);
        break;
      }
    }

    thread_table_.emplace(key, slot);
    emit_thread_record(slot, pid, tid);

    // Auto-emit kernel object records for Perfetto display
    if (named_processes_.insert(pid).second) {
      write_process_name(pid, "[pid=" + std::to_string(pid) + "]");
    }
    write_thread_name(pid, tid, "[tid=" + std::to_string(tid) + "]");

    return slot;
  }

  void
  FxtWriter::write_duration_begin(
    std::uint64_t pid,
    std::uint64_t tid,
    std::string_view category,
    std::string_view name,
    std::uint64_t timestamp) {
    emit_event(event_duration_begin, pid, tid, category, name, timestamp);
  }

  void
  FxtWriter::write_duration_end(
    std::uint64_t pid,
    std::uint64_t tid,
    std::string_view category,
    std::string_view name,
    std::uint64_t timestamp) {
    emit_event(event_duration_end, pid, tid, category, name, timestamp);
  }

  void
  FxtWriter::write_process_name(std::uint64_t pid, std::string_view name) {
    auto name_id = intern_string(name);

    fxt::KernelObjectHeader_owned header;
    header.set_rtype(7); // kernel object
    header.set_rsize(2);
    header.set_obj_type(obj_type_process);
    header.set_name_ref(name_id);
    header.set_num_args(0);
    header.set_koid(pid);
    sink_.write(header.buffer());
  }

  void
  FxtWriter::write_thread_name(
    std::uint64_t pid, std::uint64_t tid, std::string_view name) {
    auto name_id = intern_string(name);
    auto process_str_id = intern_string("process");

    // Thread kernel object: koid=tid, 1 arg (process koid)
    fxt::KernelObjectHeader_owned header;
    header.set_rtype(7); // kernel object
    header.set_rsize(4); // 2 (header+koid) + 2 (koid arg)
    header.set_obj_type(obj_type_thread);
    header.set_name_ref(name_id);
    header.set_num_args(1);
    header.set_koid(tid);
    sink_.write(header.buffer());

    // Process koid argument
    fxt::ArgKoid_owned arg;
    arg.set_arg_type(8); // koid
    arg.set_arg_size(2);
    arg.set_arg_name(process_str_id);
    arg.set_value(pid);
    sink_.write(arg.buffer());
  }

  void
  FxtWriter::emit_string_record(std::uint16_t id, std::string_view s) {
    auto str_len =
      static_cast<std::uint16_t>(std::min(s.size(), std::size_t{31999}));
    auto padded_words = words_for_bytes(str_len);
    auto rsize = static_cast<std::uint16_t>(1 + padded_words);

    fxt::StringRecordHeader_owned header;
    header.set_rtype(2); // string
    header.set_rsize(rsize);
    header.set_string_id(id);
    header.set_str_len(str_len);
    sink_.write(header.buffer());
    sink_.write({reinterpret_cast<const std::byte*>(s.data()), str_len});
    pad_to_word(str_len);
  }

  void
  FxtWriter::emit_thread_record(
    std::uint8_t slot, std::uint64_t pid, std::uint64_t tid) {
    fxt::ThreadRecord_owned record;
    record.set_rtype(3); // thread
    record.set_rsize(3);
    record.set_thread_index(slot);
    record.set_pid(pid);
    record.set_tid(tid);
    sink_.write(record.buffer());
  }

  void
  FxtWriter::emit_event(
    std::uint8_t event_type,
    std::uint64_t pid,
    std::uint64_t tid,
    std::string_view category,
    std::string_view name,
    std::uint64_t timestamp) {
    auto thread_ref = ensure_thread(pid, tid);
    auto cat_id = intern_string(category);
    auto name_id = intern_string(name);

    fxt::EventRecordHeader_owned header;
    header.set_rtype(4); // event
    header.set_rsize(2); // header + timestamp, no args
    header.set_event_type(event_type);
    header.set_num_args(0);
    header.set_thread_ref(thread_ref);
    header.set_category_ref(cat_id);
    header.set_name_ref(name_id);
    header.set_timestamp(timestamp);
    sink_.write(header.buffer());
  }

  void
  FxtWriter::pad_to_word(std::size_t byte_count) {
    auto padding = (-byte_count) & 7;
    if (padding > 0) {
      std::array<std::byte, 8> zeros{};
      sink_.write({zeros.data(), padding});
    }
  }

  auto
  FxtWriter::ThreadKeyHash::operator()(const ThreadKey& k) const
    -> std::size_t {
    auto h1 = std::hash<std::uint64_t>{}(k.pid);
    auto h2 = std::hash<std::uint64_t>{}(k.tid);
    return h1 ^ (h2 << 1);
  }

} // namespace tracey_mctraceface
