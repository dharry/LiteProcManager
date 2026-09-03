// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_MONITOR_RULE_H_
#define LITE_PROC_MANAGER_MONITOR_RULE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "process_column.h"
#include "process_item.h"

namespace lite_proc_manager {

enum class ProcessMatchTarget {
  kProcessName,
  kPid,
};

enum class ProcessMatchType {
  kExact,       // 完全一致
  kContains,    // 部分一致
  kStartsWith,  // 前方一致
  kEndsWith,    // 後方一致
};

enum class ComparisonOperator {
  kGreaterThan,         // >
  kGreaterThanOrEqual,  // >=
  kLessThan,            // <
  kLessThanOrEqual,     // <=
  kEqual,               // ==
  kNotEqual,            // !=
  kContains,            // 含む (文字列用)
};

enum class LogicalOperator {
  kAnd,
  kOr,
};

enum class EventLevel {
  kWarning,   // 警告 (黄)
  kCritical,  // 異常 (赤)
};

struct MonitorCondition {
  ProcessColumnId column_id{ProcessColumnId::kCpu};
  ComparisonOperator op{ComparisonOperator::kGreaterThanOrEqual};
  double numeric_value{80.0};
  std::wstring string_value;

  bool Evaluate(const ProcessItem& item, std::wstring* out_detail = nullptr) const;
  std::wstring ToString() const;
};

class MonitorRule {
 public:
  MonitorRule();
  ~MonitorRule() = default;

  std::wstring id;
  std::wstring name;
  bool enabled{true};

  ProcessMatchTarget match_target{ProcessMatchTarget::kProcessName};
  ProcessMatchType match_type{ProcessMatchType::kContains};
  std::wstring target_pattern;

  EventLevel level{EventLevel::kWarning};
  LogicalOperator logical_op{LogicalOperator::kAnd};
  std::vector<MonitorCondition> conditions;

  int cooldown_seconds{30};
  bool notify_if_not_found{false};

  bool MatchesProcess(const ProcessItem& item) const;
  bool Evaluate(const ProcessItem& item, std::wstring* out_reasons) const;

  static std::wstring GenerateId();
  static std::wstring OperatorToString(ComparisonOperator op);
  static std::wstring MatchTypeToString(ProcessMatchType type);
  static std::wstring EventLevelToString(EventLevel level);
  static bool IsNumericColumn(ProcessColumnId id);
  static bool IsBytesColumn(ProcessColumnId id);
  static const std::vector<ProcessColumnId>& GetNumericRuleColumnIds();
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_MONITOR_RULE_H_
