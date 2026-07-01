/// @file
/// @brief Header-only fastant library: Instant, Anchor, AtomicInstant.
///
/// Provides nanosecond-precision timing with TSC acceleration on x86 Linux
/// and a portable fallback based on `std::chrono::system_clock`.

#pragma once

/// Define a single internal macro so we don't repeat the #if guard everywhere.
#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))
#define FASTANT_X86_LINUX 1
#include "detail/tsc_now.hpp"  ///< fastant::detail::tsc_now::*
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

/// @brief Fallback clock using `std::chrono::system_clock` nanosecond
/// timestamp.
/// @return Number of nanoseconds since Unix epoch, clamped to 0.
inline uint64_t current_cycle_fallback() noexcept {
  auto dur = std::chrono::system_clock::now().time_since_epoch();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
  return ns > 0 ? static_cast<uint64_t>(ns) : 0;  ///< unwrap_or(0)
}

/// @brief Returns the current cycle count, preferring TSC when available.
/// @return Current cycle value (TSC or nanosecond fallback).
#ifdef FASTANT_X86_LINUX
inline uint64_t current_cycle() noexcept {
  if (tsc_now::is_tsc_available()) [[likely]] {
    return tsc_now::current_cycle();
  }
  return current_cycle_fallback();
}

/// @brief Nanoseconds per cycle for converting between cycles and time.
/// @return Conversion factor from TSC on x86 Linux, or 1.0 elsewhere.
inline double nanos_per_cycle() noexcept { return tsc_now::nanos_per_cycle(); }
#else
inline uint64_t current_cycle() noexcept { return current_cycle_fallback(); }

inline double nanos_per_cycle() noexcept { return 1.0; }
#endif

}  // namespace detail

struct Anchor;
class AtomicInstant;

/// @brief High-resolution timestamp backed by TSC or system clock.
class Instant {
  uint64_t _value;  ///< Raw cycle / nanosecond value.

  /// @brief Construct from a raw value
  /// @param v Raw cycle / nanosecond value.
  explicit constexpr Instant(uint64_t v) noexcept : _value(v) {}

 public:
  /// @brief Zero constant.
  static const Instant ZERO;

  /// @brief Default constructor – zero-initialised.
  constexpr Instant() noexcept : _value(0) {}

  /// @brief Capture the current instant.
  /// @return Instant representing now.
  [[nodiscard]] static inline Instant now() noexcept;

  // -- duration arithmetic (Instant - Instant → nanoseconds) ----------------

  /// @brief Compute duration since an earlier instant.
  /// @param earlier Reference point (must be earlier).
  /// @return Duration from `earlier` to `*this`.
  /// @note Returns zero if `earlier` is actually later (saturating).
  [[nodiscard]] std::chrono::nanoseconds duration_since(
      Instant earlier) const noexcept {
    return checked_duration_since(earlier).value_or(
        std::chrono::nanoseconds{0});
  }

  /// @brief Checked duration since an earlier instant.
  /// @param earlier Reference point.
  /// @return `nullopt` if `earlier > *this`, otherwise the duration.
  [[nodiscard]] std::optional<std::chrono::nanoseconds> checked_duration_since(
      Instant earlier) const noexcept {
    if (earlier._value > _value) {
      return std::nullopt;
    }
    uint64_t delta = _value - earlier._value;
    uint64_t ns = static_cast<uint64_t>(static_cast<double>(delta) *
                                        detail::nanos_per_cycle());
    return std::chrono::nanoseconds{ns};
  }

  /// @brief Saturating duration since an earlier instant.
  /// @param earlier Reference point.
  /// @return Duration clamped to 0 if `earlier > *this`.
  [[nodiscard]] std::chrono::nanoseconds saturating_duration_since(
      Instant earlier) const noexcept {
    return checked_duration_since(earlier).value_or(
        std::chrono::nanoseconds{0});
  }

