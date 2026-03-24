#include <tracey_mctraceface/perf_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace tracey_mctraceface;

namespace {

  auto
  contains(const std::vector<std::string>& v, const std::string& s) -> bool {
    return std::ranges::find(v, s) != v.end();
  }

} // namespace

TEST_CASE("scope_selector returns correct strings", "[perf_driver]") {
  CHECK(scope_selector(TraceScope::Userspace) == "u");
  CHECK(scope_selector(TraceScope::Kernel) == "k");
  CHECK(scope_selector(TraceScope::Both) == "uk");
}

TEST_CASE("timer_config returns correct strings", "[perf_driver]") {
  CHECK(timer_config(TimerResolution::Low) == "");
  CHECK(
    timer_config(TimerResolution::Normal) == "cyc=1,cyc_thresh=1,mtc_period=0");
  CHECK(
    timer_config(TimerResolution::High) ==
    "cyc=1,cyc_thresh=1,mtc_period=0,noretcomp=1");
}

TEST_CASE("build_perf_record_args for Intel PT", "[perf_driver]") {
  PerfConfig config;
  config.working_directory = "/tmp/trace";
  config.trace_scope = TraceScope::Userspace;
  config.timer_resolution = TimerResolution::Normal;

  PerfCapabilities caps;
  caps.has_intel_pt = true;
  caps.snapshot_on_exit = true;

  auto args = build_perf_record_args(config, caps, {"1234"});

  CHECK(args[0] == "perf");
  CHECK(args[1] == "record");
  CHECK(contains(args, "-o"));
  CHECK(contains(args, "/tmp/trace/perf.data"));
  CHECK(contains(args, "--snapshot=e"));
  CHECK(contains(args, "--per-thread"));
}

TEST_CASE("build_perf_record_args for sampling", "[perf_driver]") {
  PerfConfig config;
  config.working_directory = "/tmp/trace";
  config.sampling = true;

  PerfCapabilities caps;

  auto args = build_perf_record_args(config, caps, {"1234"});

  CHECK(contains(args, "--event=cycles/freq=10000/u"));
  CHECK(!contains(args, "--snapshot"));
  CHECK(!contains(args, "--snapshot=e"));
}

TEST_CASE("build_perf_record_args with program launch", "[perf_driver]") {
  PerfConfig config;
  config.working_directory = "/tmp/trace";

  PerfCapabilities caps;
  caps.has_intel_pt = true;
  caps.snapshot_on_exit = true;

  auto args =
    build_perf_record_args(config, caps, "./myprogram", {"arg1", "arg2"});

  CHECK(contains(args, "--"));
  CHECK(contains(args, "./myprogram"));
  CHECK(contains(args, "arg1"));
  CHECK(contains(args, "arg2"));
  // Should NOT have -t or -p (perf manages the child)
  CHECK(!contains(args, "-t"));
  CHECK(!contains(args, "-p"));
  CHECK(!contains(args, "--per-thread"));
}

TEST_CASE("build_perf_script_args for Intel PT", "[perf_driver]") {
  PerfConfig config;

  auto args = build_perf_script_args(config, "/tmp/trace");

  CHECK(args[0] == "perf");
  CHECK(args[1] == "script");
  CHECK(contains(args, "--ns"));
  CHECK(contains(args, "--itrace=bep"));
  CHECK(contains(
    args, "pid,tid,time,flags,ip,addr,sym,symoff,synth,dso,event,period"));
}

TEST_CASE("build_perf_script_args for sampling", "[perf_driver]") {
  PerfConfig config;
  config.sampling = true;

  auto args = build_perf_script_args(config, "/tmp/trace");

  CHECK(!contains(args, "--itrace=bep"));
  CHECK(contains(args, "pid,tid,time,ip,sym,symoff,dso,event,period"));
}
