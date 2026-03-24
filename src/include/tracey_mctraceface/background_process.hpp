#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief A background process that can be signaled and waited on.
   *
   * Unlike Subprocess, does not capture stdout — the child inherits
   * the parent's stdout/stderr. Used for perf record and target programs.
   */
  class BackgroundProcess {
  public:
    /**
     * @brief Spawn a background process.
     * @param args Command and arguments (args[0] is the program).
     * @param stopped If true, child raises SIGSTOP before exec.
     * @throws std::runtime_error on fork failure.
     */
    explicit BackgroundProcess(
      const std::vector<std::string>& args, bool stopped = false);

    ~BackgroundProcess();

    BackgroundProcess(const BackgroundProcess&) = delete;
    auto
    operator=(const BackgroundProcess&) -> BackgroundProcess& = delete;

    /** @brief Get the child PID. */
    auto
    pid() const -> pid_t;

    /** @brief Send a signal to the child. */
    void
    send_signal(int sig) const;

    /**
     * @brief Wait for the child to exit.
     * @return Exit code, or -1 on signal death.
     */
    auto
    wait() -> int;

    /**
     * @brief Check if the child has exited without blocking.
     * @return Exit code if exited, nullopt if still running.
     */
    auto
    try_wait() -> std::optional<int>;

  private:
    pid_t pid_ = -1;
  };

} // namespace tracey_mctraceface
