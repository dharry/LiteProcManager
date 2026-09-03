// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_MONITOR_SERVICE_H_
#define LITE_PROC_MANAGER_MONITOR_SERVICE_H_

#include <windows.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "monitor_rule.h"
#include "process_item.h"

namespace lite_proc_manager {

struct MonitorEvent {
  std::wstring rule_id;
  std::wstring rule_name;
  uint32_t process_id{0};
  std::wstring process_name;
  EventLevel level{EventLevel::kWarning};
  std::wstring reason;
  std::chrono::system_clock::time_point timestamp;
};

class MonitorService {
 public:
  MonitorService();
  ~MonitorService() = default;

  void SetRules(const std::vector<MonitorRule>& rules);
  const std::vector<MonitorRule>& GetRules() const { return rules_; }

  std::vector<MonitorEvent> CheckProcesses(
      const std::vector<std::shared_ptr<ProcessItem>>& processes,
      HWND notify_hwnd = nullptr,
      UINT notify_id = 1);

  const std::vector<MonitorEvent>& GetRecentEvents() const { return event_history_; }
  void ClearHistory() { event_history_.clear(); }

  static void ShowBalloonNotification(
      HWND hwnd, UINT uID, EventLevel level,
      const std::wstring& title, const std::wstring& message);

 private:
  std::vector<MonitorRule> rules_;
  std::vector<MonitorEvent> event_history_;

  // (rule_id + ":" + pid) -> last_notified_timestamp
  std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> last_notified_map_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_MONITOR_SERVICE_H_
