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
  COLORREF window_background{0};
  COLORREF surface_background{0};
  COLORREF control_background{0};
  COLORREF control_hover{0};
  COLORREF text_primary{0};
  COLORREF text_secondary{0};
  COLORREF border_color{0};
  COLORREF accent_color{0};
  COLORREF accent_hover{0};
  COLORREF selected_item{0};
  COLORREF selected_item_text{0};
  COLORREF strip_background{0};
  COLORREF header_background{0};
  COLORREF header_text{0};
  COLORREF grid_line_color{0};
  COLORREF focus_border{0};
  COLORREF placeholder_text{0};

  HBRUSH window_brush{nullptr};
  HBRUSH surface_brush{nullptr};
  HBRUSH control_brush{nullptr};
  HBRUSH strip_brush{nullptr};
};

class ThemeManager {
 public:
  static void Initialize();
  static void Shutdown();
  static void SetPreferredAppTheme(AppTheme theme);

  static const ColorPalette& GetPalette(AppTheme theme);
  static void ApplyTheme(HWND hwnd, AppTheme theme);
  static void ApplyFontToWindowTree(HWND hwnd, HFONT font);
  static void ApplyListViewHeaderTheme(HWND listview_hwnd, AppTheme theme, int sort_column = -1, bool sort_ascending = true);
  static HFONT CreateAppFont(const std::wstring& font_name, int font_size_pt, bool bold = false);

 private:
  static ColorPalette dark_palette_;
  static ColorPalette light_palette_;
  static bool initialized_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_THEME_MANAGER_H_
