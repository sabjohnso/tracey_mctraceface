#include <tracey_mctraceface/perf_script_parser.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace tracey_mctraceface {

  namespace {

    auto
    parse_hex64(std::string_view s) -> std::uint64_t {
      std::uint64_t val = 0;
      if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
      std::from_chars(s.data(), s.data() + s.size(), val, 16);
      return val;
    }

    auto
    parse_uint32(std::string_view s) -> std::uint32_t {
      std::uint32_t val = 0;
      std::from_chars(s.data(), s.data() + s.size(), val);
      return val;
    }

    auto
    parse_int32(std::string_view s) -> std::int32_t {
      std::int32_t val = 0;
      std::from_chars(s.data(), s.data() + s.size(), val);
      return val;
    }

    // Skip leading spaces, return remaining view
    auto
    skip_spaces(std::string_view s) -> std::string_view {
      auto pos = s.find_first_not_of(" \t");
      return pos == std::string_view::npos ? std::string_view{} : s.substr(pos);
    }

    // Consume digits, return the digits and advance s past them
    auto
    consume_digits(std::string_view& s) -> std::string_view {
      std::size_t i = 0;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        ++i;
      auto result = s.substr(0, i);
      s.remove_prefix(i);
      return result;
    }

    // Consume hex digits
    auto
    consume_hex(std::string_view& s) -> std::string_view {
      std::size_t i = 0;
      while (i < s.size() &&
             ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= 'A' && s[i] <= 'F')))
        ++i;
      auto result = s.substr(0, i);
      s.remove_prefix(i);
      return result;
    }

    auto
    parse_branch_kind(std::string_view kind_str) -> std::optional<BranchKind> {
      if (kind_str == "call") return BranchKind::Call;
      if (kind_str == "return") return BranchKind::Return;
      if (kind_str == "jmp" || kind_str == "jcc") return BranchKind::Jump;
      if (kind_str == "syscall") return BranchKind::Syscall;
      if (kind_str == "sysret") return BranchKind::Sysret;
      if (kind_str == "hw int") return BranchKind::HardwareInterrupt;
      if (kind_str == "int") return BranchKind::Interrupt;
      if (kind_str == "iret") return BranchKind::Iret;
      if (kind_str == "async") return BranchKind::Async;
      if (kind_str == "tx abrt") return BranchKind::TxAbort;
      if (kind_str.empty()) return std::nullopt;
      return std::nullopt;
    }

    struct KindAndTraceState {
      std::optional<BranchKind> kind;
      TraceState trace_state = TraceState::None;
    };

    auto
    parse_kind_and_trace(std::string_view raw) -> KindAndTraceState {
      KindAndTraceState result;
      bool starts_trace = false;
      bool ends_trace = false;

      if (raw.starts_with("tr strt")) {
        starts_trace = true;
        raw.remove_prefix(7);
        if (raw.starts_with(" jmp")) {
          raw = "jmp";
        } else if (!raw.empty() && raw[0] == ' ') {
          raw.remove_prefix(1);
        }
      }

      if (raw.starts_with("tr end")) {
        ends_trace = true;
        raw.remove_prefix(6);
        if (raw.starts_with("  "))
          raw.remove_prefix(2);
        else if (!raw.empty() && raw[0] == ' ')
          raw.remove_prefix(1);
      }

      result.kind = parse_branch_kind(raw);

      if (starts_trace && !ends_trace)
        result.trace_state = TraceState::Start;
      else if (!starts_trace && ends_trace)
        result.trace_state = TraceState::End;

      return result;
    }

    auto
    is_continuation_line(std::string_view line) -> bool {
      return !line.empty() && line[0] == '\t';
    }

  } // namespace

  auto
  PerfScriptParser::parse_time(std::string_view hi, std::string_view lo)
    -> std::uint64_t {
    std::uint64_t time_hi = 0;
    std::from_chars(hi.data(), hi.data() + hi.size(), time_hi);

    // Pad or truncate lo to 9 digits (nanoseconds)
    char lo_buf[9] = {'0', '0', '0', '0', '0', '0', '0', '0', '0'};
    auto copy_len = std::min(lo.size(), std::size_t{9});
    std::copy_n(lo.data(), copy_len, lo_buf);

    std::uint64_t time_lo = 0;
    std::from_chars(lo_buf, lo_buf + 9, time_lo);

    return time_hi * 1'000'000'000ULL + time_lo;
  }

  auto
  PerfScriptParser::parse_symbol_and_offset(std::string_view str)
    -> std::pair<std::string, std::int32_t> {
    // Find the last '+0x' which separates symbol from offset
    auto plus_pos = str.rfind("+0x");
    if (plus_pos != std::string_view::npos) {
      auto sym = str.substr(0, plus_pos);
      auto offset_start = str.substr(plus_pos + 1);
      // offset_start = "0xHEX (dso)"
      // Find the space before (dso)
      auto space = offset_start.find(' ');
      auto offset_hex = (space != std::string_view::npos)
                          ? offset_start.substr(0, space)
                          : offset_start;
      auto offset64 = parse_hex64(offset_hex);
      auto offset32 =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(offset64));
      return {std::string(sym), offset32};
    }

    // Check for "[unknown] (dso)"
    if (str.starts_with("[unknown]")) { return {"[unknown]", 0}; }

    return {"[unknown]", 0};
  }

  auto
  PerfScriptParser::parse_location(
    std::string_view ip_hex, std::string_view sym_str) -> Location {
    Location loc;
    loc.ip = parse_hex64(ip_hex);
    auto [sym, offset] = parse_symbol_and_offset(sym_str);
    loc.symbol = std::move(sym);
    loc.symbol_offset = offset;
    return loc;
  }

  auto
  PerfScriptParser::parse_decode_error(std::string_view line)
    -> std::optional<DecodeError> {
    // " instruction trace error type N [time T.T] cpu C pid P tid T ip IP code
    // N: msg"
    auto s = skip_spaces(line);
    if (!s.starts_with("instruction trace error")) return std::nullopt;

    DecodeError err;

    // Find "time" field (optional)
    auto time_pos = s.find("time ");
    if (time_pos != std::string_view::npos) {
      auto t = s.substr(time_pos + 5);
      auto hi = consume_digits(t);
      if (!t.empty() && t[0] == '.') {
        t.remove_prefix(1);
        auto lo = consume_digits(t);
        err.time_ns = parse_time(hi, lo);
      }
    }

    // Find "pid" and "tid"
    auto pid_pos = s.find("pid ");
    auto tid_pos = s.find("tid ");
    if (pid_pos != std::string_view::npos) {
      auto p = s.substr(pid_pos + 4);
      err.pid = static_cast<std::uint32_t>(parse_int32(p));
    }
    if (tid_pos != std::string_view::npos) {
      auto t = s.substr(tid_pos + 4);
      err.tid = static_cast<std::uint32_t>(parse_int32(t));
    }

    // Find "ip"
    auto ip_pos = s.find("ip ");
    if (ip_pos != std::string_view::npos) {
      auto i = s.substr(ip_pos + 3);
      auto space = i.find(' ');
      auto ip_str = (space != std::string_view::npos) ? i.substr(0, space) : i;
      if (ip_str != "0") { err.ip = parse_hex64(ip_str); }
    }

    // Find message after "code N: "
    auto code_pos = s.find("code ");
    if (code_pos != std::string_view::npos) {
      auto c = s.substr(code_pos);
      auto colon = c.find(": ");
      if (colon != std::string_view::npos) {
        err.message = std::string(c.substr(colon + 2));
      }
    }

    return err;
  }

  auto
  PerfScriptParser::parse_group(const std::vector<std::string>& lines)
    -> std::optional<Event> {
    if (lines.empty()) return std::nullopt;

    std::string_view header_line = lines[0];

    // Check for decode error
    if (auto err = parse_decode_error(header_line)) { return Event{*err}; }

    // Parse main header:
    // " pid/tid time_hi.time_lo: period event_name/config:selector:rest"
    auto s = skip_spaces(header_line);
    if (s.empty()) return std::nullopt;

    // pid/tid
    auto pid_sv = consume_digits(s);
    if (pid_sv.empty() || s.empty() || s[0] != '/') return std::nullopt;
    s.remove_prefix(1); // skip '/'
    auto tid_sv = consume_digits(s);

    auto pid = parse_uint32(pid_sv);
    auto tid = parse_uint32(tid_sv);

    // skip spaces
    s = skip_spaces(s);

    // time_hi.time_lo:
    auto time_hi = consume_digits(s);
    if (s.empty() || s[0] != '.') return std::nullopt;
    s.remove_prefix(1);
    auto time_lo = consume_digits(s);
    if (s.empty() || s[0] != ':') return std::nullopt;
    s.remove_prefix(1);

    auto time_ns = parse_time(time_hi, time_lo);

    // skip spaces + period
    s = skip_spaces(s);
    consume_digits(s); // period (ignored)
    s = skip_spaces(s);

    // event_name: letters and hyphens up to '/' or ':'
    std::size_t name_end = 0;
    while (name_end < s.size() && s[name_end] != '/' && s[name_end] != ':' &&
           s[name_end] != ' ')
      ++name_end;
    auto event_name = s.substr(0, name_end);
    s.remove_prefix(name_end);

    // skip "/config:selector:" — the non-space chars right after event_name
    // e.g., "branches:uH:" or "cbr:" or "cycles:u:"
    while (!s.empty() && s[0] != ' ') {
      s.remove_prefix(1);
    }
    auto rest = s;

    // Branch events
    if (event_name == "branches") {
      auto r = skip_spaces(rest);

      // Parse kind (up to first space+hex or space+(x))
      // Find the pattern: "kind  [+spaces+] [(x)] hex_addr sym"
      // The kind ends at the first hex address preceded by spaces
      auto arrow = r.find(" => ");
      if (arrow == std::string_view::npos) return std::nullopt;

      auto before_arrow = r.substr(0, arrow);
      auto after_arrow = r.substr(arrow + 4);

      // In before_arrow: "kind [(x)] hex_addr sym+off (dso)"
      // Find the hex address: scan for a hex digit sequence preceded by spaces
      // that looks like an address (after the kind + optional (x))

      // Strategy: find the first hex digit that starts an address.
      // The kind is letters/spaces, then optional (x), then spaces, then hex
      // addr.
      bool in_transaction = false;

      // Find "(x)" flag
      auto paren_pos = before_arrow.find("(x)");
      if (paren_pos != std::string_view::npos) { in_transaction = true; }

      // Find the hex address: look for a sequence of hex digits preceded by
      // spaces We scan from the back to find " hex_addr " pattern before the
      // symbol Actually, the address is right before the symbol. Format: "kind
      // [spaces]  [(x)]  [spaces]  hex_addr  sym+0xoff (dso)" The hex address
      // is a run of [0-9a-f] preceded by a space.

      // Simpler: find "=> " in the full line to split src/dst,
      // then in each half, the hex addr is the first hex-only token.
      // Let me find the first hex address by skipping the kind.

      // Skip the kind by finding the first hex char after spaces
      std::string_view src_ip_str;
      std::string_view src_sym_str;

      // Find the hex address: it's a long hex number after spaces
      // Scan for a space followed by a hex digit
      std::size_t addr_start = std::string_view::npos;
      for (std::size_t i = 1; i < before_arrow.size(); ++i) {
        if (
          before_arrow[i - 1] == ' ' &&
          ((before_arrow[i] >= '0' && before_arrow[i] <= '9') ||
           (before_arrow[i] >= 'a' && before_arrow[i] <= 'f'))) {
          // Check if this is a long hex number (address), not part of kind
          std::size_t j = i;
          while (j < before_arrow.size() &&
                 ((before_arrow[j] >= '0' && before_arrow[j] <= '9') ||
                  (before_arrow[j] >= 'a' && before_arrow[j] <= 'f')))
            ++j;
          if (j - i >= 1 && j < before_arrow.size() && before_arrow[j] == ' ') {
            addr_start = i;
            break;
          }
        }
      }

      if (addr_start == std::string_view::npos) return std::nullopt;

      // Kind is everything before the address (trimmed)
      auto raw_kind = before_arrow.substr(0, addr_start);
      // Trim trailing spaces and "(x)"
      while (!raw_kind.empty() && raw_kind.back() == ' ')
        raw_kind.remove_suffix(1);
      if (raw_kind.ends_with("(x)")) {
        raw_kind.remove_suffix(3);
        while (!raw_kind.empty() && raw_kind.back() == ' ')
          raw_kind.remove_suffix(1);
      }

      // Source: addr + sym
      auto src_rest = before_arrow.substr(addr_start);
      auto src_space = src_rest.find(' ');
      src_ip_str = (src_space != std::string_view::npos)
                     ? src_rest.substr(0, src_space)
                     : src_rest;
      src_sym_str = (src_space != std::string_view::npos)
                      ? skip_spaces(src_rest.substr(src_space))
                      : std::string_view{};

      // Destination: same pattern
      auto dst_rest = skip_spaces(after_arrow);
      auto dst_sv = dst_rest;
      auto dst_hex = consume_hex(dst_sv);
      dst_sv = skip_spaces(dst_sv);

      auto [kind, trace_state] = parse_kind_and_trace(raw_kind);

      BranchData data;
      data.kind = kind;
      data.trace_state = trace_state;
      data.in_transaction = in_transaction;
      data.src = parse_location(src_ip_str, src_sym_str);
      data.dst = parse_location(dst_hex, dst_sv);

      return Event{OkEvent{pid, tid, time_ns, data}};
    }

    // CBR events
    if (event_name == "cbr") {
      auto freq_pos = rest.find("freq: ");
      if (freq_pos != std::string_view::npos) {
        auto f = rest.substr(freq_pos + 6);
        auto freq = parse_uint32(f);
        return Event{OkEvent{pid, tid, time_ns, PowerData{freq}}};
      }
      return std::nullopt;
    }

    // PSB events — silently ignored
    if (event_name == "psb") { return std::nullopt; }

    // Cycles / sampling events with callstacks
    if (event_name == "cycles") {
      std::vector<Location> callstack;
      for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view entry = lines[i];
        entry = skip_spaces(entry);
        if (entry.empty()) continue;
        auto hex = consume_hex(entry);
        if (hex.empty()) continue;
        entry = skip_spaces(entry);
        callstack.push_back(parse_location(hex, entry));
      }
      std::ranges::reverse(callstack);
      return Event{
        OkEvent{pid, tid, time_ns, StacktraceSampleData{std::move(callstack)}}};
    }

    // Sampled events: branch-misses, cache-misses
    if (event_name == "branch-misses" || event_name == "cache-misses") {
      auto name = (event_name == "branch-misses")
                    ? SampleEventName::BranchMisses
                    : SampleEventName::CacheMisses;
      // Period was already consumed above; we'd need to re-parse.
      // For now, use count=1.
      if (lines.size() > 1) {
        std::string_view entry = lines[1];
        entry = skip_spaces(entry);
        auto hex = consume_hex(entry);
        entry = skip_spaces(entry);
        auto loc = parse_location(hex, entry);
        return Event{
          OkEvent{pid, tid, time_ns, EventSampleData{std::move(loc), 1, name}}};
      }
      return std::nullopt;
    }

    return std::nullopt;
  }

  auto
  PerfScriptParser::flush_accumulated() -> std::optional<Event> {
    if (accumulated_lines_.empty()) return std::nullopt;

    auto result = parse_group(accumulated_lines_);
    accumulated_lines_.clear();
    return result;
  }

  auto
  PerfScriptParser::feed_line(std::string_view line) -> std::optional<Event> {
    if (line.empty()) { return flush_accumulated(); }

    if (is_continuation_line(line)) {
      accumulated_lines_.emplace_back(line);
      return std::nullopt;
    }

    auto result = flush_accumulated();
    accumulated_lines_.emplace_back(line);

    if (result) return result;
    return std::nullopt;
  }

  auto
  PerfScriptParser::finish() -> std::optional<Event> {
    return flush_accumulated();
  }

} // namespace tracey_mctraceface
