#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <thread>

#include "fastant/fastant.hpp"

// Test 1: is_tsc_available – smoke test
TEST_CASE("is_tsc_available") {
  auto _ = fastant::is_tsc_available();
  REQUIRE(true);  ///< just ensure no crash / link error
}

// Test 2: monotonic – current_cycle() must be non-decreasing over 10k samples
TEST_CASE("monotonic") {
  uint64_t prev = 0;
  for (int i = 0; i < 10000; ++i) {
    uint64_t cur = fastant::detail::current_cycle();
    REQUIRE(cur >= prev);
    prev = cur;
  }
}

// Test 3: nanos_per_cycle – smoke test
TEST_CASE("nanos_per_cycle") {
  auto _ = fastant::detail::nanos_per_cycle();
  REQUIRE(true);  ///< just ensure no crash / link error
}

// Test 4: unix_time – Instant::now() anchored to system_clock yields > 0
TEST_CASE("unix_time") {
  auto now = fastant::Instant::now();
  auto anchor = fastant::Anchor::new_anchor();
  auto unix_nanos = now.as_unix_nanos(anchor);
  REQUIRE(unix_nanos > 0);
}

// Test 5: duration – compare fastant::Instant::elapsed() against
//   std::chrono::steady_clock over random sleeps (100–500 ms).
//   Tolerance: 5 ms (5 000 000 ns) on Linux.
TEST_CASE("duration") {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(100, 500);

  constexpr int64_t expect_max_delta_ns = 5'000'000;  ///< 5 ms

  for (int i = 0; i < 10; ++i) {
    int rand_ms = dist(gen);

    auto fastant_instant = fastant::Instant::now();
    auto std_start = std::chrono::steady_clock::now();

    std::this_thread::sleep_for(std::chrono::milliseconds(rand_ms));

    auto check = [fastant_instant, std_start]() {
      auto duration_ns_fastant = fastant_instant.elapsed();  ///< nanoseconds
      auto duration_ns_std = std::chrono::steady_clock::now() - std_start;

      int64_t fastant_ns = duration_ns_fastant.count();
      int64_t std_ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(duration_ns_std)
              .count();

      int64_t real_delta = std::abs(std_ns - fastant_ns);
      REQUIRE(real_delta < expect_max_delta_ns);
    };

    /// Run on main thread
    check();

    /// Run on a spawned thread
    std::thread t(check);
    t.join();
  }
}
