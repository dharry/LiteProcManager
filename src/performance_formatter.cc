// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "performance_formatter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <limits>

namespace lite_proc_manager {

std::wstring PerformanceFormatter::FormatUptime(uint64_t uptime_ms) {
  const uint64_t total_seconds = uptime_ms / 1000ULL;
  const uint64_t days = total_seconds / 86400ULL;
  const uint64_t hours = (total_seconds % 86400ULL) / 3600ULL;
  const uint64_t minutes = (total_seconds % 3600ULL) / 60ULL;
  const uint64_t seconds = total_seconds % 60ULL;

  wchar_t buffer[64];
  swprintf_s(buffer, L"%llu:%02llu:%02llu:%02llu",
             static_cast<unsigned long long>(days),
             static_cast<unsigned long long>(hours),
             static_cast<unsigned long long>(minutes),
             static_cast<unsigned long long>(seconds));
  return buffer;
}

std::wstring PerformanceFormatter::FormatMemoryGB(uint64_t bytes) {
  constexpr uint64_t kBytesPerMB = 1024ULL * 1024ULL;
  constexpr uint64_t kBytesPerGB = 1024ULL * 1024ULL * 1024ULL;

  if (bytes >= kBytesPerGB) {
    const double gb =
        static_cast<double>(bytes) / static_cast<double>(kBytesPerGB);
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f GB", gb);
    return buffer;
  }

  const uint64_t mb = (bytes + (kBytesPerMB / 2ULL)) / kBytesPerMB;
  if (mb >= 1024ULL) {
    return L"1.0 GB";
  }
  return std::to_wstring(mb) + L" MB";
}

std::wstring PerformanceFormatter::FormatCacheSize(uint64_t bytes,
                                                   const std::wstring& na_text) {
  if (bytes == 0) {
    return na_text;
  }

  constexpr uint64_t kBytesPerKB = 1024ULL;
  constexpr uint64_t kBytesPerMB = 1024ULL * 1024ULL;

  if (bytes >= kBytesPerMB) {
    const double mb =
        static_cast<double>(bytes) / static_cast<double>(kBytesPerMB);
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f MB", mb);
    return buffer;
  }

  const uint64_t kb =
      std::max<uint64_t>(1ULL, (bytes + (kBytesPerKB / 2ULL)) / kBytesPerKB);
  if (kb >= 1024ULL) {
    return L"1.0 MB";
  }
  return std::to_wstring(kb) + L" KB";
}

std::wstring PerformanceFormatter::FormatFrequencyGHz(uint32_t mhz) {
  const double ghz = static_cast<double>(mhz) / 1000.0;
  wchar_t buffer[64];
  swprintf_s(buffer, L"%.2f GHz", ghz);
  return buffer;
}

std::wstring PerformanceFormatter::FormatBitrate(double bits_per_sec) {
  if (!std::isfinite(bits_per_sec) || bits_per_sec <= 0.0) {
    return L"0 Kbps";
  }

  if (bits_per_sec < 1'000'000.0) {
    const long long kbps = std::llround(bits_per_sec / 1'000.0);
    if (kbps >= 1000) {
      return L"1.0 Mbps";
    }
    return std::to_wstring(kbps) + L" Kbps";
  }

  wchar_t buffer[64];
  if (bits_per_sec < 1'000'000'000.0) {
    const double mbps = bits_per_sec / 1'000'000.0;
    if (mbps >= 999.95) {
      return L"1.0 Gbps";
    }
    swprintf_s(buffer, L"%.1f Mbps", mbps);
    return buffer;
  }

  const double gbps = bits_per_sec / 1'000'000'000.0;
  swprintf_s(buffer, L"%.1f Gbps", gbps);
  return buffer;
}

std::wstring PerformanceFormatter::FormatLinkSpeed(uint64_t bits_per_sec,
                                                   const std::wstring& na_text) {
  if (bits_per_sec == 0) {
    return na_text;
  }

  if (bits_per_sec >= 1'000'000'000ULL) {
    const double gbps = static_cast<double>(bits_per_sec) / 1'000'000'000.0;
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f Gbps", gbps);
    return buffer;
  }

  if (bits_per_sec >= 1'000'000ULL) {
    const uint64_t mbps = (bits_per_sec + 500'000ULL) / 1'000'000ULL;
    if (mbps >= 1000ULL) {
      return L"1.0 Gbps";
    }
    return std::to_wstring(mbps) + L" Mbps";
  }

  const uint64_t kbps =
      std::max<uint64_t>(1ULL, (bits_per_sec + 500ULL) / 1'000ULL);
  if (kbps >= 1000ULL) {
    return L"1 Mbps";
  }
  return std::to_wstring(kbps) + L" Kbps";
}

std::wstring PerformanceFormatter::FormatIntegerWithCommas(uint64_t value) {
  std::wstring result = std::to_wstring(value);
  if (result.length() <= 3) {
    return result;
  }
  for (ptrdiff_t pos = static_cast<ptrdiff_t>(result.length()) - 3; pos > 0;
       pos -= 3) {
    result.insert(static_cast<size_t>(pos), 1, L',');
  }
  return result;
}

CpuGridLayout PerformanceFormatter::ComputeCpuGridLayout(size_t logical_cores) {
  if (logical_cores <= 1) {
    return {1, 1};
  }
  if (logical_cores == 2) {
    return {1, 2};
  }
  if (logical_cores <= 4) {
    return {2, 2};
  }
  if (logical_cores <= 6) {
    return {2, 3};
  }
  if (logical_cores <= 8) {
    return {2, 4};
  }
  if (logical_cores <= 12) {
    return {3, 4};
  }
  if (logical_cores <= 16) {
    return {4, 4};
  }

  constexpr size_t max_int =
      static_cast<size_t>(std::numeric_limits<int>::max());
  const size_t clamped_cores = std::min(logical_cores, max_int);
  int cols = static_cast<int>(
      std::ceil(std::sqrt(static_cast<double>(clamped_cores))));
  cols = std::max(cols, 1);
  int rows = static_cast<int>((clamped_cores + static_cast<size_t>(cols) - 1) /
                              static_cast<size_t>(cols));
  return {rows, cols};
}

double PerformanceFormatter::ComputeNetworkAutoScale(double peak_bps) {
  constexpr double kSteps[] = {
      100'000.0,
      500'000.0,
      1'000'000.0,
      5'000'000.0,
      10'000'000.0,
      50'000'000.0,
      100'000'000.0,
      500'000'000.0,
      1'000'000'000.0,
      10'000'000'000.0,
  };

  if (!std::isfinite(peak_bps) || peak_bps <= 0.0) {
    return kSteps[0];
  }
  for (double step : kSteps) {
    if (peak_bps <= step) {
      return step;
    }
  }
  return peak_bps * 1.25;
}

double PerformanceFormatter::ComputeUsagePercent(uint64_t active,
                                                 uint64_t total) {
  if (total == 0) {
    return 0.0;
  }
  const double percent =
      (100.0 * static_cast<double>(active)) / static_cast<double>(total);
  return std::clamp(percent, 0.0, 100.0);
}

}  // namespace lite_proc_manager
