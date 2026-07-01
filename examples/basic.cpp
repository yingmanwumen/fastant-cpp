/// @file
/// Basic Usage of fastant::Instant.
///
/// Demonstrates the core API: now(), elapsed(), duration arithmetic,
/// comparison operators, and the ZERO constant.

#include <chrono>
#include <iostream>
#include <thread>

#include "fastant/fastant.hpp"

int main() {
  // Capture the current instant.
  auto start = fastant::Instant::now();

  // Simulate some work.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Measure elapsed time.
  auto elapsed = start.elapsed();
  std::cout
      << "Elapsed: " << elapsed.count() << " ns ("
      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
      << " ms)" << std::endl;

  // Compare two instants.
  auto t1 = fastant::Instant::now();
  auto t2 = fastant::Instant::now();
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
      << fastant::Instant::ZERO.duration_since(fastant::Instant::ZERO).count()
      << " ns" << std::endl;

  // Check if TSC is available.
  std::cout << "TSC available: " << (fastant::is_tsc_available() ? "yes" : "no")
            << std::endl;

  auto start_chrono = std::chrono::steady_clock::now();
  auto start_fastant = fastant::Instant::now();

  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto end_chrono = std::chrono::steady_clock::now();
  auto end_fastant = fastant::Instant::now();

  auto duration_chrono = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_chrono - start_chrono);
  auto duration_fastant = end_fastant - start_fastant;

  std::cout << "Slept for 1s, " << duration_chrono.count()
            << " ns measured by std::chrono::steady_clock, "
            << duration_fastant.count() << " ns measured by fastant, diff: "
            << duration_fastant.count() - duration_chrono.count() << " ns."
            << std::endl;

  return 0;
}
