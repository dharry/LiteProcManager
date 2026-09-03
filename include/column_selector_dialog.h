// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_COLUMN_SELECTOR_DIALOG_H_
#define LITE_PROC_MANAGER_COLUMN_SELECTOR_DIALOG_H_

#include <windows.h>
#include <commctrl.h>

#include <vector>

#include "app_settings.h"
#include "process_column.h"

namespace lite_proc_manager {

class ColumnSelectorDialog {
 public:
  ColumnSelectorDialog(HWND parent_hwnd, const std::vector<ProcessColumnInfo>& columns, AppTheme theme);
  ~ColumnSelectorDialog();

  bool Show();
  const std::vector<ProcessColumnInfo>& GetColumns() const { return columns_; }

 private:
  static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  INT_PTR HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void PopulateList();
  void MoveItem(int direction);
  void SelectAll();
  void ResetDefault();
  void OnOk();

  HWND parent_hwnd_{nullptr};
  HWND dialog_hwnd_{nullptr};
  HWND listbox_hwnd_{nullptr};
  std::vector<ProcessColumnInfo> columns_;
  AppTheme theme_{AppTheme::kDark};
  bool result_{false};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_COLUMN_SELECTOR_DIALOG_H_
