/// @file
/// @brief Header-only fastant library: Instant, Anchor, AtomicInstant.
///
/// Provides nanosecond-precision timing with TSC acceleration on x86 Linux
/// and a portable fallback based on std::chrono::steady_clock.
///
/// Two backends live in parallel namespaces:
///   - fastant::static_clock::  — one-shot static calibration (fastant
///   upstream)
///   - fastant::online::        — online RDTSC with online EWMA
///   auto-calibration
///
/// Top-level aliases (fastant::Instant, etc.) point to fastant::static_clock::
/// for backward compatibility.

#pragma once

/// Define a single internal macro so we don't repeat the #if guard everywhere.
#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))
#define FASTANT_X86_LINUX 1
#include "detail/tsc_online.hpp"  ///< online path:  fastant::online
#include "detail/tsc_static.hpp"  ///< static path: fastant::detail::tsc_now
#endif

#include <atomic>      ///< std::atomic<>, std::memory_order
#include <chrono>      ///< std::chrono::*
#include <cstdint>     ///< uint64_t, UINT64_MAX
#include <cstdlib>     ///< std::abort
#include <functional>  ///< std::hash<>
#include <limits>      ///< std::numeric_limits
#include <optional>    ///< std::optional, std::nullopt
#include <ostream>     ///< std::ostream

namespace fastant {
namespace detail {

/// @brief Fallback clock using `std::chrono::steady_clock` nanosecond
/// timestamp.
inline uint64_t current_cycle_fallback() noexcept {
  auto dur = std::chrono::steady_clock::now().time_since_epoch();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
  return ns > 0 ? static_cast<uint64_t>(ns) : 0;
}

// ---------------------------------------------------------------------------
// Backend policy structs
// ---------------------------------------------------------------------------

/// Policy struct for the static/default backend.
/// Forwards to tsc_now on x86, falls back to steady_clock elsewhere.
struct StaticBackend {
  static uint64_t current_cycle() noexcept {
#ifdef FASTANT_X86_LINUX
    if (tsc_now::is_tsc_available()) [[likely]]
      return tsc_now::current_cycle();
    return current_cycle_fallback();
#else
    return current_cycle_fallback();
#endif
  }
  static double nanos_per_cycle() noexcept {
#ifdef FASTANT_X86_LINUX
    return tsc_now::nanos_per_cycle();
#else
    return 1.0;
#endif
  }
};

/// Policy struct for the online RDTSC backend with online EWMA calibration.
struct OnlineBackend {
  static uint64_t current_cycle() noexcept {
#ifdef FASTANT_X86_LINUX
    if (online::is_tsc_available()) [[likely]]
      return online::current_cycle();
    return current_cycle_fallback();
#else
    return current_cycle_fallback();
#endif
  }
  static double nanos_per_cycle() noexcept {
#ifdef FASTANT_X86_LINUX
    if (!online::is_tsc_available())
      return 1.0;
    return online::nanos_per_cycle();
#else
    return 1.0;
#endif
  }
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
template <typename Backend>
class Instant;

template <typename Backend>
struct Anchor;

template <typename Backend>
class AtomicInstant;

// =========================================================================
// template <typename Backend> class Instant
// =========================================================================

/// @brief A point in time measured by a TSC cycle counter.
///
/// @tparam Backend  Policy struct providing static current_cycle() and
///                  nanos_per_cycle() — see StaticBackend / OnlineBackend.
template <typename Backend>
class Instant {
  uint64_t m_value;  ///< Raw cycle / nanosecond value.

  explicit constexpr Instant(uint64_t v) noexcept : m_value(v) {}

 public:
  /// @brief Zero constant.
  static const Instant ZERO;

  /// @brief Default constructor — zero-initialised.
  constexpr Instant() noexcept : m_value(0) {}

  /// @brief Construct an Instant from a raw cycle value.
  [[nodiscard]] static constexpr Instant from_raw(uint64_t v) noexcept {
    return Instant(v);
  }

  /// @brief Capture the current instant using the chosen backend.
  [[nodiscard]] static inline Instant now() noexcept;

  // -- duration arithmetic (Instant - Instant → nanoseconds) ----------------

