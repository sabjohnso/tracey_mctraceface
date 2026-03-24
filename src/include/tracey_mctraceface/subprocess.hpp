#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  /**
   * @brief A subprocess with captured stdout.
   *
   * Spawns a child process via fork/execvp and provides line-by-line
   * reading of its stdout.
   */
  class Subprocess {
  public:
    /**
     * @brief Spawn a subprocess.
     * @param args Command and arguments (args[0] is the program).
     * @throws std::runtime_error on fork/pipe/exec failure.
     */
    explicit Subprocess(const std::vector<std::string>& args);

    ~Subprocess();

    Subprocess(const Subprocess&) = delete;
    auto
    operator=(const Subprocess&) -> Subprocess& = delete;

    /**
     * @brief Read the next line from stdout.
     * @return true if a line was read, false on EOF.
     */
    auto
    read_line(std::string& line) -> bool;

    /**
     * @brief Wait for the child to exit and return its status.
     * @return Exit code, or -1 on signal death.
     */
    auto
    wait() -> int;

  private:
    pid_t pid_ = -1;
    std::FILE* pipe_ = nullptr;
  };

} // namespace tracey_mctraceface
