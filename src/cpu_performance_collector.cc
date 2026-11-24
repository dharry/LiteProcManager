// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "cpu_performance_collector.h"

#include "native_types.h"

#include <intrin.h>
#include <powrprof.h>
#include <psapi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "psapi.lib")

namespace lite_proc_manager {

namespace {

constexpr ULONG kSystemProcessorPerformanceInformation = 8;

struct ProcessorPowerInfo {
  ULONG Number;
  ULONG MaxMhz;
  ULONG CurrentMhz;
  ULONG MhzLimit;
  ULONG MaxIdleState;
  ULONG CurrentIdleState;
};

struct SystemProcessorPerformanceInfo {
  LARGE_INTEGER IdleTime;
  LARGE_INTEGER KernelTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER DpcTime;
  LARGE_INTEGER InterruptTime;
  ULONG InterruptCount;
};

class ScopedRegKey {
 public:
  ScopedRegKey() = default;
  ~ScopedRegKey() { Reset(); }

  ScopedRegKey(const ScopedRegKey&) = delete;
  ScopedRegKey& operator=(const ScopedRegKey&) = delete;

  HKEY* OutParam() {
    Reset();
    return &key_;
  }

  HKEY Get() const { return key_; }
  explicit operator bool() const { return key_ != nullptr; }

  void Reset() {
    if (key_ != nullptr) {
      RegCloseKey(key_);
      key_ = nullptr;
    }
  }

 private:
  HKEY key_{nullptr};
};

std::wstring TrimWhitespace(const std::wstring& value) {
  const wchar_t* const kWhitespace = L" \t\r\n";
  const size_t first = value.find_first_not_of(kWhitespace);
  if (first == std::wstring::npos) {
    return L"";
  }
  const size_t last = value.find_last_not_of(kWhitespace);
  return value.substr(first, last - first + 1);
}

std::wstring QueryCpuidBrandName() {
  int regs[4] = {};
  __cpuid(regs, static_cast<int>(0x80000000u));
  const uint32_t max_extended_leaf = static_cast<uint32_t>(regs[0]);
  if (max_extended_leaf < 0x80000004u) {
    return L"";
  }

  char brand_ascii[49]{};
  for (uint32_t i = 0; i < 3; ++i) {
    __cpuid(regs, static_cast<int>(0x80000002u + i));
    std::memcpy(brand_ascii + i * 16, regs, 16);
  }
  brand_ascii[48] = '\0';

  const size_t ascii_len = strnlen(brand_ascii, 48);
  if (ascii_len == 0) {
    return L"";
  }

  UINT code_page = CP_UTF8;
  int wide_len = MultiByteToWideChar(
      code_page, 0, brand_ascii, static_cast<int>(ascii_len), nullptr, 0);
  if (wide_len <= 0) {
    code_page = CP_ACP;
    wide_len = MultiByteToWideChar(
        code_page, 0, brand_ascii, static_cast<int>(ascii_len), nullptr, 0);
  }
  if (wide_len <= 0) {
    return L"";
  }

  std::wstring brand_wide(static_cast<size_t>(wide_len), L'\0');
  const int written = MultiByteToWideChar(code_page,
                                          0,
                                          brand_ascii,
                                          static_cast<int>(ascii_len),
                                          brand_wide.data(),
                                          wide_len);
  if (written <= 0) {
    return L"";
  }

  return TrimWhitespace(brand_wide);
}

std::wstring ReadRegistryCpuString(const wchar_t* value_name) {
  ScopedRegKey key;
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
          0,
          KEY_READ,
          key.OutParam()) != ERROR_SUCCESS) {
    return L"";
  }

  DWORD type = 0;
  DWORD byte_size = 0;
  if (RegQueryValueExW(
          key.Get(), value_name, nullptr, &type, nullptr, &byte_size) !=
      ERROR_SUCCESS) {
    return L"";
  }
  if ((type != REG_SZ && type != REG_EXPAND_SZ) || byte_size == 0) {
    return L"";
  }

  std::vector<wchar_t> buffer(byte_size / sizeof(wchar_t) + 2, L'\0');
  DWORD actual_bytes =
      static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
  if (RegQueryValueExW(key.Get(),
                       value_name,
                       nullptr,
                       &type,
                       reinterpret_cast<LPBYTE>(buffer.data()),
                       &actual_bytes) != ERROR_SUCCESS) {
    return L"";
  }

  return TrimWhitespace(std::wstring(buffer.data()));
}

uint32_t ReadRegistryCpuDword(const wchar_t* value_name) {
  ScopedRegKey key;
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
          0,
          KEY_READ,
          key.OutParam()) != ERROR_SUCCESS) {
    return 0;
  }

  DWORD value = 0;
  DWORD type = 0;
  DWORD byte_size = sizeof(value);
  if (RegQueryValueExW(key.Get(),
                       value_name,
                       nullptr,
                       &type,
                       reinterpret_cast<LPBYTE>(&value),
                       &byte_size) != ERROR_SUCCESS) {
    return 0;
  }
  if (type != REG_DWORD || byte_size != sizeof(DWORD)) {
    return 0;
  }
  return static_cast<uint32_t>(value);
}

