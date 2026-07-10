/// @file
/// Long-term drift test: compares both backends against std steady_clock.
///
/// Runs indefinitely, printing per-second drift for both static and Online
/// backends side by side.  Press Ctrl+C to stop.

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

struct Backend {
  uint64_t elapsed_ns;
  int64_t drift_ns;
  double drift_ppm;
};

template <typename InstantType>
Backend measure(InstantType start,
                std::chrono::steady_clock::time_point start_std) {
  using namespace std::chrono;
  auto now_inst = InstantType::now();
  auto now_std = steady_clock::now();

  auto elapsed = now_inst - start;
  auto elapsed_ns = duration_cast<nanoseconds>(elapsed).count();

  auto elapsed_std_ns = duration_cast<nanoseconds>(now_std - start_std).count();
  auto drift =
      static_cast<int64_t>(elapsed_ns) - static_cast<int64_t>(elapsed_std_ns);
  double ppm =
      elapsed_std_ns > 0
          ? (static_cast<double>(drift) / static_cast<double>(elapsed_std_ns)) *
                1'000'000.0
          : 0.0;

  return {static_cast<uint64_t>(elapsed_ns), drift, ppm};
}

}  // namespace

int main() {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  auto start_std = std::chrono::steady_clock::now();
  auto start_static = fastant::static_clock::Instant::now();
  auto start_online = fastant::online::Instant::now();

  std::cout << "Long-term drift test started. Press Ctrl+C to stop.\n"
            << "TSC available: " << std::boolalpha
            << fastant::is_tsc_available() << "\n\n";

  // header
  std::cout << std::setw(8) << "sec" << std::setw(16) << "static(ns)"
            << std::setw(16) << "online(ns)" << std::setw(12) << "std(ns)"
            << std::setw(12) << "st_drift" << std::setw(12) << "st_ppm"
            << std::setw(12) << "on_drift" << std::setw(12) << "on_ppm"
            << "\n"
            << std::string(8 + 16 + 16 + 12 + 12 + 12 + 12 + 12, '-') << "\n";

  uint64_t second = 0;
  while (running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!running.load()) break;
    ++second;

    auto st = measure<fastant::static_clock::Instant>(start_static, start_std);
    auto on = measure<fastant::online::Instant>(start_online, start_std);

    auto now_std = std::chrono::steady_clock::now();
    auto elapsed_std_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now_std -
                                                             start_std)
            .count());

    std::cout << std::setw(8) << second << std::setw(16) << st.elapsed_ns
              << std::setw(16) << on.elapsed_ns << std::setw(12)
              << elapsed_std_ns << std::setw(12) << st.drift_ns << std::setw(12)
              << std::fixed << std::setprecision(6) << st.drift_ppm
              << std::setw(12) << on.drift_ns << std::setw(12) << std::fixed
              << std::setprecision(6) << on.drift_ppm << "\n";
  }

  std::cout << "\nStopped after " << std::setprecision(24) << second
            << " seconds.\n"
            << "static nanos_per_cycle: " << fastant::detail::nanos_per_cycle()
            << "\n"
            << "online   nanos_per_cycle: "
            << fastant::detail::online::nanos_per_cycle() << "\n";
  return 0;
}
