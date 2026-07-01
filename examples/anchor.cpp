/// @file
/// @brief Using Anchor to convert Instant to Unix timestamp.
///
/// Demonstrates how to pair an Anchor with Instant::as_unix_nanos()
/// to obtain wall-clock timestamps.

#include <iostream>
#include <thread>

#include "fastant/fastant.hpp"

int main() {
  // Create an anchor — records both wall-clock time and cycle counter.
  auto anchor = fastant::Anchor::new_anchor();

  // Convert the anchor to a Unix nanosecond timestamp using itself as
  // reference.
  auto anchor_now = fastant::Instant::now().as_unix_nanos(anchor);
  std::cout << "Anchor created at Unix nanos: " << anchor_now << std::endl;

  // Wait a bit, then capture another instant.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto now = fastant::Instant::now();

  // Convert the cycle-based instant to a Unix nanosecond timestamp.
  auto unix_nanos = now.as_unix_nanos(anchor);
  std::cout << "Now (via anchor): " << unix_nanos << " ns since epoch"
            << std::endl;

  // Verify the timestamp is reasonable (should be > anchor).
  if (unix_nanos > anchor_now) {
    std::cout << "Timestamp is ahead of anchor by " << (unix_nanos - anchor_now)
              << " ns" << std::endl;
  }

  // Reuse the anchor for multiple conversions.
  auto t1 = fastant::Instant::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto t2 = fastant::Instant::now();

  std::cout << "t1 = " << t1.as_unix_nanos(anchor) << std::endl;
  std::cout << "t2 = " << t2.as_unix_nanos(anchor) << std::endl;
  std::cout << "Delta = "
            << (t2.as_unix_nanos(anchor) - t1.as_unix_nanos(anchor)) << " ns"
            << std::endl;

  return 0;
}
