// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "process_item.h"

#include <iomanip>
#include <sstream>
#include <cwchar>

#include "language_manager.h"

namespace lite_proc_manager {

ProcessItem::ProcessItem() = default;

std::wstring ProcessItem::GetFormattedCpu() const {
  if (cpu_percent < 0.05) {
    return L"0.0 %";
  }
  wchar_t buffer[32];
  swprintf_s(buffer, L"%.1f %%", cpu_percent);
  return buffer;
}

std::wstring ProcessItem::GetFormattedWorkingSet() const {
  return FormatBytes(working_set);
}

std::wstring ProcessItem::GetFormattedPrivateWorkingSet() const {
  return FormatBytes(private_working_set > 0 ? private_working_set : working_set);
}

std::wstring ProcessItem::GetFormattedPeakWorkingSet() const {
  return FormatBytes(peak_working_set);
}

std::wstring ProcessItem::GetFormattedWorkingSetDelta() const {
  return FormatDeltaBytes(working_set_delta);
}

std::wstring ProcessItem::GetFormattedCommitSize() const {
  return FormatBytes(commit_size);
}

std::wstring ProcessItem::GetFormattedPagedPool() const {
  return FormatBytes(paged_pool);
}

std::wstring ProcessItem::GetFormattedNonPagedPool() const {
  return FormatBytes(non_paged_pool);
}

std::wstring ProcessItem::GetFormattedPriority() const {
  switch (priority) {
    case ProcessPriorityClass::kRealtime:
      return LanguageManager::GetString(StringId::kPriorityRealtime);
    case ProcessPriorityClass::kHigh:
      return LanguageManager::GetString(StringId::kPriorityHigh);
    case ProcessPriorityClass::kAboveNormal:
      return LanguageManager::GetString(StringId::kPriorityAboveNormal);
    case ProcessPriorityClass::kNormal:
      return LanguageManager::GetString(StringId::kPriorityNormal);
    case ProcessPriorityClass::kBelowNormal:
      return LanguageManager::GetString(StringId::kPriorityBelowNormal);
    case ProcessPriorityClass::kIdle:
      return LanguageManager::GetString(StringId::kPriorityLow);
    default:
      return LanguageManager::GetString(StringId::kPriorityNormal);
  }
}

std::wstring ProcessItem::GetFormattedStartTime() const {
  if (!start_time.has_value()) {
    return L"-";
  }

  FILETIME ft_local;
  FileTimeToLocalFileTime(&start_time.value(), &ft_local);

  SYSTEMTIME st;
  if (!FileTimeToSystemTime(&ft_local, &st)) {
    return L"-";
  }

  wchar_t buffer[64];
  swprintf_s(buffer, L"%04d/%02d/%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return buffer;
}

std::wstring ProcessItem::GetFormattedGpu() const {
  if (gpu_percent < 0.05) {
    return L"0.0 %";
  }
  wchar_t buffer[32];
  swprintf_s(buffer, L"%.1f %%", gpu_percent);
  return buffer;
}

namespace {

std::wstring FormatWithCommas(uint64_t val) {
  std::wstring s = std::to_wstring(val);
  int insert_pos = static_cast<int>(s.length()) - 3;
  while (insert_pos > 0) {
    s.insert(insert_pos, L",");
    insert_pos -= 3;
  }
  return s;
}

}  // namespace

std::wstring ProcessItem::GetFormattedDedicatedGpuMemory() const {
  return dedicated_gpu_memory > 0 ? FormatBytes(dedicated_gpu_memory) : L"0 K";
}

std::wstring ProcessItem::GetFormattedSharedGpuMemory() const {
  return shared_gpu_memory > 0 ? FormatBytes(shared_gpu_memory) : L"0 K";
}

std::wstring ProcessItem::FormatBytes(uint64_t bytes) {
  if (bytes == 0) {
    return L"0 K";
  }

  // Convert bytes to KiB (round up so non-zero values show at least 1 K)
  uint64_t kb = (bytes + 1023) / 1024;
  return FormatWithCommas(kb) + L" K";
}

std::wstring ProcessItem::FormatDeltaBytes(int64_t delta_bytes) {
  if (delta_bytes == 0) {
    return L"0 K";
  }

  wchar_t sign = (delta_bytes > 0) ? L'+' : L'-';
  uint64_t abs_bytes = (delta_bytes > 0) ? static_cast<uint64_t>(delta_bytes)
                                        : static_cast<uint64_t>(-delta_bytes);

  uint64_t kb = (abs_bytes + 1023) / 1024;
  std::wstring s;
  s += sign;
  s += FormatWithCommas(kb);
  s += L" K";
  return s;
}

std::wstring ProcessItem::FormatCount(uint64_t count) {
  return FormatWithCommas(count);
}

std::wstring ProcessItem::GetColumnValue(ProcessColumnId column_id) const {
  switch (column_id) {
    case ProcessColumnId::kName:
      return name;
    case ProcessColumnId::kPid:
      return std::to_wstring(process_id);
    case ProcessColumnId::kStatus:
      return status;
    case ProcessColumnId::kUserName:
      return user_name;
    case ProcessColumnId::kCpu:
      return GetFormattedCpu();
    case ProcessColumnId::kPrivateWorkingSet:
      return GetFormattedPrivateWorkingSet();
    case ProcessColumnId::kWorkingSet:
      return GetFormattedWorkingSet();
    case ProcessColumnId::kPeakWorkingSet:
      return GetFormattedPeakWorkingSet();
    case ProcessColumnId::kWorkingSetDelta:
      return GetFormattedWorkingSetDelta();
    case ProcessColumnId::kCommitSize:
      return GetFormattedCommitSize();
    case ProcessColumnId::kPagedPool:
      return GetFormattedPagedPool();
    case ProcessColumnId::kNonPagedPool:
      return GetFormattedNonPagedPool();
    case ProcessColumnId::kBasePriority:
      return GetFormattedPriority();
    case ProcessColumnId::kHandles:
      return FormatCount(handle_count);
    case ProcessColumnId::kThreads:
      return std::to_wstring(thread_count);
    case ProcessColumnId::kUserObjects:
      return std::to_wstring(user_objects);
    case ProcessColumnId::kGdiObjects:
      return std::to_wstring(gdi_objects);
    case ProcessColumnId::kIoReadCount:
      return FormatCount(io_read_count);
    case ProcessColumnId::kIoWriteCount:
      return FormatCount(io_write_count);
    case ProcessColumnId::kIoOtherCount:
      return FormatCount(io_other_count);
    case ProcessColumnId::kIoReadBytes:
      return FormatBytes(io_read_bytes);
    case ProcessColumnId::kIoWriteBytes:
      return FormatBytes(io_write_bytes);
    case ProcessColumnId::kIoOtherBytes:
      return FormatBytes(io_other_bytes);
    case ProcessColumnId::kFilePath:
      return file_path;
    case ProcessColumnId::kCommandLine:
      return command_line;
    case ProcessColumnId::kOsContext:
      return os_context;
    case ProcessColumnId::kPlatform:
      return platform;
    case ProcessColumnId::kElevated:
      return elevated;
    case ProcessColumnId::kUacVirtualization:
      return uac_virtualization;
    case ProcessColumnId::kDescription:
      return description;
    case ProcessColumnId::kDepStatus:
      return dep_status;
    case ProcessColumnId::kEnterpriseContext:
      return enterprise_context;
    case ProcessColumnId::kDpiAwareness:
      return dpi_awareness;
    case ProcessColumnId::kPackageName:
      return package_name;
    case ProcessColumnId::kArchitecture:
      return architecture;
    case ProcessColumnId::kGpuUsage:
      return GetFormattedGpu();
    case ProcessColumnId::kGpuEngine:
      return gpu_engine;
    case ProcessColumnId::kDedicatedGpuMemory:
      return GetFormattedDedicatedGpuMemory();
    case ProcessColumnId::kSharedGpuMemory:
      return GetFormattedSharedGpuMemory();
    case ProcessColumnId::kSessionId:
      return std::to_wstring(session_id);
    case ProcessColumnId::kCreateTime:
      return GetFormattedStartTime();
    default:
      return L"";
  }
}

}  // namespace lite_proc_manager
