// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PROCESS_EXPORT_H_
#define LITE_PROC_MANAGER_PROCESS_EXPORT_H_

#include <memory>
#include <string>
#include <vector>

#include "process_column.h"
#include "process_item.h"

namespace lite_proc_manager {

// Builds the process data used by both clipboard copy and file export.
std::wstring BuildProcessJson(
    const std::vector<std::shared_ptr<ProcessItem>>& processes,
    const std::vector<ProcessColumnInfo>& columns);

std::wstring BuildProcessTsv(
    const std::vector<std::shared_ptr<ProcessItem>>& processes,
    const std::vector<ProcessColumnInfo>& columns);

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PROCESS_EXPORT_H_
