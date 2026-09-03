// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_TOAST_WINDOW_H_
#define LITE_PROC_MANAGER_TOAST_WINDOW_H_

#include <windows.h>
#include <string>
#include <vector>

#include "monitor_rule.h"

namespace lite_proc_manager {

class ToastWindow {
 public:
  static void ShowToast(
      EventLevel level,
      const std::wstring& title,
      const std::wstring& message,
      HWND main_hwnd = nullptr);

 private:
  ToastWindow(EventLevel level, const std::wstring& title, const std::wstring& message, HWND main_hwnd);
  ~ToastWindow();

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void Paint(HDC hdc);
  void Close();

  HWND hwnd_{nullptr};
  HWND main_hwnd_{nullptr};
  EventLevel level_{EventLevel::kWarning};
  std::wstring title_;
  std::wstring message_;

  HFONT font_title_{nullptr};
  HFONT font_text_{nullptr};
  HFONT font_icon_{nullptr};

  int alpha_{255};
  bool closing_{false};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_TOAST_WINDOW_H_