  /// @brief Compute duration since an earlier instant (saturating to zero).
  /// @param earlier  Reference point — must be earlier for a positive result.
  [[nodiscard]] std::chrono::nanoseconds duration_since(
      Instant earlier) const noexcept {
    return checked_duration_since(earlier).value_or(
        std::chrono::nanoseconds{0});
  }

  /// @brief Checked duration since an earlier instant.
  /// @param earlier  Reference point.
  /// @return nullopt if earlier > *this, otherwise the duration.
  [[nodiscard]] std::optional<std::chrono::nanoseconds> checked_duration_since(
      Instant earlier) const noexcept {
    if (earlier.m_value > m_value) return std::nullopt;
    uint64_t delta = m_value - earlier.m_value;
    uint64_t ns = static_cast<uint64_t>(static_cast<double>(delta) *
                                        Backend::nanos_per_cycle());
    return std::chrono::nanoseconds{ns};
  }

  /// @brief Saturating duration since an earlier instant.
  /// @param earlier  Reference point.
  /// @return Duration clamped to zero if earlier is later.
  [[nodiscard]] std::chrono::nanoseconds saturating_duration_since(
      Instant earlier) const noexcept {
    return checked_duration_since(earlier).value_or(
        std::chrono::nanoseconds{0});
  }

  /// @brief Time elapsed since this instant.
  /// @return Duration from *this to now.
  [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept {
    return now() - *this;
  }

  // -- checked add/sub (Instant ± Duration → optional<Instant>) ------------

  /// @brief Checked addition of a duration.
  /// @param duration  Nanoseconds to add.
  /// @return nullopt on overflow or negative input.
  [[nodiscard]] std::optional<Instant> checked_add(
      std::chrono::nanoseconds duration) const noexcept {
    if (duration.count() < 0) return std::nullopt;
    uint64_t cycles = static_cast<uint64_t>(
        static_cast<double>(duration.count()) / Backend::nanos_per_cycle());
    if (cycles > std::numeric_limits<uint64_t>::max() - m_value)
      return std::nullopt;
    return Instant{m_value + cycles};
  }

  /// @brief Checked subtraction of a duration.
  /// @param duration  Nanoseconds to subtract.
  /// @return nullopt on underflow or negative input.
  [[nodiscard]] std::optional<Instant> checked_sub(
      std::chrono::nanoseconds duration) const noexcept {
    if (duration.count() < 0) return std::nullopt;
    uint64_t cycles = static_cast<uint64_t>(
        static_cast<double>(duration.count()) / Backend::nanos_per_cycle());
    if (cycles > m_value) return std::nullopt;
    return Instant{m_value - cycles};
  }

  // -- conversion to unix nanos via anchor ----------------------------------

  /// @brief Convert to an estimated Unix-nanosecond timestamp.
  /// @param anchor  Reference point providing wall-clock time and cycle.
  /// @return Estimated Unix nanosecond timestamp.
  [[nodiscard]] uint64_t as_unix_nanos(
      const Anchor<Backend>& anchor) const noexcept;

  // -- comparison -----------------------------------------------------------

  /// @brief Default three-way comparison (delegates to m_value).
  auto operator<=>(const Instant&) const = default;

  // -- arithmetic operators -------------------------------------------------

  /// @brief Add a duration (aborts on overflow).
  [[nodiscard]] Instant operator+(std::chrono::nanoseconds d) const noexcept {
    auto r = checked_add(d);
    if (!r) [[unlikely]]
      std::abort();
    return *r;
  }
  /// @brief Add-assign a duration (aborts on overflow).
  Instant& operator+=(std::chrono::nanoseconds d) noexcept {
    *this = *this + d;
    return *this;
  }
  /// @brief Subtract a duration (aborts on underflow).
  [[nodiscard]] Instant operator-(std::chrono::nanoseconds d) const noexcept {
    auto r = checked_sub(d);
    if (!r) [[unlikely]]
      std::abort();
    return *r;
  }
  /// @brief Subtract-assign a duration (aborts on underflow).
  Instant& operator-=(std::chrono::nanoseconds d) noexcept {
    *this = *this - d;
    return *this;
  }
  /// @brief Instant - Instant → nanoseconds.
  [[nodiscard]] std::chrono::nanoseconds operator-(
      Instant other) const noexcept {
    return duration_since(other);
  }

  // -- friends --------------------------------------------------------------

  friend struct std::hash<detail::Instant<Backend>>;

  friend std::ostream& operator<<(std::ostream& os, Instant i) {
    return os << i.m_value;
  }

  friend class AtomicInstant<Backend>;
};

/// @brief Out-of-class definition of ZERO.
template <typename Backend>
inline constexpr Instant<Backend> Instant<Backend>::ZERO{0};

// =========================================================================
// template <typename Backend> struct Anchor
// =========================================================================

/// @brief A wall-clock anchor pairing a system_clock timestamp with a
/// cycle counter value, used to convert Instants to Unix nanoseconds.
///
/// @tparam Backend  Policy struct — same as for Instant.
template <typename Backend>
struct Anchor {
  /// @brief Capture a fresh anchor: system_clock::now() + cycle counter.
  Anchor() noexcept {
    auto dur = std::chrono::system_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
    if (ns < 0) std::abort();
    m_unix_time_ns = static_cast<uint64_t>(ns);
    m_cycle = Backend::current_cycle();
  }

