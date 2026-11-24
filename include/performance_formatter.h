// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PERFORMANCE_FORMATTER_H_
#define LITE_PROC_MANAGER_PERFORMANCE_FORMATTER_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace lite_proc_manager {

struct CpuGridLayout {
  int rows{1};
  int cols{1};
};

// Pure formatting and layout utilities for the Performance tab.
class PerformanceFormatter {
 public:
  static std::wstring FormatUptime(uint64_t uptime_ms);
  static std::wstring FormatMemoryGB(uint64_t bytes);
  static std::wstring FormatCacheSize(uint64_t bytes,
                                      const std::wstring& na_text = L"N/A");
  static std::wstring FormatFrequencyGHz(uint32_t mhz);
  static std::wstring FormatBitrate(double bits_per_sec);
  static std::wstring FormatLinkSpeed(uint64_t bits_per_sec,
                                      const std::wstring& na_text = L"N/A");
  static std::wstring FormatIntegerWithCommas(uint64_t value);

  static CpuGridLayout ComputeCpuGridLayout(size_t logical_cores);
  static double ComputeNetworkAutoScale(double peak_bps);
  static double ComputeUsagePercent(uint64_t active, uint64_t total);
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PERFORMANCE_FORMATTER_H_
