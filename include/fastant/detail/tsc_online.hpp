#pragma once

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))

#include <x86intrin.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <utility>

#include "tsc_common.hpp"

namespace fastant::detail::online {

class EwmaCalibrator {
 public:
  void reset(uint64_t tsc, double t) noexcept {
    m_ewma_time_tsc = 0.0;
    m_ewma_tsc_square = 0.0;
    m_ewma_tsc = 0.0;
    m_ewma_time = 0.0;
    m_last_tsc = tsc;
    m_last_time = t;
    m_sec_per_cycle = 0.0;
  }

  bool calibrate(uint64_t tsc, double t, bool initial) noexcept {
    double td = t - m_last_time;
    if (td <= 0.0 || tsc <= m_last_tsc) return false;

    double alpha;
    if (initial)
      alpha = 1.0;
    else
      alpha = std::max(0.0, 1.0 - std::exp(-0.5 * td));

    double tsd = static_cast<double>(tsc - m_last_tsc);

    m_ewma_time_tsc = ewma(alpha, m_ewma_time_tsc, tsd * td);
    m_ewma_tsc_square = ewma(alpha, m_ewma_tsc_square, tsd * tsd);
    m_ewma_tsc = ewma(alpha, m_ewma_tsc, tsd);
    m_ewma_time = ewma(alpha, m_ewma_time, td);

    if (m_ewma_tsc_square > 0.0)
      m_sec_per_cycle = m_ewma_time_tsc / m_ewma_tsc_square;

    m_last_tsc = tsc;
    m_last_time = t;
    return std::isfinite(m_sec_per_cycle) && m_sec_per_cycle > 0.0;
  }

  double sec_per_cycle() const noexcept { return m_sec_per_cycle; }

 private:
  static double ewma(double alpha, double old_v, double add_v) noexcept {
    return old_v + alpha * (add_v - old_v);
  }

