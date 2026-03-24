#include <tracey_mctraceface/background_process.hpp>

#include <catch2/catch_test_macros.hpp>

#include <signal.h>

using namespace tracey_mctraceface;

TEST_CASE("BackgroundProcess spawns and waits", "[background_process]") {
  BackgroundProcess proc({"true"});
  CHECK(proc.pid() > 0);
  CHECK(proc.wait() == 0);
}

TEST_CASE("BackgroundProcess reports nonzero exit", "[background_process]") {
  BackgroundProcess proc({"false"});
  CHECK(proc.wait() == 1);
}

TEST_CASE("BackgroundProcess send_signal", "[background_process]") {
  BackgroundProcess proc({"sleep", "10"});
  CHECK(proc.pid() > 0);
  proc.send_signal(SIGKILL);
  auto code = proc.wait();
  CHECK(code < 0); // killed by signal
}

TEST_CASE("BackgroundProcess stopped mode", "[background_process]") {
  BackgroundProcess proc({"echo", "hello"}, true);
  CHECK(proc.pid() > 0);
  // Child is stopped — resume it
  proc.send_signal(SIGCONT);
  CHECK(proc.wait() == 0);
}

TEST_CASE("BackgroundProcess try_wait on running", "[background_process]") {
  BackgroundProcess proc({"sleep", "10"});
  auto result = proc.try_wait();
  CHECK(!result.has_value()); // still running
  proc.send_signal(SIGKILL);
  proc.wait();
}