  /// @brief Convenience alias for the constructor.
  static inline Anchor new_anchor() noexcept { return Anchor(); }

 private:
  uint64_t m_unix_time_ns;        ///< Unix nanosecond timestamp at creation.
  uint64_t m_cycle;               ///< Cycle counter at creation.
  friend class Instant<Backend>;  ///< Needed for as_unix_nanos().
};

/// @brief Check whether the TSC-based cycle counter is usable.
[[nodiscard]] inline bool is_tsc_available() noexcept {
#ifdef FASTANT_X86_LINUX
  return detail::tsc_now::is_tsc_available();
#else
  return false;
#endif
}

// -- out-of-class definitions for Instant -----------------------------------

template <typename Backend>
[[nodiscard]] inline Instant<Backend> Instant<Backend>::now() noexcept {
  return Instant(Backend::current_cycle());
}

template <typename Backend>
[[nodiscard]] inline uint64_t Instant<Backend>::as_unix_nanos(
    const Anchor<Backend>& anchor) const noexcept {
  if (m_value > anchor.m_cycle) {
    uint64_t forward_ns =
        static_cast<uint64_t>(static_cast<double>(m_value - anchor.m_cycle) *
                              Backend::nanos_per_cycle());
    return anchor.m_unix_time_ns + forward_ns;
  } else {
    uint64_t backward_ns =
        static_cast<uint64_t>(static_cast<double>(anchor.m_cycle - m_value) *
                              Backend::nanos_per_cycle());
    return anchor.m_unix_time_ns - backward_ns;
  }
}

// =========================================================================
// template <typename Backend> class AtomicInstant
// =========================================================================
#if !defined(FASTANT_NO_ATOMIC)

/// @brief Atomic wrapper around Instant for lock-free timestamp sharing.
///
/// @tparam Backend  Policy struct — same as for Instant.
template <typename Backend>
class AtomicInstant {
  std::atomic<uint64_t> m_value;

  static constexpr std::memory_order to_load_order(
      std::memory_order o) noexcept {
    if (o == std::memory_order_release || o == std::memory_order_relaxed)
      return std::memory_order_relaxed;
    if (o == std::memory_order_acq_rel || o == std::memory_order_acquire)
      return std::memory_order_acquire;
    return o;
  }

 public:
  using InstantType = Instant<Backend>;

  /// @brief Wrap an Instant in an atomic.
  explicit AtomicInstant(InstantType v) noexcept : m_value(v.m_value) {}

  /// @brief Atomically load the stored instant.
  /// @param order  Memory order (default: seq_cst).
  /// @note Aborts if order is release or acq_rel.
  [[nodiscard]] InstantType load(
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    if (order == std::memory_order_release ||
        order == std::memory_order_acq_rel) [[unlikely]]
      std::abort();
    return InstantType(m_value.load(order));
  }