  double m_ewma_time_tsc = 0.0;
  double m_ewma_tsc_square = 0.0;
  double m_ewma_tsc = 0.0;
  double m_ewma_time = 0.0;
  uint64_t m_last_tsc = 0;
  double m_last_time = 0.0;
  double m_sec_per_cycle = 0.0;
};

// =========================================================================
// Internal state
// =========================================================================
struct TscLevel {
  enum class Kind { Stable, Unstable };
  Kind m_kind = Kind::Unstable;
  uint64_t m_cycles_per_second = 0;
  uint64_t m_cycles_from_anchor = 0;
};

inline std::atomic<bool> g_available{false};
inline TscLevel g_tsc_level{};
inline EwmaCalibrator g_calibrator{};
inline std::mutex g_calibrator_mutex{};
inline std::atomic<bool> g_initialized{false};
inline std::atomic<uint64_t> g_next_cal_tsc{0};

inline uint64_t next_cal_tsc_locked(uint64_t now) noexcept {
  const double sec_per_cycle = g_calibrator.sec_per_cycle();
  const double cycles = 1.0 / sec_per_cycle;
  constexpr double max_uint64 = static_cast<double>(
      std::numeric_limits<uint64_t>::max());
  if (!std::isfinite(cycles) || cycles <= 0.0 || cycles >= max_uint64)
    return std::numeric_limits<uint64_t>::max();

  const uint64_t delta = static_cast<uint64_t>(cycles);
  if (delta > std::numeric_limits<uint64_t>::max() - now)
    return std::numeric_limits<uint64_t>::max();
  return now + delta;
}

// =========================================================================
// Initial calibration  —  convergent loop matching static backend
// =========================================================================

/// Online RDTSC version of monotonic_with_tsc (no LFENCE).
inline std::pair<std::chrono::steady_clock::time_point, uint64_t>
online_monotonic_with_tsc() {
  uint64_t tsc = __rdtsc();
  std::atomic_signal_fence(std::memory_order_seq_cst);
  return {std::chrono::steady_clock::now(), tsc};
}

struct CpsMeasurement {
  uint64_t cycles_per_second;
  std::pair<std::chrono::steady_clock::time_point, uint64_t> first;
  std::pair<std::chrono::steady_clock::time_point, uint64_t> last;
};

/// Convergent cps estimation — same algorithm as static's _cycles_per_sec.
inline CpsMeasurement online_cycles_per_sec() {
  using namespace std::chrono;
  double old_cps = 0.0;
  double cps = 0.0;
  std::pair<steady_clock::time_point, uint64_t> first;
  std::pair<steady_clock::time_point, uint64_t> last;

  for (;;) {
    auto [t1, tsc1] = online_monotonic_with_tsc();
    first = {t1, tsc1};
    for (;;) {
      auto [t2, tsc2] = online_monotonic_with_tsc();
      last = {t2, tsc2};
      auto elapsed_ns = duration_cast<nanoseconds>(t2 - t1).count();
      if (elapsed_ns > 10'000'000) {
        if (tsc2 < tsc1) break;
        cps = static_cast<double>(tsc2 - tsc1) * 1'000'000'000.0 /
              static_cast<double>(elapsed_ns);
        break;
      }
    }
    if (cps > 0.0 && std::fabs(cps - old_cps) / cps < 0.00001) break;
    old_cps = cps;
  }

  return {static_cast<uint64_t>(std::round(cps)), first, last};
}

inline TscLevel init_calibrator() {
  using namespace std::chrono;

  auto t0 = steady_clock::now();
  uint64_t tsc0 = __rdtsc();

  auto measurement = online_cycles_per_sec();
  if (measurement.cycles_per_second == 0)
    return TscLevel{};

  // Prime EWMA with the two real, separated samples from the convergence
  // measurement, avoiding a zero-length initial calibration interval.
  {
    std::lock_guard lock(g_calibrator_mutex);
    const auto [t0, tsc0] = measurement.first;
    const auto [t1, tsc1] = measurement.last;
    g_calibrator.reset(tsc0, duration<double>(t0.time_since_epoch()).count());
    if (!g_calibrator.calibrate(
            tsc1, duration<double>(t1.time_since_epoch()).count(), true) ||
        !std::isfinite(g_calibrator.sec_per_cycle()) ||
        g_calibrator.sec_per_cycle() <= 0.0)
      return TscLevel{};
    g_next_cal_tsc.store(next_cal_tsc_locked(tsc1),
                         std::memory_order_release);
  }
  return TscLevel{TscLevel::Kind::Stable, measurement.cycles_per_second,
                  measurement.first.second};
}

// =========================================================================
// Public API
// =========================================================================

inline bool is_tsc_available() noexcept {
  return g_available.load(std::memory_order_acquire);
}

inline double nanos_per_cycle() noexcept {
  std::lock_guard lock(g_calibrator_mutex);
  return g_calibrator.sec_per_cycle() * 1'000'000'000.0;
}

inline uint64_t current_cycle() noexcept {
  if (g_tsc_level.m_kind != TscLevel::Kind::Stable) [[unlikely]]
    std::abort();

  uint64_t now = __rdtsc();
  uint64_t next = g_next_cal_tsc.load(std::memory_order_relaxed);
  if (now >= next) [[unlikely]] {
    std::lock_guard lock(g_calibrator_mutex);
    next = g_next_cal_tsc.load(std::memory_order_relaxed);
    if (now >= next) {
      // Re-sample after acquiring the lock.  The timestamp used for the
      // calibration must correspond to the TSC value used for its delta.
      auto [t, tsc] = online_monotonic_with_tsc();
      using namespace std::chrono;
      if (g_calibrator.calibrate(
              tsc, duration<double>(t.time_since_epoch()).count(), false))
        g_next_cal_tsc.store(next_cal_tsc_locked(tsc),
                             std::memory_order_release);
    }
  }
  return now - g_tsc_level.m_cycles_from_anchor;
}

inline void recalibrate() noexcept {
  std::lock_guard lock(g_calibrator_mutex);
  auto [t, tsc] = online_monotonic_with_tsc();
  using namespace std::chrono;
  if (g_calibrator.calibrate(
          tsc, duration<double>(t.time_since_epoch()).count(), false))
    g_next_cal_tsc.store(next_cal_tsc_locked(tsc), std::memory_order_release);
}

// =========================================================================
// Static constructor
// =========================================================================
namespace {
__attribute__((constructor(101))) void init_online_tsc() {
  bool expected = false;
  if (!g_initialized.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel))
    return;
  if (!detail::tsc_common::is_tsc_stable()) {
    g_available.store(false, std::memory_order_release);
    return;
  }
  g_tsc_level = init_calibrator();
  std::atomic_thread_fence(std::memory_order_seq_cst);
  g_available.store(g_tsc_level.m_kind == TscLevel::Kind::Stable,
                    std::memory_order_release);
}
}  // namespace

}  // namespace fastant::detail::online

#endif  // __linux__ && (__x86_64__ || __i386__)
