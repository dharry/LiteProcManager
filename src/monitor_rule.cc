// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "monitor_rule.h"

#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <random>
#include <sstream>

#include "language_manager.h"

namespace lite_proc_manager {

namespace {
bool CaseInsensitiveEquals(const std::wstring& a, const std::wstring& b) {
  if (a.length() != b.length()) return false;
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool CaseInsensitiveContains(const std::wstring& str, const std::wstring& substr) {
  if (substr.empty()) return true;
  if (str.empty()) return false;
  auto it = std::search(
      str.begin(), str.end(), substr.begin(), substr.end(),
      [](wchar_t c1, wchar_t c2) { return std::towlower(c1) == std::towlower(c2); });
  return it != str.end();
}

bool CaseInsensitiveStartsWith(const std::wstring& str, const std::wstring& prefix) {
  if (prefix.empty()) return true;
  if (str.length() < prefix.length()) return false;
  return _wcsnicmp(str.c_str(), prefix.c_str(), prefix.length()) == 0;
}

bool CaseInsensitiveEndsWith(const std::wstring& str, const std::wstring& suffix) {
  if (suffix.empty()) return true;
  if (str.length() < suffix.length()) return false;
  const wchar_t* p = str.c_str() + (str.length() - suffix.length());
  return _wcsicmp(p, suffix.c_str()) == 0;
}

double GetProcessNumericValue(const ProcessItem& item, ProcessColumnId col_id, bool* is_numeric) {
  *is_numeric = true;
  switch (col_id) {
    case ProcessColumnId::kPid:
      return static_cast<double>(item.process_id);
    case ProcessColumnId::kCpu:
      return item.cpu_percent;
    case ProcessColumnId::kPrivateWorkingSet:
      return static_cast<double>(item.private_working_set > 0 ? item.private_working_set : item.working_set);
    case ProcessColumnId::kWorkingSet:
      return static_cast<double>(item.working_set);
    case ProcessColumnId::kPeakWorkingSet:
      return static_cast<double>(item.peak_working_set);
    case ProcessColumnId::kWorkingSetDelta:
      return static_cast<double>(item.working_set_delta);
    case ProcessColumnId::kCommitSize:
      return static_cast<double>(item.commit_size);
    case ProcessColumnId::kPagedPool:
      return static_cast<double>(item.paged_pool);
    case ProcessColumnId::kNonPagedPool:
      return static_cast<double>(item.non_paged_pool);
    case ProcessColumnId::kHandles:
      return static_cast<double>(item.handle_count);
    case ProcessColumnId::kThreads:
      return static_cast<double>(item.thread_count);
    case ProcessColumnId::kUserObjects:
      return static_cast<double>(item.user_objects);
    case ProcessColumnId::kGdiObjects:
      return static_cast<double>(item.gdi_objects);
    case ProcessColumnId::kIoReadCount:
      return static_cast<double>(item.io_read_count);
    case ProcessColumnId::kIoWriteCount:
      return static_cast<double>(item.io_write_count);
    case ProcessColumnId::kIoOtherCount:
      return static_cast<double>(item.io_other_count);
    case ProcessColumnId::kIoReadBytes:
      return static_cast<double>(item.io_read_bytes);
    case ProcessColumnId::kIoWriteBytes:
      return static_cast<double>(item.io_write_bytes);
    case ProcessColumnId::kIoOtherBytes:
      return static_cast<double>(item.io_other_bytes);
    case ProcessColumnId::kGpuUsage:
      return item.gpu_percent;
    case ProcessColumnId::kDedicatedGpuMemory:
      return static_cast<double>(item.dedicated_gpu_memory);
    case ProcessColumnId::kSharedGpuMemory:
      return static_cast<double>(item.shared_gpu_memory);
    case ProcessColumnId::kBasePriority:
      return static_cast<double>(item.base_priority_raw);
    case ProcessColumnId::kSessionId:
      return static_cast<double>(item.session_id);
    default:
      *is_numeric = false;
      return 0.0;
  }
}

std::wstring GetColumnDisplayName(ProcessColumnId id) {
  return LanguageManager::GetColumnHeaderText(id);
}
}  // namespace

bool MonitorCondition::Evaluate(const ProcessItem& item, std::wstring* out_detail) const {
  bool is_numeric = false;
  double actual_num = GetProcessNumericValue(item, column_id, &is_numeric);

  std::wstring col_name = GetColumnDisplayName(column_id);
  bool pass = false;

  if (is_numeric) {
    switch (op) {
      case ComparisonOperator::kGreaterThan:
        pass = (actual_num > numeric_value);
        break;
      case ComparisonOperator::kGreaterThanOrEqual:
        pass = (actual_num >= numeric_value);
        break;
      case ComparisonOperator::kLessThan:
        pass = (actual_num < numeric_value);
        break;
      case ComparisonOperator::kLessThanOrEqual:
        pass = (actual_num <= numeric_value);
        break;
      case ComparisonOperator::kEqual:
        pass = (std::abs(actual_num - numeric_value) < 0.001);
        break;
      case ComparisonOperator::kNotEqual:
        pass = (std::abs(actual_num - numeric_value) >= 0.001);
        break;
      default:
        pass = (actual_num >= numeric_value);
        break;
    }

    if (pass && out_detail) {
      wchar_t buf[128];
      if (column_id == ProcessColumnId::kCpu || column_id == ProcessColumnId::kGpuUsage) {
        swprintf_s(buf, L"%s: %.1f%% (%s %.1f%%)", col_name.c_str(), actual_num,
                   MonitorRule::OperatorToString(op).c_str(), numeric_value);
      } else if (MonitorRule::IsBytesColumn(column_id)) {
        swprintf_s(buf, L"%s: %s (%s %s)", col_name.c_str(),
                   ProcessItem::FormatBytes(static_cast<uint64_t>(actual_num)).c_str(),
                   MonitorRule::OperatorToString(op).c_str(),
                   ProcessItem::FormatBytes(static_cast<uint64_t>(numeric_value)).c_str());
      } else {
        swprintf_s(buf, L"%s: %.0f (%s %.0f)", col_name.c_str(), actual_num,
                   MonitorRule::OperatorToString(op).c_str(), numeric_value);
      }
      *out_detail = buf;
    }
  } else {
    // String column evaluation
    std::wstring actual_str = item.GetColumnValue(column_id);
    switch (op) {
      case ComparisonOperator::kEqual:
        pass = CaseInsensitiveEquals(actual_str, string_value);
        break;
      case ComparisonOperator::kNotEqual:
        pass = !CaseInsensitiveEquals(actual_str, string_value);
        break;
      case ComparisonOperator::kContains:
        pass = CaseInsensitiveContains(actual_str, string_value);
        break;
      default:
        pass = CaseInsensitiveContains(actual_str, string_value);
        break;
    }

    if (pass && out_detail) {
      *out_detail = col_name + L": \"" + actual_str + L"\" (" +
                    MonitorRule::OperatorToString(op) + L" \"" + string_value + L"\")";
    }
  }

  return pass;
}

std::wstring MonitorCondition::ToString() const {
  std::wstring col_name = GetColumnDisplayName(column_id);
  std::wstring op_str = MonitorRule::OperatorToString(op);

  bool is_numeric = false;
  ProcessItem dummy;
  GetProcessNumericValue(dummy, column_id, &is_numeric);

  if (is_numeric) {
    if (column_id == ProcessColumnId::kCpu || column_id == ProcessColumnId::kGpuUsage) {
      wchar_t buf[64];
      swprintf_s(buf, L"%s %s %.1f%%", col_name.c_str(), op_str.c_str(), numeric_value);
      return buf;
    } else if (MonitorRule::IsBytesColumn(column_id)) {
      return col_name + L" " + op_str + L" " + ProcessItem::FormatBytes(static_cast<uint64_t>(numeric_value));
    }
    return col_name + L" " + op_str + L" " + std::to_wstring(static_cast<int64_t>(numeric_value));
  }
  return col_name + L" " + op_str + L" \"" + string_value + L"\"";
}

MonitorRule::MonitorRule() : id(GenerateId()) {}

std::wstring MonitorRule::GenerateId() {
  static std::mt19937_64 rng(GetTickCount64());
  uint64_t val = rng();
  wchar_t buf[32];
  swprintf_s(buf, L"rule_%llx", val);
  return buf;
}

std::wstring MonitorRule::OperatorToString(ComparisonOperator op) {
  switch (op) {
    case ComparisonOperator::kGreaterThan: return L">";
    case ComparisonOperator::kGreaterThanOrEqual: return L">=";
    case ComparisonOperator::kLessThan: return L"<";
    case ComparisonOperator::kLessThanOrEqual: return L"<=";
    case ComparisonOperator::kEqual: return L"==";
    case ComparisonOperator::kNotEqual: return L"!=";
    case ComparisonOperator::kContains: return LanguageManager::GetString(StringId::kOpContains);
    default: return L">=";
  }
}

std::wstring MonitorRule::MatchTypeToString(ProcessMatchType type) {
  switch (type) {
    case ProcessMatchType::kExact: return LanguageManager::GetString(StringId::kMatchExact);
    case ProcessMatchType::kContains: return LanguageManager::GetString(StringId::kMatchContains);
    case ProcessMatchType::kStartsWith: return LanguageManager::GetString(StringId::kMatchStartsWith);
    case ProcessMatchType::kEndsWith: return LanguageManager::GetString(StringId::kMatchEndsWith);
    default: return LanguageManager::GetString(StringId::kMatchContains);
  }
}

std::wstring MonitorRule::EventLevelToString(EventLevel level) {
  return (level == EventLevel::kCritical) ? LanguageManager::GetString(StringId::kLevelCritical)
                                          : LanguageManager::GetString(StringId::kLevelWarning);
}

bool MonitorRule::MatchesProcess(const ProcessItem& item) const {
  if (!enabled) return false;
  if (target_pattern.empty()) return true;  // 空なら全プロセス対象

  if (match_target == ProcessMatchTarget::kPid) {
    std::wstring pid_str = std::to_wstring(item.process_id);
    switch (match_type) {
      case ProcessMatchType::kExact:
        return pid_str == target_pattern;
      case ProcessMatchType::kContains:
        return pid_str.find(target_pattern) != std::wstring::npos;
      case ProcessMatchType::kStartsWith:
        return pid_str.rfind(target_pattern, 0) == 0;
      case ProcessMatchType::kEndsWith:
        return CaseInsensitiveEndsWith(pid_str, target_pattern);
    }
  } else {
    // Process Name
    switch (match_type) {
      case ProcessMatchType::kExact:
        return CaseInsensitiveEquals(item.name, target_pattern);
      case ProcessMatchType::kContains:
        return CaseInsensitiveContains(item.name, target_pattern);
      case ProcessMatchType::kStartsWith:
        return CaseInsensitiveStartsWith(item.name, target_pattern);
      case ProcessMatchType::kEndsWith:
        return CaseInsensitiveEndsWith(item.name, target_pattern);
    }
  }
  return false;
}

bool MonitorRule::Evaluate(const ProcessItem& item, std::wstring* out_reasons) const {
  if (!MatchesProcess(item)) return false;
  if (conditions.empty()) return false;

  std::vector<std::wstring> passed_details;

  if (logical_op == LogicalOperator::kAnd) {
    for (const auto& cond : conditions) {
      std::wstring detail;
      if (!cond.Evaluate(item, &detail)) {
        return false;
      }
      passed_details.push_back(detail);
    }
  } else {
    // OR
    bool any_passed = false;
    for (const auto& cond : conditions) {
      std::wstring detail;
      if (cond.Evaluate(item, &detail)) {
        any_passed = true;
        passed_details.push_back(detail);
      }
    }
    if (!any_passed) return false;
  }

  if (out_reasons) {
    std::wstring combined;
    for (size_t i = 0; i < passed_details.size(); ++i) {
      if (i > 0) combined += (logical_op == LogicalOperator::kAnd ? L" && " : L" || ");
      combined += passed_details[i];
    }
    *out_reasons = combined;
  }

  return true;
}

bool MonitorRule::IsBytesColumn(ProcessColumnId id) {
  switch (id) {
    case ProcessColumnId::kWorkingSet:
    case ProcessColumnId::kPrivateWorkingSet:
    case ProcessColumnId::kPeakWorkingSet:
    case ProcessColumnId::kWorkingSetDelta:
    case ProcessColumnId::kCommitSize:
    case ProcessColumnId::kPagedPool:
    case ProcessColumnId::kNonPagedPool:
    case ProcessColumnId::kDedicatedGpuMemory:
    case ProcessColumnId::kSharedGpuMemory:
    case ProcessColumnId::kIoReadBytes:
    case ProcessColumnId::kIoWriteBytes:
    case ProcessColumnId::kIoOtherBytes:
      return true;
    default:
      return false;
  }
}

bool MonitorRule::IsNumericColumn(ProcessColumnId id) {
  bool is_num = false;
  ProcessItem dummy;
  GetProcessNumericValue(dummy, id, &is_num);
  return is_num;
}

const std::vector<ProcessColumnId>& MonitorRule::GetNumericRuleColumnIds() {
  static const std::vector<ProcessColumnId> kCols = {
    ProcessColumnId::kCpu,
    ProcessColumnId::kWorkingSet,
    ProcessColumnId::kPrivateWorkingSet,
    ProcessColumnId::kPeakWorkingSet,
    ProcessColumnId::kWorkingSetDelta,
    ProcessColumnId::kCommitSize,
    ProcessColumnId::kPagedPool,
    ProcessColumnId::kNonPagedPool,
    ProcessColumnId::kHandles,
    ProcessColumnId::kThreads,
    ProcessColumnId::kUserObjects,
    ProcessColumnId::kGdiObjects,
    ProcessColumnId::kIoReadBytes,
    ProcessColumnId::kIoWriteBytes,
    ProcessColumnId::kIoOtherBytes,
    ProcessColumnId::kIoReadCount,
    ProcessColumnId::kIoWriteCount,
    ProcessColumnId::kIoOtherCount,
    ProcessColumnId::kGpuUsage,
    ProcessColumnId::kDedicatedGpuMemory,
    ProcessColumnId::kSharedGpuMemory,
    ProcessColumnId::kBasePriority,
    ProcessColumnId::kPid,
    ProcessColumnId::kSessionId,
  };
  return kCols;
}

}  // namespace lite_proc_manager
