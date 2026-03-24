#include <tracey_mctraceface/fxt_writer.hpp>

#include <wire_types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>

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
    auto name_len = static_cast<std::uint8_t>(
      std::min(provider_name.size(), std::size_t{255}));
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

    std::uint16_t id;
    if (next_string_id_ <= max_string_id_) {
      // Normal path: allocate a new ID
      id = next_string_id_++;
    } else {
      // Exhausted: evict the least recently inserted string.
      // Find the entry with the lowest ID and reclaim it.
      if (!string_id_warning_issued_) {
        std::cerr << "warning: string ID limit reached (" << max_string_id_
                  << " unique symbols). Evicting old entries.\n";
        string_id_warning_issued_ = true;
      }

      // Evict: find entry with the smallest ID value
      auto victim = string_table_.begin();
      for (auto jt = string_table_.begin(); jt != string_table_.end(); ++jt) {
        if (jt->second < victim->second) victim = jt;
      }
      id = victim->second;
      string_table_.erase(victim);
    }

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
    std::string truncated;
    auto effective = s;

    if (s.size() > max_string_length_) {
      std::cerr << "warning: symbol name too long (" << s.size()
                << " bytes). Truncating: " << s.substr(0, 60) << "...\n";
      truncated = std::string(s.substr(0, max_string_length_ - 15));
      truncated += "<...truncated>";
      effective = truncated;
    }

    auto str_len = static_cast<std::uint16_t>(effective.size());
    auto padded_words = words_for_bytes(str_len);
    auto rsize = static_cast<std::uint16_t>(1 + padded_words);

    fxt::StringRecordHeader_owned header;
    header.set_rtype(2); // string
    header.set_rsize(rsize);
    header.set_string_id(id);
    header.set_str_len(str_len);
    sink_.write(header.buffer());
    sink_.write(
      {reinterpret_cast<const std::byte*>(effective.data()), str_len});
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
  FxtWriter::write_counter(
    std::uint64_t pid,
    std::uint64_t tid,
    std::string_view category,
    std::string_view name,
    std::uint64_t counter_id,
    std::int64_t value,
    std::uint64_t timestamp) {
    auto thread_ref = ensure_thread(pid, tid);
    auto cat_id = intern_string(category);
    auto name_id = intern_string(name);

    // Counter event: header(1) + timestamp(1) + counter_id(1) + arg(2) = 5
    // Arg is Int64: arg_type=3, arg_size=2
    auto value_name_id = intern_string("value");

    fxt::EventRecordHeader_owned header;
    header.set_rtype(4);      // event record
    header.set_rsize(5);      // 2 (header+ts) + 1 (counter_id) + 2 (int64 arg)
    header.set_event_type(1); // counter
    header.set_num_args(1);
    header.set_thread_ref(thread_ref);
    header.set_category_ref(cat_id);
    header.set_name_ref(name_id);
    header.set_timestamp(timestamp);
    sink_.write(header.buffer());

    // Counter ID word
    std::uint64_t cid = counter_id;
    sink_.write({reinterpret_cast<const std::byte*>(&cid), 8});

    // Int64 argument: arg_type=3, arg_size=2, name=value_name_id
    fxt::ArgInt64_owned arg;
    arg.set_arg_type(3);
    arg.set_arg_size(2);
    arg.set_arg_name(value_name_id);
    arg.set_value(value);
    sink_.write(arg.buffer());
  }

  void
  FxtWriter::write_event_by_id(
    std::uint8_t event_type,
    std::uint8_t thread_ref,
    std::uint16_t category_id,
    std::uint16_t name_id,
    std::uint64_t timestamp) {
    fxt::EventRecordHeader_owned header;
    header.set_rtype(4);
    header.set_rsize(2);
    header.set_event_type(event_type);
    header.set_num_args(0);
    header.set_thread_ref(thread_ref);
    header.set_category_ref(category_id);
    header.set_name_ref(name_id);
    header.set_timestamp(timestamp);
    sink_.write(header.buffer());
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
