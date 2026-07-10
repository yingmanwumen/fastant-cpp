cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(fastant
  VERSION 2.0.0
  LANGUAGES CXX
)

include(GNUInstallDirs)

add_library(fastant INTERFACE)
add_library(fastant::fastant ALIAS fastant)

target_compile_features(fastant INTERFACE cxx_std_23)

target_include_directories(fastant
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(FASTANT_BUILD_TESTS "Build fastant tests" OFF)
option(FASTANT_BUILD_BENCHMARKS "Build fastant benchmarks" OFF)
option(FASTANT_BUILD_EXAMPLES "Build fastant examples" OFF)

# Tests
if(FASTANT_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
  enable_testing()
  add_subdirectory(tests)
endif()

# Benchmarks
if(FASTANT_BUILD_BENCHMARKS)
  include(FetchContent)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.2
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
  add_subdirectory(bench)
endif()

# Examples
if(FASTANT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Install headers
install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install library target (INTERFACE) and export targets
install(
  TARGETS fastant
  EXPORT fastantTargets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Generate and install CMake package config files
include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fastantConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/fastantConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastant
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/fastantConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  EXPORT fastantTargets
  FILE fastantTargets.cmake
  NAMESPACE fastant::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastant
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/fastantConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/fastantConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fastant
)
