# fastant

A drop-in replacement for `std::chrono::steady_clock` that measures time with
high performance and high accuracy powered by the
[Time Stamp Counter (TSC)](https://en.wikipedia.org/wiki/Time_Stamp_Counter).

This is a C++23 port of the Rust [`fastant`](https://github.com/fast/fastant) crate
(originally part of [TiKV](https://github.com/tikv/tikv)).

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)

## Features

- **Fast** — TSC-backed `Instant::now()` takes ~7.5 ns vs ~19 ns for `std::chrono::steady_clock::now()` (x86-64 Linux, ~2.5× speedup)
- **Accurate** — nanosecond precision with TSC frequency calibration
- **Drop-in** — API mirrors `std::chrono` conventions: `elapsed()`, `duration_since()`, arithmetic operators, hashing
- **Header-only** — single `#include "fastant/fastant.hpp"`, no linking required
- **Portable** — falls back to `std::chrono::steady_clock` on non-TSC platforms

## Platform Support

| Platform | Timer | Accuracy |
|---|---|---|
| Linux x86 / x86-64 | Time Stamp Counter (TSC) | ~0.3 ns resolution |
| Other platforms | `std::chrono::steady_clock` | OS-dependent (~ns) |

TSC calibration runs automatically at process startup (`__attribute__((constructor))`).
When TSC is unavailable, the library falls back silently.

## Quick Start

```cpp
#include "fastant/fastant.hpp"
#include <iostream>
#include <thread>

int main() {
    auto start = fastant::Instant::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto elapsed = start.elapsed();
    std::cout << "Elapsed: " << elapsed.count() << " ns" << std::endl;
}
```

## API Overview

### `fastant::Instant`

| Method | Description |
|---|---|
| `Instant::now()` | Capture the current instant |
| `elapsed()` → `nanoseconds` | Time since creation |
| `duration_since(earlier)` → `nanoseconds` | Duration between two instants (saturating) |
| `checked_duration_since(earlier)` → `optional<nanoseconds>` | Checked duration (nullopt if earlier is later) |
| `checked_add(duration)` → `optional<Instant>` | Checked addition (nullopt on overflow) |
| `checked_sub(duration)` → `optional<Instant>` | Checked subtraction (nullopt on underflow) |
| `as_unix_nanos(anchor)` → `uint64_t` | Convert to Unix nanosecond timestamp |
| `operator<=>` | Three-way comparison |
| `operator+` / `operator-` | Arithmetic (aborts on overflow) |
| `ZERO` | Zero constant |

### `fastant::Anchor`

| Method | Description |
|---|---|
| `Anchor::new_anchor()` | Capture wall-clock time + cycle counter |

### `fastant::AtomicInstant`

Thread-safe atomic wrapper around `Instant`. Methods: `load`, `store`, `swap`, `fetch_max`, `fetch_min`, `into_instant`.

### Free Functions

| Function | Description |
|---|---|
| `fastant::is_tsc_available()` | Check if TSC acceleration is active |

## Building

```bash
# Header-only — just add the include path
cmake -B build
cmake --build build

# With tests (requires Catch2, fetched automatically)
cmake -B build -DFASTANT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build

# With benchmarks (requires Google Benchmark, fetched automatically)
cmake -B build -DFASTANT_BUILD_BENCHMARKS=ON
cmake --build build

# With examples
cmake -B build -DFASTANT_BUILD_EXAMPLES=ON
cmake --build build

# Install
cmake --install build --prefix /usr/local
```

### Requirements

- **C++23** compiler (GCC >= 14, Clang >= 19)
- **CMake** >= 3.21

## Performance

Measured on x86-64 Linux (AMD Ryzen, 3.75 GHz):

```
BM_InstantNowFastant         7.6 ns
BM_InstantNowSteadyClock    18.9 ns
BM_AnchorNew                23.5 ns
BM_AsUnixNanos             0.46 ns
```

## How It Works

On x86 Linux, the library calibrates the TSC frequency by comparing
`std::chrono::steady_clock` readings against `RDTSC` counts over a 10 ms
window, converging to within 0.001%. The calibration runs once before
`main()` via `__attribute__((constructor))`.

Once calibrated, `Instant::now()` is a single `RDTSC` instruction (~7.5 ns).
The library relies on the hardware's invariant TSC feature
(CPUID 0x80000007 EDX[8]) to ensure cross-core consistency.

On non-TSC platforms, the library falls back to
`std::chrono::steady_clock::now()` nanoseconds.

