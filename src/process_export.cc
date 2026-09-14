// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "process_export.h"

#include <sstream>

#include "json_helper.h"
#include "tsv_helper.h"

namespace lite_proc_manager {

namespace {

const wchar_t* GetColumnJsonKey(ProcessColumnId id) {
  switch (id) {
    case ProcessColumnId::kName: return L"name";
    case ProcessColumnId::kPid: return L"pid";
    case ProcessColumnId::kStatus: return L"status";
    case ProcessColumnId::kUserName: return L"user_name";
    case ProcessColumnId::kCpu: return L"cpu_percent";
    case ProcessColumnId::kPrivateWorkingSet: return L"private_working_set";
    case ProcessColumnId::kWorkingSet: return L"working_set";
    case ProcessColumnId::kPeakWorkingSet: return L"peak_working_set";
    case ProcessColumnId::kWorkingSetDelta: return L"working_set_delta";
    case ProcessColumnId::kCommitSize: return L"commit_size";
    case ProcessColumnId::kPagedPool: return L"paged_pool";
    case ProcessColumnId::kNonPagedPool: return L"non_paged_pool";
    case ProcessColumnId::kBasePriority: return L"base_priority";
    case ProcessColumnId::kHandles: return L"handles";
    case ProcessColumnId::kThreads: return L"threads";
    case ProcessColumnId::kUserObjects: return L"user_objects";
    case ProcessColumnId::kGdiObjects: return L"gdi_objects";
    case ProcessColumnId::kIoReadCount: return L"io_read_count";
    case ProcessColumnId::kIoWriteCount: return L"io_write_count";
    case ProcessColumnId::kIoOtherCount: return L"io_other_count";
    case ProcessColumnId::kIoReadBytes: return L"io_read_bytes";
    case ProcessColumnId::kIoWriteBytes: return L"io_write_bytes";
    case ProcessColumnId::kIoOtherBytes: return L"io_other_bytes";
    case ProcessColumnId::kFilePath: return L"file_path";
    case ProcessColumnId::kCommandLine: return L"command_line";
    case ProcessColumnId::kOsContext: return L"os_context";
    case ProcessColumnId::kPlatform: return L"platform";
    case ProcessColumnId::kElevated: return L"elevated";
    case ProcessColumnId::kUacVirtualization: return L"uac_virtualization";
    case ProcessColumnId::kDescription: return L"description";
    case ProcessColumnId::kDepStatus: return L"dep_status";
    case ProcessColumnId::kEnterpriseContext: return L"enterprise_context";
    case ProcessColumnId::kDpiAwareness: return L"dpi_awareness";
    case ProcessColumnId::kPackageName: return L"package_name";
    case ProcessColumnId::kArchitecture: return L"architecture";
    case ProcessColumnId::kGpuUsage: return L"gpu_usage";
    case ProcessColumnId::kGpuEngine: return L"gpu_engine";
    case ProcessColumnId::kDedicatedGpuMemory: return L"dedicated_gpu_memory";
    case ProcessColumnId::kSharedGpuMemory: return L"shared_gpu_memory";
    case ProcessColumnId::kSessionId: return L"session_id";
    case ProcessColumnId::kCreateTime: return L"create_time";
    default: return L"unknown";
  }
}

std::vector<ProcessColumnInfo> GetVisibleColumns(
    const std::vector<ProcessColumnInfo>& columns) {
  std::vector<ProcessColumnInfo> visible_columns;
  for (const auto& column : columns) {
    if (column.visible) {
      visible_columns.push_back(column);
    }
  }
  return visible_columns;
}

}  // namespace

std::wstring BuildProcessJson(
    const std::vector<std::shared_ptr<ProcessItem>>& processes,
    const std::vector<ProcessColumnInfo>& columns) {
  const auto visible_columns = GetVisibleColumns(columns);
  JsonArray root_array;
  for (const auto& process : processes) {
    JsonObject object;
    for (const auto& column : visible_columns) {
      object.push_back({GetColumnJsonKey(column.id),
                        JsonValue(process->GetColumnValue(column.id))});
    }
    root_array.push_back(JsonValue(object));
  }
  return JsonValue(root_array).Serialize(2);
}

std::wstring BuildProcessTsv(
    const std::vector<std::shared_ptr<ProcessItem>>& processes,
    const std::vector<ProcessColumnInfo>& columns) {
  const auto visible_columns = GetVisibleColumns(columns);
  std::wostringstream output;
  for (size_t index = 0; index < visible_columns.size(); ++index) {
    if (index > 0) output << L'\t';
    output << SanitizeTsvCell(visible_columns[index].header_text);
  }
  output << L"\r\n";

  for (const auto& process : processes) {
    for (size_t index = 0; index < visible_columns.size(); ++index) {
      if (index > 0) output << L'\t';
      output << SanitizeTsvCell(
          process->GetColumnValue(visible_columns[index].id));
    }
    output << L"\r\n";
  }
  return output.str();
}

}  // namespace lite_proc_manager