  /// @brief Atomically store an Instant.
  /// @param val    Value to store.
  /// @param order  Memory order (default: seq_cst).
  /// @note Aborts if order is consume, acquire, or acq_rel.
  void store(InstantType val,
             std::memory_order order = std::memory_order_seq_cst) noexcept {
    if (order == std::memory_order_consume ||
        order == std::memory_order_acquire ||
        order == std::memory_order_acq_rel) [[unlikely]]
      std::abort();
    m_value.store(val.m_value, order);
  }

  /// @brief Atomically swap the stored instant.
  /// @param val    Replacement value.
  /// @param order  Memory order (default: seq_cst).
  /// @return The value previously stored.
  [[nodiscard]] InstantType swap(
      InstantType val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    return InstantType(m_value.exchange(val.m_value, order));
  }

  /// @brief Atomically set to max(current, val), return the old value.
  [[nodiscard]] InstantType fetch_max(
      InstantType val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    uint64_t old = m_value.load(to_load_order(order));
    while (val.m_value > old) {
      if (m_value.compare_exchange_weak(old, val.m_value, order,
                                        to_load_order(order)))
        return InstantType(old);
    }
    return InstantType(old);
  }

  /// @brief Atomically set to min(current, val), return the old value.
  [[nodiscard]] InstantType fetch_min(
      InstantType val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    uint64_t old = m_value.load(to_load_order(order));
    while (val.m_value < old) {
      if (m_value.compare_exchange_weak(old, val.m_value, order,
                                        to_load_order(order)))
        return InstantType(old);
    }
    return InstantType(old);
  }

  /// @brief Consume the atomic, returning the stored instant.
  [[nodiscard]] InstantType into_instant() && noexcept {
    return InstantType(m_value.load(std::memory_order_relaxed));
  }

  /// @brief Assign from an Instant (delegates to store).
  AtomicInstant& operator=(InstantType v) noexcept {
    store(v);
    return *this;
  }
};
#endif

/// @brief Backward-compat: default (static) current_cycle().
inline uint64_t current_cycle() noexcept {
  return StaticBackend::current_cycle();
}
/// @brief Backward-compat: default (static) nanos_per_cycle().
inline double nanos_per_cycle() noexcept {
  return StaticBackend::nanos_per_cycle();
}

}  // namespace detail

// =========================================================================
// Public namespace aliases
// =========================================================================

/// One-shot static calibration backend (safe default).
namespace static_clock {
using Instant = detail::Instant<detail::StaticBackend>;
using Anchor = detail::Anchor<detail::StaticBackend>;
#if !defined(FASTANT_NO_ATOMIC)
using AtomicInstant = detail::AtomicInstant<detail::StaticBackend>;
#endif
}  // namespace static_clock

/// Online RDTSC backend with online EWMA calibration (JaneStreet-style).
namespace online {
using Instant = detail::Instant<detail::OnlineBackend>;
using Anchor = detail::Anchor<detail::OnlineBackend>;
#if !defined(FASTANT_NO_ATOMIC)
using AtomicInstant = detail::AtomicInstant<detail::OnlineBackend>;
#endif
}  // namespace online

using Instant = detail::Instant<detail::StaticBackend>;
using Anchor = detail::Anchor<detail::StaticBackend>;
#if !defined(FASTANT_NO_ATOMIC)
using AtomicInstant = detail::AtomicInstant<detail::StaticBackend>;
#endif

/// @brief Check whether the TSC-based cycle counter is usable.
[[nodiscard]] inline bool is_tsc_available() noexcept {
#ifdef FASTANT_X86_LINUX
  return detail::tsc_now::is_tsc_available();
#else
  return false;
#endif
}

}  // namespace fastant

// =========================================================================
// std::hash specializations
// =========================================================================
namespace std {

template <typename Backend>
struct hash<fastant::detail::Instant<Backend>> {
  /// @brief Hash the raw cycle value.
  size_t operator()(fastant::detail::Instant<Backend> i) const noexcept {
    return hash<uint64_t>{}(i.m_value);
  }
};

}  // namespace std

#undef FASTANT_X86_LINUX
