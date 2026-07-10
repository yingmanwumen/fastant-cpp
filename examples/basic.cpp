/// @file
/// Basic Usage of fastant::static_clock::Instant.
///
/// Demonstrates the core API: now(), elapsed(), duration arithmetic,
/// comparison operators, and the ZERO constant.

#include <chrono>
#include <iostream>
#include <thread>

#include "fastant/fastant.hpp"

int main() {
  // Capture the current instant.
  auto start = fastant::static_clock::Instant::now();

  // Simulate some work.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Measure elapsed time.
  auto elapsed = start.elapsed();
  std::cout
      << "Elapsed: " << elapsed.count() << " ns ("
      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
      << " ms)" << std::endl;

  // Compare two instants.
  auto t1 = fastant::static_clock::Instant::now();
  auto t2 = fastant::static_clock::Instant::now();
  if (t2 > t1) {
    std::cout << "t2 > t1: monotonic clock works correctly." << std::endl;
  }

  // Duration arithmetic.
  auto later = t1 + std::chrono::milliseconds(500);
  auto diff = later - t1;
  std::cout << "t1 + 500ms - t1 = " << diff.count() << " ns" << std::endl;

  // ZERO constant.
  std::cout
      << "ZERO duration_since ZERO = "
      << fastant::static_clock::Instant::ZERO.duration_since(fastant::static_clock::Instant::ZERO).count()
      << " ns" << std::endl;

  // Check if TSC is available.
  std::cout << "TSC available: " << (fastant::is_tsc_available() ? "yes" : "no")
            << std::endl;

  auto start_chrono = std::chrono::steady_clock::now();
  auto start_fastant = fastant::static_clock::Instant::now();

  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto end_chrono = std::chrono::steady_clock::now();
  auto end_fastant = fastant::static_clock::Instant::now();

  auto duration_chrono = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_chrono - start_chrono);
  auto duration_fastant = end_fastant - start_fastant;

  std::cout << "Slept for 1s, " << duration_chrono.count()
            << " ns measured by std::chrono::steady_clock, "
            << duration_fastant.count() << " ns measured by fastant, diff: "
            << duration_fastant.count() - duration_chrono.count() << " ns."
            << std::endl;

  // ── Online RDTSC backend (JaneStreet-style, ~7 ns) ──
  std::cout << "\n--- Online backend ---\n";
  auto online_start = fastant::online::Instant::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  auto online_elapsed = online_start.elapsed();
  std::cout << "Online elapsed: " << online_elapsed.count() << " ns\n";

  // online::Anchor + as_unix_nanos
  auto online_anchor = fastant::online::Anchor::new_anchor();
  auto online_now = fastant::online::Instant::now();
  auto online_unix = online_now.as_unix_nanos(online_anchor);
  std::cout << "Online unix timestamp: " << online_unix << " ns\n";

  // online::nanos_per_cycle (dynamic EWMA)
  std::cout << "Online nanos_per_cycle: " << fastant::detail::online::nanos_per_cycle()
            << "\n";

  return 0;
}
