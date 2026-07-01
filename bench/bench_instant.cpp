#include <benchmark/benchmark.h>

#include "fastant/fastant.hpp"

// Benchmark 1: Instant::now() — fastant vs std::steady_clock
static void BM_InstantNowFastant(benchmark::State& state) {
  for (auto _ : state) {
    auto t = fastant::Instant::now();
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_InstantNowFastant);

static void BM_InstantNowSteadyClock(benchmark::State& state) {
  for (auto _ : state) {
    auto t = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_InstantNowSteadyClock);

// Benchmark 2: Anchor::new()
static void BM_AnchorNew(benchmark::State& state) {
  for (auto _ : state) {
    auto a = fastant::Anchor::new_anchor();
    benchmark::DoNotOptimize(a);
  }
}
BENCHMARK(BM_AnchorNew);

// Benchmark 3: as_unix_nanos
static void BM_AsUnixNanos(benchmark::State& state) {
  auto anchor = fastant::Anchor::new_anchor();
  auto instant = fastant::Instant::now();
  for (auto _ : state) {
    auto ns = instant.as_unix_nanos(anchor);
    benchmark::DoNotOptimize(ns);
  }
}
BENCHMARK(BM_AsUnixNanos);

BENCHMARK_MAIN();
