#include "matmul_main.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// Marker functions for trace filtering (--start-symbol / --end-symbol).
// extern "C" prevents name mangling; noinline + volatile prevent elision.
extern "C" {
__attribute__((noinline)) void
start_mul() {
  volatile int marker = 1;
  (void)marker;
}

__attribute__((noinline)) void
end_mul() {
  volatile int marker = 0;
  (void)marker;
}
}

namespace matmul {

  namespace {

    using Matrix = std::vector<std::vector<double>>;

    auto
    make_random_matrix(int rows, int cols, std::mt19937& rng) -> Matrix {
      std::uniform_real_distribution<double> dist(-1.0, 1.0);
      Matrix m(rows, std::vector<double>(cols));
      for (auto& row : m) {
        std::generate(row.begin(), row.end(), [&] { return dist(rng); });
      }
      return m;
    }

    void
    multiply_rows(
      const Matrix& a,
      const Matrix& b,
      Matrix& c,
      int row_start,
      int row_end,
      int m,
      int n) {
      for (int i = row_start; i < row_end; ++i) {
        for (int j = 0; j < n; ++j) {
          double sum = 0.0;
          for (int k = 0; k < m; ++k) {
            sum += a[i][k] * b[k][j];
          }
          c[i][j] = sum;
        }
      }
    }

    void
    multiply_threaded(
      const Matrix& a,
      const Matrix& b,
      Matrix& c,
      int l,
      int m,
      int n,
      int num_threads) {
      std::vector<std::thread> threads;
      threads.reserve(num_threads);

      int rows_per_thread = l / num_threads;
      int remainder = l % num_threads;
      int row_start = 0;

      for (int t = 0; t < num_threads; ++t) {
        int row_end = row_start + rows_per_thread + (t < remainder ? 1 : 0);
        threads.emplace_back(
          multiply_rows,
          std::cref(a),
          std::cref(b),
          std::ref(c),
          row_start,
          row_end,
          m,
          n);
        row_start = row_end;
      }

      for (auto& th : threads) {
        th.join();
      }
    }

  } // namespace

  auto
  run(const nlohmann::json& config) -> int {
    auto l = config.value("l", 128);
    auto m = config.value("m", 128);
    auto n = config.value("n", 128);
    auto num_threads = config.value("threads", 4);
    auto nmul = config.value("nmul", 1);

    std::cerr << "matmul: " << l << "x" << m << " * " << m << "x" << n
              << " with " << num_threads << " threads, " << nmul
              << " iterations\n";

    std::mt19937 rng(42);

    auto total_start = std::chrono::steady_clock::now();

    for (int iter = 0; iter < nmul; ++iter) {
      auto a = make_random_matrix(l, m, rng);
      auto b = make_random_matrix(m, n, rng);
      Matrix c(l, std::vector<double>(n, 0.0));

      start_mul();
      multiply_threaded(a, b, c, l, m, n, num_threads);
      end_mul();

      // Prevent optimizer from eliding the computation
      volatile double sink = c[0][0];
      (void)sink;
    }

    auto total_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        total_end - total_start)
                        .count();

    std::cerr << "Done in " << elapsed_ms << " ms\n";
    return 0;
  }

} // namespace matmul
