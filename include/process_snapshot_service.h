// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PROCESS_SNAPSHOT_SERVICE_H_
#define LITE_PROC_MANAGER_PROCESS_SNAPSHOT_SERVICE_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "native_types.h"
#include "process_item.h"

namespace lite_proc_manager {

struct SystemTotals {
  int total_processes{0};
  uint32_t total_threads{0};
  uint32_t total_handles{0};
  double total_cpu_usage{0.0};
  uint64_t total_physical_memory{0};
  uint64_t used_physical_memory{0};
  uint64_t total_commit_limit{0};
  uint64_t total_committed{0};
};

struct SnapshotResult {
  std::vector<std::shared_ptr<ProcessItem>> items;
  SystemTotals totals;
};

class ProcessSnapshotService {
 public:
  ProcessSnapshotService();
  ~ProcessSnapshotService();

  SnapshotResult GetSnapshot();

  static bool TerminateProcessById(uint32_t pid);
  bool TerminateProcessTree(uint32_t root_pid, const std::vector<std::shared_ptr<ProcessItem>>& all_items);
  static bool SetPriority(uint32_t pid, ProcessPriorityClass priority);
  static std::vector<std::shared_ptr<ProcessItem>> BuildProcessTree(const std::vector<std::shared_ptr<ProcessItem>>& flat_items);

 private:
  struct ProcessTimeTrack {
    int64_t kernel_time{0};
    int64_t user_time{0};
    uint64_t cycle_time{0};
    uint64_t working_set{0};
  };

  struct ProcessStaticCache {
    std::optional<FILETIME> start_time;
    std::wstring file_path;
    std::wstring command_line;
    std::wstring description;
    std::wstring user_name{L"-"};
    std::wstring architecture{L"x64"};
    std::wstring platform{L"64ビット"};
    std::wstring os_context{L"64ビット"};
    std::wstring elevated{L"いいえ"};
    std::wstring uac_virtualization{L"該当なし"};
    std::wstring dep_status{L"有効"};
    std::wstring enterprise_context{L"-"};
    std::wstring dpi_awareness{L"Per-Monitor"};
    std::wstring package_name{L"-"};
    ProcessPriorityClass priority{ProcessPriorityClass::kNormal};
  };

  void EnrichProcessDetails(ProcessItem* item);
  static std::wstring QueryProcessUser(HANDLE process_handle);

  PfnNtQuerySystemInformation pfn_nt_query_system_information_{nullptr};
  HMODULE ntdll_module_{nullptr};

  void* buffer_{nullptr};
  uint32_t buffer_size_{1024 * 1024};  // Initial 1MB

  std::unordered_map<uint32_t, ProcessStaticCache> static_cache_;
  std::unordered_map<uint32_t, ProcessTimeTrack> previous_times_;

  // Accurate System Times Tracking
  uint64_t prev_sys_idle_time_{0};
  uint64_t prev_sys_kernel_time_{0};
  uint64_t prev_sys_user_time_{0};
  bool has_prev_sys_times_{false};

  int processor_count_{1};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PROCESS_SNAPSHOT_SERVICE_H_
