#include <benchmark/benchmark.h>

#include "fastant/fastant.hpp"

// Benchmark 1: Instant::now() — static vs online vs std::steady_clock
static void BM_StaticInstantNow(benchmark::State& state) {
  for (auto _ : state) {
    auto t = fastant::static_clock::Instant::now();
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_StaticInstantNow);

// Online RDTSC backend
static void BM_OnlineInstantNow(benchmark::State& state) {
  for (auto _ : state) {
    auto t = fastant::online::Instant::now();
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_OnlineInstantNow);

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
    auto a = fastant::static_clock::Anchor::new_anchor();
    benchmark::DoNotOptimize(a);
  }
}
BENCHMARK(BM_AnchorNew);

BENCHMARK_MAIN();
