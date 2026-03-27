# Tracey McTraceface

A C++ Intel Processor Trace recorder and decoder that produces
[Fuchsia Trace Format](https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format)
files viewable in [Perfetto](https://ui.perfetto.dev).

Records hardware-level call traces using Linux `perf` and Intel PT,
reconstructs per-thread call stacks, and outputs `.fxt` files that
display as flame charts in Perfetto's timeline UI.

## Quick Start

```sh
# Trace a program (full execution)
tracey_mctraceface run --full-execution -o trace.fxt -- ./your_program args

# Open in Perfetto automatically
tracey_mctraceface run --full-execution --serve -- ./your_program args

# Trace only a specific region
tracey_mctraceface run --full-execution \
  --start-symbol my_function --end-symbol cleanup \
  -o trace.fxt -- ./your_program
```

## Requirements

### Build Requirements

- **C++20 compiler** (Clang 15+ or GCC 12+)
- **CMake** 3.12+
- **Ninja** or **Make**
- **zlib** (`zlib-dev` or `zlib1g-dev`)
- **libzstd** (`libzstd-dev`)
- **pkg-config**

The following are fetched automatically via CMake FetchContent:

- [xb](https://github.com/sabjohnso/xb) — XML databinding / BES code generation
- [json-commander](https://github.com/JSON-Commander/json-commander) — CLI from JSON schemas
- [Catch2](https://github.com/catchorg/Catch2) — test framework
- [nlohmann/json](https://github.com/nlohmann/json)
- [cmake_utilities](https://github.com/sabjohnso/cmake_utilities) — CMake helpers (git submodule)

### Runtime Requirements

- **Linux** with `perf` installed (`linux-tools-$(uname -r)` or `perf`)
- **Intel Processor Trace** support (Intel Broadwell or newer)
  - Check: `ls /sys/bus/event_source/devices/intel_pt`
  - Falls back to sampling mode if Intel PT is unavailable
- **perf_event_paranoid** setting must allow recording:
  ```sh
  # Check current setting
  cat /proc/sys/kernel/perf_event_paranoid
  # Set to allow non-root recording (requires root)
  sudo sysctl kernel.perf_event_paranoid=1
  ```

## Building

```sh
# Configure
cmake --preset default

# Build
cmake --build --preset default

# Run tests
ctest --preset default
```

For a specific build type:

```sh
cmake --preset default -DCMAKE_BUILD_TYPE=Release
cmake --build --preset default
```

The build produces:

- `build/src/bin/tracey_mctraceface` — the main executable
- `build/test/sample-targets/matmul/matmul` — sample benchmark program

## Usage

### Commands

#### `run` — Trace a program from launch to exit

```sh
# Basic: trace full execution, save as FXT
tracey_mctraceface run --full-execution -o trace.fxt -- ./program args

# Compressed output (determined by file extension)
tracey_mctraceface run --full-execution -o trace.fxt.gz -- ./program
tracey_mctraceface run --full-execution -o trace.fxt.zst -- ./program

# Open in Perfetto UI after recording
tracey_mctraceface run --full-execution --serve -- ./program

# Record now, decode later (saves perf.data, skips decode)
tracey_mctraceface run --full-execution --no-decode -- ./program
# → Saved to: ./perf.data
tracey_mctraceface decode -d ./perf.data -o trace.fxt
```

#### `attach` — Trace a running process

```sh
# Attach to a process by PID, Ctrl+C to stop
tracey_mctraceface attach -p 12345 -o trace.fxt

# Attach with full execution recording
tracey_mctraceface attach -p 12345 --full-execution -o trace.fxt
```

#### `decode` — Convert existing perf.data to FXT

```sh
# Decode a previously saved perf.data
tracey_mctraceface decode -d ./perf.data -o trace.fxt

# Decode with filtering
tracey_mctraceface decode -d ./perf.data \
  --start-symbol main --end-symbol cleanup \
  -o filtered.fxt

# Decode and serve
tracey_mctraceface decode -d ./perf.data --serve
```

### Trace Filtering

Extract only the region of interest from a trace:

```sh
# Record only between start_mul and end_mul symbols
tracey_mctraceface run --full-execution \
  --start-symbol start_mul --end-symbol end_mul \
  -o slice.fxt -- ./matmul

# Collect multiple slices for comparison
tracey_mctraceface run --full-execution \
  --start-symbol begin_work --end-symbol end_work \
  --multi-slice -o slices.fxt -- ./program
```

To use trace filtering, add unmangled marker functions to your code:

```cpp
extern "C" {
__attribute__((noinline)) void start_mul() {
  volatile int marker = 1;
  (void)marker;
}
__attribute__((noinline)) void end_mul() {
  volatile int marker = 0;
  (void)marker;
}
}
```

### Viewing Traces

**Option 1: `--serve` flag** (recommended)

```sh
tracey_mctraceface run --full-execution --serve -- ./program
```

Opens a local page in your browser that sends the trace to
[Perfetto UI](https://ui.perfetto.dev) via `postMessage`. Click
"Open in Perfetto" to load the trace.

**Option 2: Manual upload**

Open [ui.perfetto.dev](https://ui.perfetto.dev) and drag-and-drop
the `.fxt` file.

**Option 3: Serve an existing trace**

```sh
tracey_mctraceface decode -d ./perf.data --serve --serve-port 9090
```

### Offline Processing

For expensive hardware (replay labs, production systems), record
without decoding and process later on a development machine:

```sh
# On production hardware: record only
tracey_mctraceface run --full-execution --no-decode -o recording.data \
  -- ./program

# Copy to dev machine
scp prod:recording.data .

# On dev machine: decode
tracey_mctraceface decode -d recording.data -o trace.fxt.gz --serve
```

### Diagnostic Flags

```sh
# Verbose: show perf command lines for debugging
tracey_mctraceface run --verbose --full-execution -- ./program

# Quiet: suppress status messages (for scripting)
tracey_mctraceface run --quiet --full-execution -o trace.fxt -- ./program
```

## Output Formats

The output format is determined by the file extension:

| Extension  | Format                  | Size    |
|------------|-------------------------|---------|
| `.fxt`     | Uncompressed FXT        | Largest |
| `.fxt.gz`  | gzip-compressed FXT     | ~5-10x smaller |
| `.fxt.zst` | zstd-compressed FXT     | ~5-10x smaller, faster |

All formats are loadable by Perfetto.

## Architecture

```
perf record ──→ perf script ──→ PerfScriptParser ──→ TraceFilter
                                                        │
                                                        ▼
                                            StackReconstructor
                                                        │
                                                        ▼
                                                   FxtWriter ──→ OutputSink
                                                                  ├─ FileSink
                                                                  ├─ GzipSink
                                                                  └─ ZstdSink
```

- **PerfScriptParser**: Hand-written parser for `perf script` text output.
  Handles all Intel PT branch types, trace state changes, CBR events,
  decode errors, and sampled callstacks.
- **TraceFilter**: State machine that passes only events between
  configurable start/end symbol markers.
- **StackReconstructor**: Maintains per-thread call stacks from branch
  events. Handles call/return, syscall/interrupt context switches, trace
  gaps, and stack reconciliation from sampling data.
- **FxtWriter**: Encodes Fuchsia Trace Format binary records using
  BES-generated wire types. Manages string interning (15-bit IDs with
  LRU eviction) and thread slot allocation.

## Testing

```sh
# Run all tests
ctest --preset default

# Run a specific test suite
./build/test/unit/perf_script_parser_test
./build/test/unit/trace_validation_test
./build/test/feature/cli_acceptance_test
```

15 test suites covering:

- FXT binary record encoding (BES-generated type round-trips)
- Perf script parsing (all event types and edge cases)
- Stack reconstruction (call/return, syscall, trace gaps)
- Symbol shortening (templates, parameters, operators)
- Structural trace validation (first-principles invariants)
- CLI acceptance tests (end-to-end command verification)
- Error path tests (I/O failures, process errors)
- Compressed output (gzip/zstd magic bytes and ratios)

## License

See LICENSE file.