  /// @brief Time elapsed since this instant.
  /// @return Duration from `*this` to now.
  [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept {
    return Instant::now() - *this;
  }

  // -- checked add/sub (Instant ± Duration → optional<Instant>) ------------

  /// @brief Checked addition of a duration.
  /// @param duration Nanoseconds to add.
  /// @return `nullopt` on overflow or negative input.
  [[nodiscard]] std::optional<Instant> checked_add(
      std::chrono::nanoseconds duration) const noexcept {
    if (duration.count() < 0) {
      return std::nullopt;
    }
    uint64_t cycles = static_cast<uint64_t>(
        static_cast<double>(duration.count()) / detail::nanos_per_cycle());
    /// Portably check for overflow (avoids __builtin_add_overflow for MSVC)
    if (cycles > std::numeric_limits<uint64_t>::max() - _value) {
      return std::nullopt;
    }
    return Instant{_value + cycles};
  }

  /// @brief Checked subtraction of a duration.
  /// @param duration Nanoseconds to subtract.
  /// @return `nullopt` on underflow or negative input.
  [[nodiscard]] std::optional<Instant> checked_sub(
      std::chrono::nanoseconds duration) const noexcept {
    if (duration.count() < 0) {
      return std::nullopt;
    }
    uint64_t cycles = static_cast<uint64_t>(
        static_cast<double>(duration.count()) / detail::nanos_per_cycle());
    if (cycles > _value) {
      return std::nullopt;
    }
    return Instant{_value - cycles};
  }

  // -- conversion to unix nanos via anchor ----------------------------------

  /// @brief Convert to Unix nanosecond timestamp using an anchor.
  /// @param anchor Reference point providing both wall-clock time and cycle.
  /// @return Estimated Unix nanosecond timestamp.
  [[nodiscard]] uint64_t as_unix_nanos(const Anchor& anchor) const noexcept;

  // -- comparison -----------------------------------------------------------

  /// @brief Default three-way comparison (delegates to `_value`).
  auto operator<=>(const Instant&) const = default;

  // -- arithmetic operators -------------------------------------------------

  /// @brief Add duration
  Instant operator+(std::chrono::nanoseconds d) const noexcept {
    auto r = checked_add(d);
    if (!r) [[unlikely]] {
      std::abort();
    }
    return *r;
  }

  /// @brief Add-assign duration
  Instant& operator+=(std::chrono::nanoseconds d) noexcept {
    *this = *this + d;
    return *this;
  }

  /// @brief Subtract duration
  Instant operator-(std::chrono::nanoseconds d) const noexcept {
    auto r = checked_sub(d);
    if (!r) [[unlikely]] {
      std::abort();
    }
    return *r;
  }

  /// @brief Subtract-assign duration
  Instant& operator-=(std::chrono::nanoseconds d) noexcept {
    *this = *this - d;
    return *this;
  }

  /// @brief Instant - Instant → nanoseconds
  std::chrono::nanoseconds operator-(Instant other) const noexcept {
    return duration_since(other);
  }

  /// Friends that need access to private `_value`.
  friend struct std::hash<Instant>;
  friend std::ostream& operator<<(std::ostream&, Instant);
  friend class AtomicInstant;
};

/// @brief Out-of-class definition of `Instant::ZERO`.
/// Cannot be defined inline because the class is incomplete at the point of
/// a static data member declaration when the member type is the enclosing
/// class.
inline constexpr Instant Instant::ZERO{0};

struct Anchor {
  /// @brief Capture a new anchor (wall-clock + cycle).
  ///
  /// Does the work inline (cannot delegate to `new_anchor()` because that would
  /// recurse — `new_anchor()` creates a local `Anchor`, calling this
  /// constructor again).
  Anchor() noexcept {
    auto dur = std::chrono::system_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
    if (ns < 0) {
      std::abort();
    }  // unexpected time drift
    unix_time_ns = static_cast<uint64_t>(ns);
    cycle = detail::current_cycle();
  }

  /// @brief Create a new anchor (convenience alias for the constructor).
  /// @return A fresh Anchor with current wall-clock and cycle.
  static inline Anchor new_anchor() noexcept { return Anchor(); }

 private:
  uint64_t unix_time_ns;  ///< Unix nanosecond timestamp at creation.
  uint64_t cycle;         ///< Cycle counter at creation.
  friend class Instant;   ///< Needed for as_unix_nanos().
};

/// @brief Check whether the TSC-based cycle counter is usable.
/// @return `true` on x86 Linux with a stable TSC, `false` otherwise.
[[nodiscard]] inline bool is_tsc_available() noexcept {
#ifdef FASTANT_X86_LINUX
  return detail::tsc_now::is_tsc_available();
#else
  return false;
#endif
}

/// @brief Capture the current instant via the platform cycle counter.
[[nodiscard]] inline Instant Instant::now() noexcept {
  return Instant(detail::current_cycle());
}

/// @brief Convert this instant to a Unix nanosecond timestamp.
///
/// Uses the anchor as a reference point.  If this instant is ahead of the
/// anchor the forward offset is added; otherwise the backward offset is
/// subtracted.  This yields a monotonic estimate even when the clock and
/// TSC are not perfectly synchronised.
/// @param anchor Reference point providing wall-clock and cycle.
/// @return Estimated Unix nanosecond timestamp.
[[nodiscard]] inline uint64_t Instant::as_unix_nanos(
    const Anchor& anchor) const noexcept {
  if (_value > anchor.cycle) {
    uint64_t forward_ns = static_cast<uint64_t>(
        static_cast<double>(_value - anchor.cycle) * detail::nanos_per_cycle());
    return anchor.unix_time_ns + forward_ns;
  } else {
    uint64_t backward_ns = static_cast<uint64_t>(
        static_cast<double>(anchor.cycle - _value) * detail::nanos_per_cycle());
    return anchor.unix_time_ns - backward_ns;
  }
}

/// @brief Stream the raw `_value` of an Instant.
/// @param os Output stream.
/// @param i Instant to print.
/// @return The output stream.
inline std::ostream& operator<<(std::ostream& os, Instant i) {
  return os << i._value;
}

/// @brief Atomic wrapper around `Instant`, guarded by `FASTANT_NO_ATOMIC`.
#if !defined(FASTANT_NO_ATOMIC)
class AtomicInstant {
  std::atomic<uint64_t> _value;

