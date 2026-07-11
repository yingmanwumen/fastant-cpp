# fastant-cpp

A C++23 header-only timing library for low-overhead, monotonic time sampling.
The accelerated path is conditional on the platform and runtime checks described
below; otherwise the library uses `std::chrono::steady_clock`.

This is a port of the Rust [fastant](https://github.com/fast/fastant) crate
(originally developed for [TiKV](https://github.com/tikv/tikv)), extended with
an online-calibrated backend inspired by
[JaneStreet's `time_stamp_counter`](https://github.com/janestreet/time_stamp_counter).

## How It Works

On supported x86 Linux systems, the library may read the CPU's TSC via the
`RDTSC` instruction. The invariant-TSC CPUID bit and the kernel's current
clocksource from `/sys` are heuristic eligibility checks, not guarantees of
consistent behavior across cores, sockets, or virtual machines. If the checks
do not pass, the library falls back to `std::chrono::steady_clock`.

TSC cycles are converted to nanoseconds using a calibrated conversion factor
`nanos_per_cycle = 1e9 / cycles_per_second`. The library exposes two backends
with different calibration policies. On unsupported platforms, or when the
runtime checks reject TSC use, both backends fall back to
`std::chrono::steady_clock`.

## Backends

| Property | `static_clock` (default) | `online` |
|---|---|---|
| Calibration | Once at startup against `steady_clock` | Ongoing EWMA following `steady_clock` |
| TSC serialization | None on the `now()` hot path | None (compiler barrier only) |
| Hot-path structure | TSC read and anchor subtraction | TSC read, anchor subtraction, and deadline check |
| Calibration overhead | Startup only | Low-frequency calibration can add latency |
| `now()` cost | `rdtsc - anchor` | `rdtsc - anchor` + cal check |
| Time model | Fixed conversion after startup | Current conversion updated over time |

### Algorithm origins and semantics

- **`static_clock`** ports the startup calibration design from Rust
  [`fastant`](https://github.com/fast/fastant): it measures TSC against a
  monotonic reference over repeated windows, derives a fixed cycles-to-time
  conversion and anchors subsequent TSC reads to that calibration.
- **`online`** is inspired by Jane Street's
  [`time_stamp_counter`](https://github.com/janestreet/core_unix/blob/master/time_stamp_counter/src/time_stamp_counter.ml).
  It retains the EWMA regression used to estimate the current TSC-to-time
  slope from sampled deltas.
- Jane Street's implementation also has a separate monotonic catch-up mapping
  to smooth calibration-induced changes. This backend does **not** implement
  that layer: it publishes the current EWMA slope directly. Consequently,
  recalibration can change the conversion applied to historical `Instant`
  values; use `static_clock` when that semantic is unsuitable.

**`static_clock` (recommended)** — calibrates once before `main()` by measuring
TSC increments against `steady_clock`. Its `now()` hot path issues `RDTSC`
without a hardware serialization fence and then applies the startup conversion.

**`online`** — calibrates continuously using an exponentially weighted moving
average (EWMA) linear regression that follows `steady_clock`'s observed
conversion. It does not issue a hardware serialization instruction around
`RDTSC`; its compiler barrier alone does not prevent CPU execution reordering.
Low-frequency recalibration can add latency. Because its conversion changes,
durations computed from historical `Instant` values may differ after a
calibration update; it is not an independent or absolute clock.

## Quick Start

```cpp
#include "fastant/fastant.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    // Use the default backend (startup calibration)
    auto start = fastant::static_clock::Instant::now();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto elapsed = start.elapsed();
    std::cout << "Elapsed: " << elapsed.count() << " ns\n";

    // Check TSC availability
    std::cout << "TSC available: "
              << (fastant::is_tsc_available() ? "yes" : "no") << "\n";
}
```

## API

All types are in the `fastant::static_clock` and `fastant::online` namespaces.
`fastant::static_clock` is the recommended default.

### `Instant`

A point in time represented by the selected backend's cycle or timestamp value.

| Method | Description |
|---|---|
| `Instant::now()` | Capture the current instant |
| `elapsed()` → `std::chrono::nanoseconds` | Time elapsed since creation |
| `duration_since(earlier)` → `nanoseconds` | Saturating duration between two instants |
| `checked_duration_since(earlier)` → `optional<nanoseconds>` | Checked duration (nullopt if earlier later) |
| `checked_add(duration)` → `optional<Instant>` | Addition with overflow check |
| `checked_sub(duration)` → `optional<Instant>` | Subtraction with underflow check |
| `as_unix_nanos(anchor)` → `uint64_t` | Convert to Unix nanosecond timestamp |
| `operator<=>` | Three-way comparison |
| `operator+` / `operator-` (duration) | Arithmetic (aborts on overflow) |
| `operator-` (Instant) | Instant difference → nanoseconds (saturating) |
| `Instant::ZERO` | Zero-valued constant |

### `Anchor`

| Method | Description |
|---|---|
| `Anchor::new_anchor()` | Pair `system_clock::now()` timestamp with current TSC cycle |

### `AtomicInstant`

Atomic wrapper around `Instant`. Supports `load`, `store`, `swap`,
`fetch_max`, `fetch_min`, `into_instant`. Memory-order aware with validation.

### Free Functions

| Function | Description |
|---|---|
| `fastant::is_tsc_available()` | Whether TSC acceleration is active (static_clock backend status) |

## Platform Support

| Platform | Timer | Cycle granularity |
|---|---|---|
| Supported x86 Linux | Time Stamp Counter (TSC) | Typical cycle value depends on the CPU; not a timestamp-accuracy guarantee |
| Other platforms | `std::chrono::steady_clock` | OS-dependent (~ns) |

TSC timestamps are sampled without a hardware serialization guarantee, so an
individual sampling point may not correspond exactly to the surrounding
instructions' execution point.

## Requirements

- **C++23** compiler (GCC ≥ 14, Clang ≥ 19)
- **CMake** ≥ 3.21
- x86-64 CPU with invariant TSC for hardware acceleration (falls back gracefully otherwise)

## Building

```bash
# Header-only — just add the include path
cmake -B build && cmake --build build

# With tests (requires Catch2, fetched automatically)
cmake -B build -DFASTANT_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build

# With benchmarks (requires Google Benchmark, fetched automatically)
cmake -B build -DFASTANT_BUILD_BENCHMARKS=ON
cmake --build build && ./build/bench/bench_instant

# With examples
cmake -B build -DFASTANT_BUILD_EXAMPLES=ON
cmake --build build
```

## Performance

The following are results from one benchmark run on one x86-64 Linux machine
(AMD Ryzen, approximately 3.75 GHz, GCC 14). They are illustrative only and
must not be generalized to other machines, compilers, or build settings:

```
BM_StaticInstantNow       7.6 ns   (static_clock, rdtsc - anchor, no hardware fence)
BM_OnlineInstantNow       7.0 ns   (online, rdtsc only)
BM_InstantNowSteadyClock  18.9 ns  (std::chrono::steady_clock)
BM_AnchorNew              23.5 ns  (system_clock + rdtsc)
```

## Long-Term Drift

This is a local consistency observation: both backends are calibrated against
and compared with `std::chrono::steady_clock` on the same machine. Each
backend's measurement uses its corresponding steady-clock endpoint:

![Long-term drift chart](assets/longterm_drift.png)

## License

Apache 2.0 — see [LICENSE](./LICENSE)
