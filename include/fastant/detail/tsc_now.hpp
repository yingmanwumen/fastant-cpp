#pragma once

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))

#include <cpuid.h>
#include <x86intrin.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace fastant::detail::tsc_now {

struct TscLevel {
  enum class Kind { Stable, Unstable };
  Kind kind = Kind::Unstable;
  uint64_t cycles_per_second = 0;
  uint64_t cycles_from_anchor = 0;

  static TscLevel get();
};

inline std::atomic<bool> g_tsc_available{false};
inline TscLevel g_tsc_level{};
inline double g_nanos_per_cycle = 1.0;
inline std::atomic<bool> g_initialized{false};

inline bool has_invariant_tsc() {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) == 0) {
    return false;
  }
  if (eax < 0x80000007) {
    return false;
  }
  __get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
  return (edx & (1u << 8)) != 0;
}

inline bool clock_source_has_tsc() {
  {
    std::ifstream file(
        "/sys/devices/system/clocksource/clocksource0/current_clocksource");
    if (file.is_open()) {
      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      if (content.find("tsc") != std::string::npos) {
        return true;
      }
      return false;
    }
    if (errno != ENOENT) {
      return false;
    }
  }
  {
    std::ifstream avail(
        "/sys/devices/system/clocksource/clocksource0/available_clocksource");
    if (avail.is_open()) {
      std::string content((std::istreambuf_iterator<char>(avail)),
                          std::istreambuf_iterator<char>());
      return content.find("tsc") != std::string::npos;
    }
  }
  return false;
}

inline bool is_tsc_stable() {
  return has_invariant_tsc() || clock_source_has_tsc();
}

inline std::pair<std::chrono::steady_clock::time_point, uint64_t>
monotonic_with_tsc() {
  auto t = std::chrono::steady_clock::now();
#ifdef __SSE2__
  _mm_lfence();
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  return {t, __rdtsc()};
}

inline std::tuple<uint64_t, std::chrono::steady_clock::time_point, uint64_t>
_cycles_per_sec() {
  double old_cps = 0.0;
  std::chrono::steady_clock::time_point last_monotonic;
  uint64_t last_tsc = 0;
  double cps = 0.0;

  for (;;) {
    auto [t1, tsc1] = monotonic_with_tsc();
    bool inner_retry = false;
    for (;;) {
      auto [t2, tsc2] = monotonic_with_tsc();
      last_monotonic = t2;
      last_tsc = tsc2;
      auto elapsed_nanos =
          std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
      if (elapsed_nanos > 10'000'000) {
        if (tsc2 < tsc1) {
          inner_retry = true;
          break;
        }
        cps = static_cast<double>(tsc2 - tsc1) * 1'000'000'000.0 /
              static_cast<double>(elapsed_nanos);
        break;
      }
    }
    if (inner_retry) {
      continue;
    }
    if (std::fabs(cps - old_cps) / cps < 0.00001) {
      break;
    }
    old_cps = cps;
  }

  return {static_cast<uint64_t>(std::round(cps)), last_monotonic, last_tsc};
}

inline std::pair<uint64_t, uint64_t> cycles_per_sec(
    std::chrono::steady_clock::time_point anchor) {
  auto [cps, last_monotonic, last_tsc] = _cycles_per_sec();
  auto nanos_from_anchor = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               last_monotonic - anchor)
                               .count();
  double cycles_flied = static_cast<double>(cps) *
                        static_cast<double>(nanos_from_anchor) /
                        1'000'000'000.0;
  uint64_t cycles_from_anchor =
      last_tsc - static_cast<uint64_t>(std::ceil(cycles_flied));
  return {cps, cycles_from_anchor};
}

inline TscLevel TscLevel::get() {
  if (!is_tsc_stable()) {
    return TscLevel{};
  }
  auto anchor = std::chrono::steady_clock::now();
  auto [cps, cfa] = cycles_per_sec(anchor);
  return TscLevel{TscLevel::Kind::Stable, cps, cfa};
}

inline bool is_tsc_available() {
  return g_tsc_available.load(std::memory_order_acquire);
}

inline double nanos_per_cycle() { return g_nanos_per_cycle; }

inline uint64_t current_cycle() {
  if (g_tsc_level.kind == TscLevel::Kind::Stable) [[likely]] {
    return __rdtsc() - g_tsc_level.cycles_from_anchor;
  }
  std::abort();
}

namespace {
__attribute__((constructor(101))) void init_tsc() {
  bool expected = false;
  if (!g_initialized.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
    return;
  }
  auto level = TscLevel::get();
  bool available = (level.kind == TscLevel::Kind::Stable);
  if (available) {
    g_nanos_per_cycle =
        1'000'000'000.0 / static_cast<double>(level.cycles_per_second);
  }
  g_tsc_level = level;
  std::atomic_thread_fence(std::memory_order_seq_cst);
  g_tsc_available.store(available, std::memory_order_release);
}
}  // namespace
}  // namespace fastant::detail::tsc_now

#endif
