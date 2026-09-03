// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "language_manager.h"

namespace lite_proc_manager {

std::vector<ProcessColumnInfo> ProcessColumnInfo::GetDefaultColumns() {
  std::vector<ProcessColumnInfo> cols = {
      {ProcessColumnId::kName, L"", 220, ColumnAlignment::kLeft, true, 0},
      {ProcessColumnId::kPid, L"", 70, ColumnAlignment::kRight, true, 1},
      {ProcessColumnId::kStatus, L"", 80, ColumnAlignment::kLeft, true, 2},
      {ProcessColumnId::kUserName, L"", 140, ColumnAlignment::kLeft, true, 3},
      {ProcessColumnId::kCpu, L"", 75, ColumnAlignment::kRight, true, 4},
      {ProcessColumnId::kPrivateWorkingSet, L"", 120, ColumnAlignment::kRight, true, 5},
      {ProcessColumnId::kWorkingSet, L"", 110, ColumnAlignment::kRight, true, 6},
      {ProcessColumnId::kPeakWorkingSet, L"", 110, ColumnAlignment::kRight, false, 7},
      {ProcessColumnId::kWorkingSetDelta, L"", 110, ColumnAlignment::kRight, false, 8},
      {ProcessColumnId::kCommitSize, L"", 110, ColumnAlignment::kRight, true, 9},
      {ProcessColumnId::kPagedPool, L"", 90, ColumnAlignment::kRight, false, 10},
      {ProcessColumnId::kNonPagedPool, L"", 90, ColumnAlignment::kRight, false, 11},
      {ProcessColumnId::kBasePriority, L"", 90, ColumnAlignment::kLeft, true, 12},
      {ProcessColumnId::kHandles, L"", 80, ColumnAlignment::kRight, true, 13},
      {ProcessColumnId::kThreads, L"", 70, ColumnAlignment::kRight, true, 14},
      {ProcessColumnId::kUserObjects, L"", 90, ColumnAlignment::kRight, false, 15},
      {ProcessColumnId::kGdiObjects, L"", 90, ColumnAlignment::kRight, false, 16},
      {ProcessColumnId::kIoReadCount, L"", 90, ColumnAlignment::kRight, false, 17},
      {ProcessColumnId::kIoWriteCount, L"", 90, ColumnAlignment::kRight, false, 18},
      {ProcessColumnId::kIoOtherCount, L"", 90, ColumnAlignment::kRight, false, 19},
      {ProcessColumnId::kIoReadBytes, L"", 110, ColumnAlignment::kRight, false, 20},
      {ProcessColumnId::kIoWriteBytes, L"", 110, ColumnAlignment::kRight, false, 21},
      {ProcessColumnId::kIoOtherBytes, L"", 110, ColumnAlignment::kRight, false, 22},
      {ProcessColumnId::kFilePath, L"", 280, ColumnAlignment::kLeft, false, 23},
      {ProcessColumnId::kCommandLine, L"", 280, ColumnAlignment::kLeft, false, 24},
      {ProcessColumnId::kOsContext, L"", 120, ColumnAlignment::kLeft, false, 25},
      {ProcessColumnId::kPlatform, L"", 90, ColumnAlignment::kLeft, true, 26},
      {ProcessColumnId::kElevated, L"", 60, ColumnAlignment::kLeft, false, 27},
      {ProcessColumnId::kUacVirtualization, L"", 90, ColumnAlignment::kLeft, false, 28},
      {ProcessColumnId::kDescription, L"", 200, ColumnAlignment::kLeft, true, 29},
      {ProcessColumnId::kDepStatus, L"", 100, ColumnAlignment::kLeft, false, 30},
      {ProcessColumnId::kEnterpriseContext, L"", 100, ColumnAlignment::kLeft, false, 31},
      {ProcessColumnId::kDpiAwareness, L"", 110, ColumnAlignment::kLeft, false, 32},
      {ProcessColumnId::kPackageName, L"", 180, ColumnAlignment::kLeft, false, 33},
      {ProcessColumnId::kArchitecture, L"", 90, ColumnAlignment::kLeft, true, 34},
      {ProcessColumnId::kGpuUsage, L"", 70, ColumnAlignment::kRight, false, 35},
      {ProcessColumnId::kGpuEngine, L"", 90, ColumnAlignment::kLeft, false, 36},
      {ProcessColumnId::kDedicatedGpuMemory, L"", 100, ColumnAlignment::kRight, false, 37},
      {ProcessColumnId::kSharedGpuMemory, L"", 100, ColumnAlignment::kRight, false, 38},
      {ProcessColumnId::kSessionId, L"", 80, ColumnAlignment::kRight, false, 39},
      {ProcessColumnId::kCreateTime, L"", 140, ColumnAlignment::kLeft, false, 40},
  };

  for (auto& c : cols) {
    c.header_text = LanguageManager::GetColumnHeaderText(c.id);
  }
  return cols;
}

}  // namespace lite_proc_manager
