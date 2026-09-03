// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_THEME_MANAGER_H_
#define LITE_PROC_MANAGER_THEME_MANAGER_H_

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "app_settings.h"

namespace lite_proc_manager {

struct ColorPalette {
  COLORREF window_background;
  COLORREF surface_background;
  COLORREF control_background;
  COLORREF control_hover;
  COLORREF text_primary;
  COLORREF text_secondary;
  COLORREF border_color;
  COLORREF accent_color;
  COLORREF accent_hover;
  COLORREF selected_item;
  COLORREF selected_item_text;
  COLORREF strip_background;
  COLORREF header_background;
  COLORREF header_text;
  COLORREF grid_line_color;

  HBRUSH window_brush{nullptr};
  HBRUSH surface_brush{nullptr};
  HBRUSH control_brush{nullptr};
  HBRUSH strip_brush{nullptr};
};

class ThemeManager {
 public:
  static void Initialize();
  static void Shutdown();

  static const ColorPalette& GetPalette(AppTheme theme);
  static void ApplyTheme(HWND hwnd, AppTheme theme);
  static void ApplyListViewHeaderTheme(HWND listview_hwnd, AppTheme theme, int sort_column = -1, bool sort_ascending = true);
  static HFONT CreateAppFont(const std::wstring& font_name, int font_size_pt, bool bold = false);

 private:
  static ColorPalette dark_palette_;
  static ColorPalette light_palette_;
  static bool initialized_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_THEME_MANAGER_H_
