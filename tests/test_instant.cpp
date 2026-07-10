#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <thread>

#include "fastant/fastant.hpp"

// =========================================================================
// static backend tests
// =========================================================================

TEST_CASE("is_tsc_available") {
  auto _ = fastant::is_tsc_available();
  REQUIRE(true);  ///< just ensure no crash / link error
}

TEST_CASE("monotonic") {
  uint64_t prev = 0;
  for (int i = 0; i < 10000; ++i) {
    uint64_t cur = fastant::detail::current_cycle();
    REQUIRE(cur >= prev);
    prev = cur;
  }
}

TEST_CASE("nanos_per_cycle") {
  auto _ = fastant::detail::nanos_per_cycle();
  REQUIRE(true);
}

TEST_CASE("unix_time") {
  auto now = fastant::static_clock::Instant::now();
  auto anchor = fastant::static_clock::Anchor::new_anchor();
  auto unix_nanos = now.as_unix_nanos(anchor);
  REQUIRE(unix_nanos > 0);
}

TEST_CASE("duration") {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(100, 500);
  constexpr int64_t expect_max_delta_ns = 50'000;

  for (int i = 0; i < 10; ++i) {
    int rand_ms = dist(gen);
    auto fastant_instant = fastant::static_clock::Instant::now();
    auto std_start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_ms));

    auto check = [fastant_instant, std_start]() {
      auto duration_ns_fastant = fastant_instant.elapsed();
      auto duration_ns_std = std::chrono::steady_clock::now() - std_start;
      int64_t real_delta = std::abs(
          std::chrono::duration_cast<std::chrono::nanoseconds>(duration_ns_std)
              .count() -
          duration_ns_fastant.count());
      REQUIRE(real_delta < expect_max_delta_ns);
    };
    check();
    std::thread t(check);
    t.join();
  }
}

// =========================================================================
// online backend tests
// =========================================================================

TEST_CASE("online_is_tsc_available") {
  auto _ = fastant::detail::online::is_tsc_available();
  REQUIRE(true);
}

TEST_CASE("online_monotonic") {
  uint64_t prev = 0;
  for (int i = 0; i < 10000; ++i) {
    uint64_t cur = fastant::detail::online::current_cycle();
    REQUIRE(cur >= prev);
    prev = cur;
  }
}

TEST_CASE("online_nanos_per_cycle") {
  auto _ = fastant::online::Instant::now();
  auto npc = fastant::detail::online::nanos_per_cycle();
  REQUIRE(npc > 0.0);
}

TEST_CASE("online_unix_time") {
  auto now = fastant::online::Instant::now();
  auto anchor = fastant::online::Anchor::new_anchor();
  auto unix_nanos = now.as_unix_nanos(anchor);
  REQUIRE(unix_nanos > 0);
}

TEST_CASE("online_duration") {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(100, 500);
  constexpr int64_t expect_max_delta_ns = 5'000'000;

  for (int i = 0; i < 10; ++i) {
    int rand_ms = dist(gen);
    auto fastant_instant = fastant::online::Instant::now();
    auto std_start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_ms));
    auto duration_ns_fastant = fastant_instant.elapsed();
    auto duration_ns_std = std::chrono::steady_clock::now() - std_start;
    int64_t real_delta = std::abs(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration_ns_std)
            .count() -
        duration_ns_fastant.count());
    REQUIRE(real_delta < expect_max_delta_ns);
  }
}

TEST_CASE("online_concurrent") {
  // Verify online backend works under concurrency — multiple threads
  // calling Instant::now() concurrently while auto-calibration fires.
  std::atomic<bool> start{false};
  std::atomic<int> errors{0};

  auto worker = [&]() {
    while (!start.load()) /* spin */
      ;
    auto prev = fastant::online::Instant::now();
    for (int i = 0; i < 5000; ++i) {
      auto cur = fastant::online::Instant::now();
      if (cur < prev) errors.fetch_add(1);
      prev = cur;
    }
  };

  std::thread t1(worker), t2(worker), t3(worker), t4(worker);
  start.store(true);
  t1.join();
  t2.join();
  t3.join();
  t4.join();
  REQUIRE(errors.load() == 0);
}

TEST_CASE("online_recalibrate") {
  // Smoke test: recalibrate() should not crash and should leave
  // nanos_per_cycle >= 0.
  auto _ = fastant::online::Instant::now();
  fastant::detail::online::recalibrate();
  REQUIRE(fastant::detail::online::nanos_per_cycle() > 0.0);
}

// =========================================================================
// AtomicInstant tests
// =========================================================================

TEST_CASE("atomic_load_store") {
  auto t1 = fastant::static_clock::Instant::now();
  fastant::static_clock::AtomicInstant ai(t1);
  REQUIRE(ai.load() == t1);
  auto t2 = fastant::static_clock::Instant::now();
  ai.store(t2);
  REQUIRE(ai.load() == t2);
}

TEST_CASE("atomic_swap") {
  auto t1 = fastant::static_clock::Instant::now();
  fastant::static_clock::AtomicInstant ai(t1);
  auto t2 = t1 + std::chrono::milliseconds(1);
  auto old = ai.swap(t2);
  REQUIRE(old == t1);
  REQUIRE(ai.load() == t2);
}

TEST_CASE("atomic_fetch_max") {
  auto t1 = fastant::static_clock::Instant::now();
  fastant::static_clock::AtomicInstant ai(t1);
  auto t2 = t1 + std::chrono::milliseconds(1);
  auto old = ai.fetch_max(t2);
  REQUIRE(old == t1);
  REQUIRE(ai.load() == t2);
  // fetch_max with smaller value should not change
  auto t3 = t1;
  auto old2 = ai.fetch_max(t3);
  REQUIRE(old2 == t2);
  REQUIRE(ai.load() == t2);
}

TEST_CASE("atomic_fetch_min") {
  auto t2 = fastant::static_clock::Instant::now();
  auto t1 = t2 - std::chrono::milliseconds(1);
  fastant::static_clock::AtomicInstant ai(t2);
  auto old = ai.fetch_min(t1);
  REQUIRE(old == t2);
  REQUIRE(ai.load() == t1);
  // fetch_min with larger value should not change
  auto old2 = ai.fetch_min(t2);
  REQUIRE(old2 == t1);
  REQUIRE(ai.load() == t1);
}

TEST_CASE("atomic_into_instant") {
  auto t1 = fastant::static_clock::Instant::now();
  fastant::static_clock::AtomicInstant ai(t1);
  auto t2 = std::move(ai).into_instant();
  REQUIRE(t2 == t1);
}

TEST_CASE("online_atomic") {
  // Online-backend AtomicInstant: basic store/load round-trip
  auto t1 = fastant::online::Instant::now();
  fastant::online::AtomicInstant ai(t1);
  REQUIRE(ai.load() == t1);
  auto t2 = fastant::online::Instant::now();
  ai.store(t2);
  REQUIRE(ai.load() == t2);
}
