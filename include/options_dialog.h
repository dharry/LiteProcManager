// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_OPTIONS_DIALOG_H_
#define LITE_PROC_MANAGER_OPTIONS_DIALOG_H_

#include <windows.h>
#include <commctrl.h>

#include "app_settings.h"

namespace lite_proc_manager {

class OptionsDialog {
 public:
  OptionsDialog(HWND parent_hwnd, const AppSettings& settings);
  ~OptionsDialog() = default;

  bool Show();
  const AppSettings& GetSettings() const { return settings_; }

 private:
  static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void InitializeDialog(HWND hwnd);
  void UpdateIntervalLabel(int seconds);
  void OnChangeListFont();
  void OnChangeUiFont();
  void OnAddExcludedProcess();
  void OnDeleteExcludedProcess();
  void OnSave();

  HWND parent_hwnd_{nullptr};
  HWND dlg_hwnd_{nullptr};

  HWND combo_lang_{nullptr};
  HWND lbl_list_font_val_{nullptr};
  HWND btn_list_font_{nullptr};
  HWND lbl_ui_font_val_{nullptr};
  HWND btn_ui_font_{nullptr};
  HWND slider_interval_{nullptr};
  HWND lbl_interval_val_{nullptr};
  HWND chk_always_top_{nullptr};
  HWND chk_tray_{nullptr};
  HWND chk_auto_start_{nullptr};
  HWND list_excluded_{nullptr};
  HWND btn_add_excluded_{nullptr};
  HWND btn_del_excluded_{nullptr};

  AppSettings settings_;
  bool confirmed_{false};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_OPTIONS_DIALOG_H_
