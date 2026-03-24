#include <tracey_mctraceface/subprocess.hpp>

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace tracey_mctraceface {

  Subprocess::Subprocess(const std::vector<std::string>& args) {
    if (args.empty()) {
      throw std::runtime_error("Subprocess: empty argument list");
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
      throw std::runtime_error(
        std::string("Subprocess: pipe() failed: ") + std::strerror(errno));
    }

    pid_ = fork();
    if (pid_ < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      throw std::runtime_error(
        std::string("Subprocess: fork() failed: ") + std::strerror(errno));
    }

    if (pid_ == 0) {
      // Child: redirect stdout to pipe write end
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[1]);

      // Prevent perf from spawning a pager
      setenv("PAGER", "cat", 1);

      // Build argv
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
      }
      argv.push_back(nullptr);

      execvp(argv[0], argv.data());
      // If execvp returns, it failed
      _exit(127);
    }

    // Parent: read from pipe read end
    close(pipefd[1]);
    pipe_ = fdopen(pipefd[0], "r");
    if (!pipe_) {
      close(pipefd[0]);
      throw std::runtime_error("Subprocess: fdopen() failed");
    }
  }

  Subprocess::~Subprocess() {
    if (pipe_) {
      std::fclose(pipe_);
      pipe_ = nullptr;
    }
    if (pid_ > 0) {
      waitpid(pid_, nullptr, 0);
      pid_ = -1;
    }
  }

  auto
  Subprocess::read_line(std::string& line) -> bool {
    if (!pipe_) return false;

    line.clear();
    int c;
    while ((c = std::fgetc(pipe_)) != EOF) {
      if (c == '\n') return true;
      line += static_cast<char>(c);
    }
    return !line.empty();
  }

  auto
  Subprocess::wait() -> int {
    if (pipe_) {
      std::fclose(pipe_);
      pipe_ = nullptr;
    }

    if (pid_ <= 0) return -1;

    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
  }

} // namespace tracey_mctraceface
