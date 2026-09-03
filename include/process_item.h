// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PROCESS_ITEM_H_
#define LITE_PROC_MANAGER_PROCESS_ITEM_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "native_types.h"
#include "process_column.h"

namespace lite_proc_manager {

class ProcessItem {
 public:
  ProcessItem();
  ~ProcessItem() = default;

  // Basic Identity
  uint32_t process_id{0};
  uint32_t parent_process_id{0};
  std::wstring name;
  std::wstring status{L"実行中"};
  std::wstring user_name{L"-"};
  double cpu_percent{0.0};

  // Memory metrics
  uint64_t private_working_set{0};
  uint64_t working_set{0};
  uint64_t peak_working_set{0};
  int64_t working_set_delta{0};
  uint64_t commit_size{0};
  uint64_t paged_pool{0};
  uint64_t non_paged_pool{0};

  // Priority & Handles & Threads
  ProcessPriorityClass priority{ProcessPriorityClass::kNormal};
  int32_t base_priority_raw{8};
  uint32_t handle_count{0};
  uint32_t thread_count{0};
  uint32_t user_objects{0};
  uint32_t gdi_objects{0};

  // I/O metrics
  uint64_t io_read_count{0};
  uint64_t io_write_count{0};
  uint64_t io_other_count{0};
  uint64_t io_read_bytes{0};
  uint64_t io_write_bytes{0};
  uint64_t io_other_bytes{0};

  // Path & Execution Context
  std::wstring file_path;
  std::wstring command_line;
  std::wstring os_context{L"64ビット"};
  std::wstring platform{L"64ビット"};
  std::wstring elevated{L"いいえ"};
  std::wstring uac_virtualization{L"該当なし"};
  std::wstring description;
  std::wstring dep_status{L"有効"};
  std::wstring enterprise_context{L"-"};
  std::wstring dpi_awareness{L"Per-Monitor"};
  std::wstring package_name{L"-"};
  std::wstring architecture{L"x64"};

  // GPU metrics
  double gpu_percent{0.0};
  std::wstring gpu_engine{L"-"};
  uint64_t dedicated_gpu_memory{0};
  uint64_t shared_gpu_memory{0};

  // Session & Time
  uint32_t session_id{0};
  std::optional<FILETIME> start_time;

  // CPU & Delta Tracking
  int64_t previous_kernel_time{0};
  int64_t previous_user_time{0};
  uint64_t previous_cycle_time{0};
  uint64_t previous_working_set{0};

  // Tree Hierarchy
  std::vector<std::shared_ptr<ProcessItem>> children;

  // Formatting helpers
  std::wstring GetFormattedCpu() const;
  std::wstring GetFormattedWorkingSet() const;
  std::wstring GetFormattedPrivateWorkingSet() const;
  std::wstring GetFormattedPeakWorkingSet() const;
  std::wstring GetFormattedWorkingSetDelta() const;
  std::wstring GetFormattedCommitSize() const;
  std::wstring GetFormattedPagedPool() const;
  std::wstring GetFormattedNonPagedPool() const;
  std::wstring GetFormattedPriority() const;
  std::wstring GetFormattedStartTime() const;
  std::wstring GetFormattedGpu() const;
  std::wstring GetFormattedDedicatedGpuMemory() const;
  std::wstring GetFormattedSharedGpuMemory() const;
  std::wstring GetColumnValue(ProcessColumnId column_id) const;

  static std::wstring FormatBytes(uint64_t bytes);
  static std::wstring FormatDeltaBytes(int64_t delta_bytes);
  static std::wstring FormatCount(uint64_t count);
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PROCESS_ITEM_H_
