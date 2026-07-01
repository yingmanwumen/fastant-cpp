add_executable(bench_instant bench_instant.cpp)
target_compile_features(bench_instant PRIVATE cxx_std_23)
target_link_libraries(bench_instant PRIVATE fastant::fastant benchmark::benchmark)
