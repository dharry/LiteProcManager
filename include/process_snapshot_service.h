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

struct ProcessSnapshotOptions {
  bool query_priority{true};
  bool query_user_objects{true};
  bool query_gdi_objects{true};

  static ProcessSnapshotOptions None() {
    return {false, false, false};
  }

  void IncludeColumn(ProcessColumnId column_id) {
    switch (column_id) {
      case ProcessColumnId::kBasePriority:
        query_priority = true;
        break;
      case ProcessColumnId::kUserObjects:
        query_user_objects = true;
        break;
      case ProcessColumnId::kGdiObjects:
        query_gdi_objects = true;
        break;
      default:
        break;
    }
  }
};

class ProcessSnapshotService {
 public:
  ProcessSnapshotService();
  ~ProcessSnapshotService();

  SnapshotResult GetSnapshot(const ProcessSnapshotOptions& options = {});

  static bool TerminateProcess(const ProcessItem& process);
  bool TerminateProcessTree(
      const ProcessItem& root_process,
      const std::vector<std::shared_ptr<ProcessItem>>& all_items);
  struct ProcessTimeTrack {
    std::optional<FILETIME> start_time;
    int64_t kernel_time{0};
    int64_t user_time{0};
    uint64_t cycle_time{0};
    uint64_t working_set{0};
  };

  static bool SetPriority(const ProcessItem& process, ProcessPriorityClass priority);
  static std::vector<std::shared_ptr<ProcessItem>> BuildProcessTree(const std::vector<std::shared_ptr<ProcessItem>>& flat_items);
  static void ApplyProcessMetricsDelta(
      const ProcessTimeTrack* previous,
      const ProcessTimeTrack& current,
      uint64_t delta_sys_total,
      ProcessItem* item);

 private:

  struct ProcessStaticCache {
    std::optional<FILETIME> start_time;
    std::wstring file_path;
    std::wstring command_line;
    std::wstring description;
    std::wstring user_name{L"-"};
    std::wstring architecture{L"x64"};
    ProcessBitness platform{ProcessBitness::k64Bit};
    ProcessBitness os_context{ProcessBitness::k64Bit};
    bool elevated{false};
    ProcessPolicyStatus uac_virtualization{
        ProcessPolicyStatus::kNotApplicable};
    ProcessPolicyStatus dep_status{ProcessPolicyStatus::kEnabled};
    std::wstring enterprise_context{L"-"};
    std::wstring dpi_awareness{L"-"};
    std::wstring package_name{L"-"};
    ProcessPriorityClass priority{ProcessPriorityClass::kNormal};
  };

  void EnrichProcessDetails(ProcessItem* item, const ProcessSnapshotOptions& options);
  static std::wstring QueryProcessUser(HANDLE token_handle);
  static std::wstring QueryProcessCommandLine(HANDLE process_handle);
  std::wstring QueryProcessDpiAwareness(HANDLE process_handle) const;

  using PfnGetProcessDpiAwareness = HRESULT(WINAPI*)(HANDLE hProcess, int* value);

  PfnNtQuerySystemInformation pfn_nt_query_system_information_{nullptr};
  HMODULE ntdll_module_{nullptr};
  HMODULE loaded_shcore_module_{nullptr};
  PfnGetProcessDpiAwareness pfn_get_process_dpi_awareness_{nullptr};

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
