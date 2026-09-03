#include "monitor_service.h"

#include <shellapi.h>

#include <algorithm>
#include <cwchar>

#include "language_manager.h"
#include "toast_window.h"

namespace lite_proc_manager {

MonitorService::MonitorService() = default;

void MonitorService::SetRules(const std::vector<MonitorRule>& rules) {
  rules_ = rules;
}

std::vector<MonitorEvent> MonitorService::CheckProcesses(
    const std::vector<std::shared_ptr<ProcessItem>>& processes,
    HWND notify_hwnd,
    UINT notify_id) {
  std::vector<MonitorEvent> triggered_events;
  auto now = std::chrono::steady_clock::now();

  for (const auto& rule : rules_) {
    if (!rule.enabled) continue;

    bool matched_any_process = false;

    for (const auto& proc : processes) {
      if (rule.MatchesProcess(*proc)) {
        matched_any_process = true;
        std::wstring reasons;
        if (rule.Evaluate(*proc, &reasons)) {
          std::wstring key = rule.id + L"_" + std::to_wstring(proc->process_id);

          auto it_last = last_notified_map_.find(key);
          bool should_notify = true;

          if (it_last != last_notified_map_.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it_last->second).count();
            if (elapsed < rule.cooldown_seconds) {
              should_notify = false;
            }
          }

          if (should_notify) {
            last_notified_map_[key] = now;

            MonitorEvent ev;
            ev.rule_id = rule.id;
            ev.rule_name = rule.name.empty() ? LanguageManager::GetString(StringId::kDlgMonitorTitle) : rule.name;
            ev.process_id = proc->process_id;
            ev.process_name = proc->name;
            ev.level = rule.level;
            ev.reason = reasons;
            ev.timestamp = std::chrono::system_clock::now();

            triggered_events.push_back(ev);
            event_history_.push_back(ev);

            // Keep history capped at 100 items
            if (event_history_.size() > 100) {
              event_history_.erase(event_history_.begin());
            }

            // Show task tray balloon notification
            if (notify_hwnd) {
              std::wstring level_text = (rule.level == EventLevel::kCritical)
                                            ? LanguageManager::GetString(StringId::kLevelCritical)
                                            : LanguageManager::GetString(StringId::kLevelWarning);
              std::wstring title = L"[" + level_text + L"] " + ev.rule_name + L": " + proc->name +
                                   L" (PID: " + std::to_wstring(proc->process_id) + L")";
              ShowBalloonNotification(notify_hwnd, notify_id, rule.level, title, reasons);
            }
          }
        }
      }
    }

    // Process Not Found notification (Deadman / heartbeat check)
    if (rule.notify_if_not_found && !matched_any_process && !rule.target_pattern.empty()) {
      std::wstring key = rule.id + L"_not_found";

      auto it_last = last_notified_map_.find(key);
      bool should_notify = true;

      if (it_last != last_notified_map_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it_last->second).count();
        if (elapsed < rule.cooldown_seconds) {
          should_notify = false;
        }
      }

      if (should_notify) {
        last_notified_map_[key] = now;

        MonitorEvent ev;
        ev.rule_id = rule.id;
        ev.rule_name = rule.name.empty() ? LanguageManager::GetString(StringId::kDlgMonitorTitle) : rule.name;
        ev.process_id = 0;
        ev.process_name = rule.target_pattern;
        ev.level = rule.level;
        ev.reason = LanguageManager::GetString(StringId::kMsgProcessNotFound);
        ev.timestamp = std::chrono::system_clock::now();

        triggered_events.push_back(ev);
        event_history_.push_back(ev);

        if (event_history_.size() > 100) {
          event_history_.erase(event_history_.begin());
        }

        if (notify_hwnd) {
          std::wstring level_text = (rule.level == EventLevel::kCritical)
                                        ? LanguageManager::GetString(StringId::kLevelCritical)
                                        : LanguageManager::GetString(StringId::kLevelWarning);
          std::wstring title = L"[" + level_text + L"] " + ev.rule_name + L": " + rule.target_pattern;
          ShowBalloonNotification(notify_hwnd, notify_id, rule.level, title, ev.reason);
        }
      }
    }
  }

  return triggered_events;
}

void MonitorService::ShowBalloonNotification(
    HWND hwnd, UINT uID, EventLevel level,
    const std::wstring& title, const std::wstring& message) {
  // 1. Try OS Native Shell_NotifyIconW Balloon/Toast
  if (hwnd) {
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = uID;
    nid.uFlags = NIF_INFO | NIF_SHOWTIP;

    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);

    DWORD info_flags = (level == EventLevel::kCritical) ? NIIF_ERROR : NIIF_WARNING;
    info_flags |= NIIF_LARGE_ICON;
    nid.dwInfoFlags = info_flags;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }

  // 2. Always show 100% visible Modern Custom Alert Balloon Popup in bottom-right corner
  ToastWindow::ShowToast(level, title, message, hwnd);
}

}  // namespace lite_proc_manager
