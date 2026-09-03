// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_MONITOR_DIALOG_H_
#define LITE_PROC_MANAGER_MONITOR_DIALOG_H_

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "app_settings.h"
#include "monitor_rule.h"

namespace lite_proc_manager {

class MonitorDialog {
 public:
  MonitorDialog(HWND parent_hwnd, const std::vector<MonitorRule>& rules, AppTheme theme);
  ~MonitorDialog() = default;

  bool Show();
  const std::vector<MonitorRule>& GetRules() const { return rules_; }

  static bool ShowAddRuleForProcess(
      HWND parent_hwnd, const std::wstring& process_name, uint32_t pid,
      std::vector<MonitorRule>* in_out_rules, AppTheme theme);

 private:
  static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void InitializeDialog(HWND hwnd);
  void RefreshRuleList();
  void UpdateButtonsState();
  std::vector<int> GetSelectedRuleIndices() const;
  void OnAddRule();
  void OnEditRule();
  void OnDeleteRule();
  void OnToggleRuleEnabled();
  void OnTestRules(EventLevel level);

  HWND parent_hwnd_{nullptr};
  HWND dlg_hwnd_{nullptr};
  HWND listview_hwnd_{nullptr};
  HWND btn_add_{nullptr};
  HWND btn_edit_{nullptr};
  HWND btn_delete_{nullptr};
  HWND btn_toggle_{nullptr};
  HWND btn_test_warn_{nullptr};
  HWND btn_test_err_{nullptr};
  HWND btn_ok_{nullptr};
  HWND btn_cancel_{nullptr};

  std::vector<MonitorRule> rules_;
  AppTheme theme_{AppTheme::kDark};
  bool confirmed_{false};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_MONITOR_DIALOG_H_
