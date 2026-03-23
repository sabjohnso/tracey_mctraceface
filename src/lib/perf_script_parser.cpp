#include <tracey_mctraceface/perf_script_parser.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>

namespace tracey_mctraceface {

  namespace {

    // Static regex patterns — compiled once.

    // Main event header:
    // " pid/tid time_hi.time_lo: period event_name/config:selector:rest"
    const std::regex header_re(
      R"(^ *(\d+)/(\d+) +(\d+)\.(\d+): +(\d+) +)"
      R"(([a-z\-]+)(?:/[a-z=0-9]+)?(?:/[a-zA-Z]*)?:(?:[a-zA-Z]+:)?(.*)$)");

    // Decode error:
    // " instruction trace error type N [time T.T] cpu C pid P tid T ip IP code
    // N: msg"
    const std::regex error_re(
      R"(^ instruction trace error type \d+)"
      R"( (?:time (\d+)\.(\d+) )?)"
      R"(cpu [\-\d]+ pid ([\-\d]+) tid ([\-\d]+))"
      R"( ip (0x[0-9a-fA-F]+|0) code \d+: (.*)$)");

    // Branch event body:
    // " kind [(x)] src_ip sym+off (dso) => dst_ip sym+off (dso)"
    const std::regex branch_re(
      R"(^ *(call|return|tr strt(?: jmp)?|syscall|sysret|)"
      R"(hw int|iret|int|tx abrt|tr end|tr strt tr end|)"
      R"(tr end  (?:async|call|return|syscall|sysret|iret)|)"
      R"(jmp|jcc) +)"
      R"((?:\(([^)]*)\) +)?)"
      R"(([0-9a-f]+) (.*?) => +([0-9a-f]+) (.*)$)");

    // CBR event body: "cbr: N freq: M MHz ..."
    const std::regex cbr_re(R"(cbr: +\d+ +freq: +(\d+) MHz)");

    // Callstack entry: "\t ip sym+off (dso)"
    const std::regex callstack_re(R"(^\t *([0-9a-f]+) (.*)$)");

    // Symbol and offset: "symbol+0xoffset (dso)"
    const std::regex symbol_re(R"(^(.*)\+(0x[0-9a-f]+)\s+\(.*\)$)");

    // Unknown symbol: "[unknown] (dso)"
    const std::regex unknown_sym_re(R"(^\[unknown\]\s+\((.*)\)$)");

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
      std::string s(raw);

      bool starts_trace = false;
      bool ends_trace = false;

      // Check for "tr strt" prefix
      if (s.starts_with("tr strt")) {
        starts_trace = true;
        s = s.substr(7);
        // "tr strt jmp" means trace start + jump kind
        if (s.starts_with(" jmp")) {
          s = "jmp";
        } else if (!s.empty() && s[0] == ' ') {
          s = s.substr(1);
        }
      }

      // Check for "tr end" prefix in remainder
      if (s.starts_with("tr end")) {
        ends_trace = true;
        s = s.substr(6);
        // Remove double space + kind after "tr end  kind"
        if (s.starts_with("  "))
          s = s.substr(2);
        else if (!s.empty() && s[0] == ' ')
          s = s.substr(1);
      }

      result.kind = parse_branch_kind(s);

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
    std::string lo_padded(lo);
    if (lo_padded.size() < 9) {
      lo_padded.append(9 - lo_padded.size(), '0');
    } else if (lo_padded.size() > 9) {
      lo_padded.resize(9);
    }

    std::uint64_t time_lo = 0;
    std::from_chars(
      lo_padded.data(), lo_padded.data() + lo_padded.size(), time_lo);

    return time_hi * 1'000'000'000ULL + time_lo;
  }

  auto
  PerfScriptParser::parse_symbol_and_offset(std::string_view str)
    -> std::pair<std::string, std::int32_t> {
    std::cmatch m;
    std::string s(str);

    if (std::regex_match(s.c_str(), m, symbol_re)) {
      auto sym = m[1].str();
      auto offset_hex = m[2].str();
      // Parse as uint64 then truncate to int32 (matches OCaml behavior)
      auto offset64 = parse_hex64(offset_hex);
      auto offset32 =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(offset64));
      return {sym, offset32};
    }

    if (std::regex_match(s.c_str(), m, unknown_sym_re)) {
      return {"[unknown]", 0};
    }

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
    std::cmatch m;
    std::string s(line);
    if (!std::regex_match(s.c_str(), m, error_re)) return std::nullopt;

    DecodeError err;

    // Time is optional
    if (m[1].matched && m[2].matched) {
      err.time_ns = parse_time(m[1].str(), m[2].str());
    }

