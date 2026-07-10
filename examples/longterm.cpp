/// @file
/// Long-term drift test: continuously compares fastant and std steady_clock.
///
/// Runs indefinitely, printing the drift between the two clocks every second.
/// Press Ctrl+C to stop. Useful for validating TSC stability over hours/days.

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>

#include "fastant/fastant.hpp"

namespace {

std::atomic<bool> running{true};

void handle_signal(int /* sig */) { running.store(false); }

}  // namespace

int main() {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  auto start_fastant = fastant::Instant::now();
  auto start_std = std::chrono::steady_clock::now();

  std::cout << "Long-term drift test started. Press Ctrl+C to stop.\n";
  std::cout << "fastant TSC available: " << std::boolalpha
            << fastant::is_tsc_available() << "\n";
  std::cout << "-----------------------------------------------------------------\n";

  uint64_t second = 0;

  while (running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!running.load()) break;

    ++second;

    auto now_fastant = fastant::Instant::now();
    auto now_std = std::chrono::steady_clock::now();

    auto elapsed_fastant = now_fastant - start_fastant;
    auto elapsed_std_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now_std - start_std)
            .count();

    auto elapsed_fastant_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed_fastant)
            .count();

    auto drift_ns = static_cast<int64_t>(elapsed_fastant_ns) -
                    static_cast<int64_t>(elapsed_std_ns);
    double drift_ppm =
        (static_cast<double>(drift_ns) /
         static_cast<double>(elapsed_std_ns)) *
        1'000'000.0;

    std::cout << "[" << std::setw(8) << second << " s] "
              << "fastant: " << std::setw(14) << elapsed_fastant_ns << " ns  "
              << "std: " << std::setw(14) << elapsed_std_ns << " ns  "
              << "drift: " << std::setw(10) << drift_ns << " ns  "
              << "ppm: " << std::setw(12) << std::fixed
              << std::setprecision(6) << drift_ppm << "\n";
  }

  std::cout << "\nStopped after " << second << " seconds.\n";
  return 0;
}
