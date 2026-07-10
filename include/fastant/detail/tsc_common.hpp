#pragma once

/// @file tsc_common.hpp
/// @brief Shared TSC capability probes used by both clock backends.

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))

#include <cerrno>
#include <fstream>
#include <iterator>
#include <string>

#include <cpuid.h>

namespace fastant::detail::tsc_common {

/// @brief Check for invariant TSC via CPUID 0x80000007 EDX[8].
inline bool has_invariant_tsc() noexcept {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) == 0) return false;
  if (eax < 0x80000007) return false;
  __get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
  return (edx & (1u << 8)) != 0;
}

/// @brief Check whether the kernel's current clocksource is TSC.
inline bool clock_source_has_tsc() noexcept {
  {
    std::ifstream file(
        "/sys/devices/system/clocksource/clocksource0/current_clocksource");
    if (file.is_open()) {
      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      return content.find("tsc") != std::string::npos;
    }
    if (errno != ENOENT) return false;
  }
  {
    std::ifstream avail(
        "/sys/devices/system/clocksource/clocksource0/available_clocksource");
    if (avail.is_open()) {
      std::string content((std::istreambuf_iterator<char>(avail)),
                          std::istreambuf_iterator<char>());
      return content.find("tsc") != std::string::npos;
    }
  }
  return false;
}

/// @brief TSC is usable when the hardware supports invariant TSC or the
/// kernel is actively using it as the clocksource.
inline bool is_tsc_stable() noexcept {
  return has_invariant_tsc() || clock_source_has_tsc();
}

}  // namespace fastant::detail::tsc_common

#endif  // __linux__ && (__x86_64__ || __i386__)