std::vector<ProcessorPowerInfo> QueryProcessorPowerInfo(
    uint32_t logical_processor_count) {
  size_t entry_count = std::max<uint32_t>(1u, logical_processor_count);
  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    std::vector<ProcessorPowerInfo> buffer(entry_count);
    const ULONG buffer_bytes =
        static_cast<ULONG>(buffer.size() * sizeof(ProcessorPowerInfo));
    const NTSTATUS status = CallNtPowerInformation(
        ProcessorInformation, nullptr, 0, buffer.data(), buffer_bytes);
    if (status == kStatusSuccess) {
      return buffer;
    }
    if (status != kStatusBufferTooSmall) {
      break;
    }
    entry_count *= 2;
  }
  return {};
}

}  // namespace

CpuPerformanceCollector::CpuPerformanceCollector() = default;

CpuStaticInfo CpuPerformanceCollector::GetStaticInfo() {
  if (!static_info_cached_) {
    cached_static_info_ = QueryStaticInfo();
    static_info_cached_ = true;
  }
  return cached_static_info_;
}

CpuStaticInfo CpuPerformanceCollector::QueryStaticInfo() {
  CpuStaticInfo info;

  std::wstring brand = QueryCpuidBrandName();
  if (brand.empty()) {
    brand = ReadRegistryCpuString(L"ProcessorNameString");
  }
  if (!brand.empty()) {
    info.brand_name = std::move(brand);
  }

  int cpuid_regs[4] = {};
  __cpuid(cpuid_regs, 1);
  info.is_virtual_machine =
      (static_cast<uint32_t>(cpuid_regs[2]) & (1u << 31)) != 0;

  const DWORD active_processors =
      GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  info.logical_processor_count =
      std::max<uint32_t>(1u, static_cast<uint32_t>(active_processors));

  DWORD buffer_bytes = 0;
  if (!GetLogicalProcessorInformationEx(RelationAll, nullptr, &buffer_bytes) &&
      GetLastError() == ERROR_INSUFFICIENT_BUFFER && buffer_bytes > 0) {
    std::vector<uint8_t> buffer(buffer_bytes);
    if (GetLogicalProcessorInformationEx(
            RelationAll,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data()),
            &buffer_bytes)) {
      uint32_t sockets = 0;
      uint32_t cores = 0;
      uint64_t l1 = 0;
      uint64_t l2 = 0;
      uint64_t l3 = 0;

      const DWORD header_bytes = static_cast<DWORD>(
          offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size) +
          sizeof(DWORD));
      DWORD offset = 0;
      while (offset + header_bytes <= buffer_bytes) {
        const auto* entry =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                buffer.data() + offset);
        if (entry->Size < header_bytes || offset + entry->Size > buffer_bytes) {
          break;
        }

        switch (entry->Relationship) {
          case RelationProcessorPackage:
            ++sockets;
            break;
          case RelationProcessorCore:
            ++cores;
            break;
          case RelationCache:
            if (entry->Size >=
                offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Cache) +
                    sizeof(CACHE_RELATIONSHIP)) {
              if (entry->Cache.Level == 1) {
                l1 += entry->Cache.CacheSize;
              } else if (entry->Cache.Level == 2) {
                l2 += entry->Cache.CacheSize;
              } else if (entry->Cache.Level == 3) {
                l3 += entry->Cache.CacheSize;
              }
            }
            break;
          default:
            break;
        }

        offset += entry->Size;
      }

      info.socket_count = std::max<uint32_t>(1u, sockets);
      info.physical_core_count = std::max<uint32_t>(1u, cores);
      info.l1_cache_bytes = l1;
      info.l2_cache_bytes = l2;
      info.l3_cache_bytes = l3;
    }
  }

  const auto power_info =
      QueryProcessorPowerInfo(info.logical_processor_count);
  if (!power_info.empty() && power_info[0].MaxMhz > 0) {
    info.base_speed_mhz = static_cast<uint32_t>(power_info[0].MaxMhz);
  } else {
    info.base_speed_mhz = ReadRegistryCpuDword(L"~MHz");
  }

  return info;
}

