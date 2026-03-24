#include <tracey_mctraceface/background_process.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  BackgroundProcess::BackgroundProcess(
    const std::vector<std::string>& args, bool stopped) {
    if (args.empty()) {
      throw std::runtime_error("BackgroundProcess: empty argument list");
    }

    pid_ = fork();
    if (pid_ < 0) {
      throw std::runtime_error(
        std::string("BackgroundProcess: fork() failed: ") +
        std::strerror(errno));
    }

    if (pid_ == 0) {
      // Child: new process group so signals don't propagate to parent
      setpgid(0, 0);

      // Build argv BEFORE stopping — so when resumed, execvp is
      // immediate and perf doesn't record our setup code.
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
      }
      argv.push_back(nullptr);

      // If requested, stop before exec so parent can set up perf
      if (stopped) { raise(SIGSTOP); }

      execvp(argv[0], argv.data());
      _exit(127);
    }

    // Parent: also set child's process group (covers race with child)
    setpgid(pid_, pid_);

    // Parent: if stopped, wait for child to actually stop
    if (stopped) {
      int status = 0;
      waitpid(pid_, &status, WUNTRACED);
    }
  }

  BackgroundProcess::~BackgroundProcess() {
    if (pid_ > 0) {
      // Try to reap if not already waited
      waitpid(pid_, nullptr, WNOHANG);
    }
  }

  auto
  BackgroundProcess::pid() const -> pid_t {
    return pid_;
  }

  void
  BackgroundProcess::send_signal(int sig) const {
    if (pid_ > 0) { kill(pid_, sig); }
  }

  auto
  BackgroundProcess::wait() -> int {
    if (pid_ <= 0) return -1;

    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
  }

  auto
  BackgroundProcess::try_wait() -> std::optional<int> {
    if (pid_ <= 0) return -1;

    int status = 0;
    auto result = waitpid(pid_, &status, WNOHANG);
    if (result <= 0) return std::nullopt;

    pid_ = -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
  }

} // namespace tracey_mctraceface
