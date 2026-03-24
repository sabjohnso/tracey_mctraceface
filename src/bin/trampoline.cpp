/// Minimal trampoline for tracey_mctraceface.
///
/// Waits for a byte on stdin, then replaces itself with the target
/// program via execvp. This gives the parent a known PID to attach
/// perf and stats samplers to before the target starts executing.
///
/// Usage: trampoline PROGRAM [ARGS...]
/// Parent writes any byte to trampoline's stdin to trigger exec.

#include <unistd.h>

#include <cstdlib>

auto
main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    const char* msg = "usage: trampoline PROGRAM [ARGS...]\n";
    write(STDERR_FILENO, msg, 37);
    return 1;
  }

  // Wait for the go signal (any byte on stdin)
  char buf;
  read(STDIN_FILENO, &buf, 1);

  // Replace ourselves with the target program.
  // argv[0] is "trampoline", argv[1] is the program, argv[2..] are args.
  execvp(argv[1], &argv[1]);

  // If exec fails
  _exit(127);
}