CpuSample CpuPerformanceCollector::Sample() {
  const CpuStaticInfo static_info = GetStaticInfo();
  CpuSample sample;

  std::vector<CoreTimeSample> current_times;
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll != nullptr) {
    const auto pfn_nt_query =
        reinterpret_cast<PfnNtQuerySystemInformation>(
            GetProcAddress(ntdll, "NtQuerySystemInformation"));
    if (pfn_nt_query != nullptr) {
      size_t entry_count =
          std::max<uint32_t>(1u, static_info.logical_processor_count);
      constexpr int kMaxAttempts = 4;
      for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::vector<SystemProcessorPerformanceInfo> perf_info(entry_count);
        const ULONG buffer_bytes = static_cast<ULONG>(
            perf_info.size() * sizeof(SystemProcessorPerformanceInfo));
        ULONG return_length = 0;
        const NTSTATUS status = pfn_nt_query(
            kSystemProcessorPerformanceInformation,
            perf_info.data(),
            buffer_bytes,
            &return_length);
        if (status == kStatusSuccess) {
          const ULONG actual_bytes =
              (return_length > 0 && return_length <= buffer_bytes)
                  ? return_length
                  : buffer_bytes;
          const size_t core_count =
              actual_bytes / sizeof(SystemProcessorPerformanceInfo);
          current_times.resize(core_count);
          for (size_t i = 0; i < core_count; ++i) {
            const auto& info = perf_info[i];
            current_times[i].idle_time =
                info.IdleTime.QuadPart > 0
                    ? static_cast<uint64_t>(info.IdleTime.QuadPart)
                    : 0;
            current_times[i].kernel_time =
                info.KernelTime.QuadPart > 0
                    ? static_cast<uint64_t>(info.KernelTime.QuadPart)
                    : 0;
            current_times[i].user_time =
                info.UserTime.QuadPart > 0
                    ? static_cast<uint64_t>(info.UserTime.QuadPart)
                    : 0;
          }
          break;
        }
        if (status != kStatusInfoLengthMismatch &&
            status != kStatusBufferTooSmall) {
          break;
        }
        if (return_length > buffer_bytes) {
          entry_count =
              (return_length + sizeof(SystemProcessorPerformanceInfo) - 1) /
              sizeof(SystemProcessorPerformanceInfo);
        } else {
          entry_count *= 2;
        }
      }
    }
  }

  if (current_times.empty()) {
    sample.per_core_usage_percent.assign(static_info.logical_processor_count,
                                         0.0);
    sample.overall_usage_percent = 0.0;
  } else if (prev_core_times_.size() != current_times.size()) {
    prev_core_times_ = std::move(current_times);
    sample.per_core_usage_percent.assign(prev_core_times_.size(), 0.0);
    sample.overall_usage_percent = 0.0;
  } else {
    sample.per_core_usage_percent.resize(current_times.size());
    double usage_sum = 0.0;
    for (size_t i = 0; i < current_times.size(); ++i) {
      const double core_usage = ComputeCoreUsageDelta(
          prev_core_times_[i].idle_time,
          prev_core_times_[i].kernel_time,
          prev_core_times_[i].user_time,
          current_times[i].idle_time,
          current_times[i].kernel_time,
          current_times[i].user_time);
      sample.per_core_usage_percent[i] = core_usage;
      usage_sum += core_usage;
    }
    sample.overall_usage_percent = std::clamp(
        usage_sum / static_cast<double>(current_times.size()), 0.0, 100.0);
    prev_core_times_ = std::move(current_times);
  }

  const auto power_info =
      QueryProcessorPowerInfo(static_info.logical_processor_count);
  if (!power_info.empty() && power_info[0].CurrentMhz > 0) {
    sample.current_speed_mhz = static_cast<uint32_t>(power_info[0].CurrentMhz);
  } else {
    sample.current_speed_mhz = static_info.base_speed_mhz;
  }

  PERFORMANCE_INFORMATION perf_counters{};
  perf_counters.cb = sizeof(perf_counters);
  if (GetPerformanceInfo(&perf_counters, sizeof(perf_counters))) {
    sample.process_count = static_cast<uint32_t>(perf_counters.ProcessCount);
    sample.thread_count = static_cast<uint32_t>(perf_counters.ThreadCount);
    sample.handle_count = static_cast<uint32_t>(perf_counters.HandleCount);
  }

  sample.uptime_ms = GetTickCount64();
  return sample;
}

double CpuPerformanceCollector::ComputeCoreUsageDelta(uint64_t prev_idle,
                                                      uint64_t prev_kernel,
                                                      uint64_t prev_user,
                                                      uint64_t cur_idle,
                                                      uint64_t cur_kernel,
                                                      uint64_t cur_user) {
  if (cur_idle < prev_idle || cur_kernel < prev_kernel ||
      cur_user < prev_user) {
    return 0.0;
  }

  const uint64_t delta_idle = cur_idle - prev_idle;
  const uint64_t delta_kernel = cur_kernel - prev_kernel;
  const uint64_t delta_user = cur_user - prev_user;

  const uint64_t total = delta_kernel + delta_user;
  if (total == 0) {
    return 0.0;
  }

  const uint64_t active_kernel =
      (delta_kernel >= delta_idle) ? (delta_kernel - delta_idle) : 0;
  const uint64_t active = active_kernel + delta_user;
  return std::clamp(
      100.0 * static_cast<double>(active) / static_cast<double>(total),
      0.0,
      100.0);
}

}  // namespace lite_proc_manager