    auto pid_str = m[3].str();
    auto tid_str = m[4].str();
    err.pid = static_cast<std::uint32_t>(parse_int32(pid_str));
    err.tid = static_cast<std::uint32_t>(parse_int32(tid_str));

    auto ip_str = m[5].str();
    if (ip_str != "0") { err.ip = parse_hex64(ip_str); }

    err.message = m[6].str();
    return err;
  }

  auto
  PerfScriptParser::parse_group(const std::vector<std::string>& lines)
    -> std::optional<Event> {
    if (lines.empty()) return std::nullopt;

    const auto& header_line = lines[0];

    // Check for decode error
    if (auto err = parse_decode_error(header_line)) { return Event{*err}; }

    // Parse main header
    std::cmatch hm;
    if (!std::regex_match(header_line.c_str(), hm, header_re))
      return std::nullopt;

    auto pid = parse_uint32(hm[1].str());
    auto tid = parse_uint32(hm[2].str());
    auto time_ns = parse_time(hm[3].str(), hm[4].str());
    auto event_name = hm[6].str();
    auto rest = hm[7].str();

    // Branch events
    if (event_name == "branches") {
      std::cmatch bm;
      if (!std::regex_match(rest.c_str(), bm, branch_re)) return std::nullopt;

      auto raw_kind = bm[1].str();
      auto aux_flags = bm[2].str();
      auto src_ip = bm[3].str();
      auto src_sym = bm[4].str();
      auto dst_ip = bm[5].str();
      auto dst_sym = bm[6].str();

      auto [kind, trace_state] = parse_kind_and_trace(raw_kind);
      bool in_tx = aux_flags.find('x') != std::string::npos;

      BranchData data;
      data.kind = kind;
      data.trace_state = trace_state;
      data.in_transaction = in_tx;
      data.src = parse_location(src_ip, src_sym);
      data.dst = parse_location(dst_ip, dst_sym);

      return Event{OkEvent{pid, tid, time_ns, data}};
    }

    // CBR events
    if (event_name == "cbr") {
      std::cmatch cm;
      if (std::regex_search(rest.c_str(), cm, cbr_re)) {
        auto freq = parse_uint32(cm[1].str());
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
        std::cmatch cm;
        std::string line_str(lines[i]);
        if (std::regex_match(line_str.c_str(), cm, callstack_re)) {
          callstack.push_back(parse_location(cm[1].str(), cm[2].str()));
        }
      }
      // Reverse: innermost-first becomes outermost-first
      std::ranges::reverse(callstack);
      return Event{
        OkEvent{pid, tid, time_ns, StacktraceSampleData{std::move(callstack)}}};
    }

    // Sampled events: branch-misses, cache-misses
    if (event_name == "branch-misses" || event_name == "cache-misses") {
      auto name = (event_name == "branch-misses")
                    ? SampleEventName::BranchMisses
                    : SampleEventName::CacheMisses;
      auto period = parse_uint32(hm[5].str());

      // Try to get location from first callstack line
      if (lines.size() > 1) {
        std::cmatch cm;
        std::string line_str(lines[1]);
        if (std::regex_match(line_str.c_str(), cm, callstack_re)) {
          auto loc = parse_location(cm[1].str(), cm[2].str());
          return Event{OkEvent{
            pid,
            tid,
            time_ns,
            EventSampleData{std::move(loc), static_cast<int>(period), name}}};
        }
      }

      // Single-line: parse rest as "period ip sym+off"
      std::regex sample_re(R"(^ *(\d+) +([0-9a-f]+) (.*)$)");
      std::cmatch sm;
      if (std::regex_match(rest.c_str(), sm, sample_re)) {
        auto loc = parse_location(sm[2].str(), sm[3].str());
        return Event{OkEvent{
          pid,
          tid,
          time_ns,
          EventSampleData{std::move(loc), static_cast<int>(period), name}}};
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
    // Empty line: flush accumulated
    if (line.empty()) { return flush_accumulated(); }

    // Continuation line (tab-indented): append to accumulated
    if (is_continuation_line(line)) {
      accumulated_lines_.emplace_back(line);
      return std::nullopt;
    }

    // New header line: flush previous, start new accumulation
    auto result = flush_accumulated();
    accumulated_lines_.emplace_back(line);

    // If there was a previous event, return it
    if (result) return result;

    // For single-line events, we can't parse yet — wait for next line
    // to determine if this is multi-line
    return std::nullopt;
  }

  auto
  PerfScriptParser::finish() -> std::optional<Event> {
    return flush_accumulated();
  }

} // namespace tracey_mctraceface
