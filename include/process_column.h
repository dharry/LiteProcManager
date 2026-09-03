// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PROCESS_COLUMN_H_
#define LITE_PROC_MANAGER_PROCESS_COLUMN_H_

#include <string>
#include <vector>

namespace lite_proc_manager {

enum class ProcessColumnId {
  kName,
  kPid,
  kStatus,
  kUserName,
  kCpu,
  kPrivateWorkingSet,
  kWorkingSet,
  kPeakWorkingSet,
  kWorkingSetDelta,
  kCommitSize,
  kPagedPool,
  kNonPagedPool,
  kBasePriority,
  kHandles,
  kThreads,
  kUserObjects,
  kGdiObjects,
  kIoReadCount,
  kIoWriteCount,
  kIoOtherCount,
  kIoReadBytes,
  kIoWriteBytes,
  kIoOtherBytes,
  kFilePath,
  kCommandLine,
  kOsContext,
  kPlatform,
  kElevated,
  kUacVirtualization,
  kDescription,
  kDepStatus,
  kEnterpriseContext,
  kDpiAwareness,
  kPackageName,
  kArchitecture,
  kGpuUsage,
  kGpuEngine,
  kDedicatedGpuMemory,
  kSharedGpuMemory,
  kSessionId,
  kCreateTime,
};

enum class ColumnAlignment {
  kLeft,
  kRight,
  kCenter,
};

struct ProcessColumnInfo {
  ProcessColumnId id;
  std::wstring header_text;
  int default_width;
  ColumnAlignment alignment;
  bool visible;
  int order;

  static std::vector<ProcessColumnInfo> GetDefaultColumns();
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PROCESS_COLUMN_H_
