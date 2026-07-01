add_executable(test_instant test_instant.cpp)

target_compile_features(test_instant PRIVATE cxx_std_23)

target_link_libraries(test_instant PRIVATE fastant::fastant Catch2::Catch2WithMain)

include(Catch)

catch_discover_tests(test_instant)