  /// @brief Map user-provided memory order to a valid `load` order.
  /// @param o User-requested memory order.
  /// @return Order safe for `std::atomic::load`.
  static constexpr std::memory_order to_load_order(
      std::memory_order o) noexcept {
    if (o == std::memory_order_release || o == std::memory_order_relaxed) {
      return std::memory_order_relaxed;
    }
    if (o == std::memory_order_acq_rel || o == std::memory_order_acquire) {
      return std::memory_order_acquire;
    }
    return o;  ///< SeqCst / Consume pass through
  }

 public:
  /// @brief Wrap an Instant in an atomic.
  /// @param v Initial value.
  explicit AtomicInstant(Instant v) noexcept : _value(v._value) {}

  /// @brief Atomically load the stored instant.
  /// @param order Memory order (default: SeqCst).
  /// @note Aborts if `order` is `Release` or `AcqRel`
  /// @return The stored Instant.
  [[nodiscard]] Instant load(
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    if (order == std::memory_order_release ||
        order == std::memory_order_acq_rel) [[unlikely]] {
      std::abort();
    }
    return Instant(_value.load(order));
  }

  /// @brief Atomically store an instant.
  /// @param val Value to store.
  /// @param order Memory order (default: SeqCst).
  /// @note Aborts if `order` is `Acquire` or `AcqRel`
  void store(Instant val,
             std::memory_order order = std::memory_order_seq_cst) noexcept {
    if (order == std::memory_order_acquire ||
        order == std::memory_order_acq_rel) [[unlikely]] {
      std::abort();
    }
    _value.store(val._value, order);
  }

  /// @brief Atomically swap the stored instant.
  /// @param val Replacement value.
  /// @param order Memory order (default: SeqCst).
  /// @return The value that was previously stored.
  [[nodiscard]] Instant swap(
      Instant val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    return Instant(_value.exchange(val._value, order));
  }

  /// @brief Atomically set to `max(current, val)` and return the old value.
  /// @param val Candidate maximum.
  /// @param order Memory order (default: SeqCst).
  /// @return The value before the operation.
  [[nodiscard]] Instant fetch_max(
      Instant val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    uint64_t old = _value.load(to_load_order(order));
    while (val._value > old) {
      if (_value.compare_exchange_weak(old, val._value, order,
                                       std::memory_order_relaxed)) {
        return Instant(old);
      }
    }
    return Instant(old);
  }

  /// @brief Atomically set to `min(current, val)` and return the old value.
  /// @param val Candidate minimum.
  /// @param order Memory order (default: SeqCst).
  /// @return The value before the operation.
  [[nodiscard]] Instant fetch_min(
      Instant val,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    uint64_t old = _value.load(to_load_order(order));
    while (val._value < old) {
      if (_value.compare_exchange_weak(old, val._value, order,
                                       std::memory_order_relaxed)) {
        return Instant(old);
      }
    }
    return Instant(old);
  }

  /// @brief Consume the atomic and return the wrapped instant.
  ///
  /// The `&&` ref-qualifier enforces that this can only be called on rvalue
  /// references (idiomatic consuming operation).  The underlying value is
  /// simply loaded (not exchanged) because the object is about to be destroyed.
  /// @return The stored Instant.
  [[nodiscard]] Instant into_instant() && noexcept {
    return Instant(_value.load(std::memory_order_relaxed));
  }

  /// @brief Assign from an Instant (delegates to `store`).
  /// @param v Value to store.
  /// @return Reference to self.
  AtomicInstant& operator=(Instant v) noexcept {
    store(v);
    return *this;
  }
};
#endif

}  // namespace fastant

namespace std {
/// @brief Hash support for `fastant::Instant`.
template <>
struct hash<fastant::Instant> {
  /// @brief Hash the raw `_value`.
  /// @param i Instant to hash.
  /// @return Hash value (identical to `std::hash<uint64_t>` of the inner
  /// value).
  size_t operator()(fastant::Instant i) const noexcept {
    return hash<uint64_t>{}(i._value);
  }
};
}  // namespace std

#undef FASTANT_X86_LINUX
