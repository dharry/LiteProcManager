// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "theme_manager.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace lite_proc_manager {

namespace {
using PfnSetPreferredAppMode = int(WINAPI*)(int app_mode);
using PfnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND hwnd, BOOL allow);
using PfnFlushMenuThemes = void(WINAPI*)();

PfnSetPreferredAppMode pfn_set_preferred_app_mode = nullptr;
PfnAllowDarkModeForWindow pfn_allow_dark_mode_for_window = nullptr;
PfnFlushMenuThemes pfn_flush_menu_themes = nullptr;

void InitDarkModeApis() {
  HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
  if (!uxtheme) {
    uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  if (uxtheme) {
    // Ordinal 135: SetPreferredAppMode (Windows 10 1903+)
    // 0 = Default, 1 = AllowDark, 2 = ForceDark, 3 = ForceLight, 4 = Max
    pfn_set_preferred_app_mode = reinterpret_cast<PfnSetPreferredAppMode>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));

    // Ordinal 133: AllowDarkModeForWindow
    pfn_allow_dark_mode_for_window = reinterpret_cast<PfnAllowDarkModeForWindow>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));

    // Ordinal 136: FlushMenuThemes
    pfn_flush_menu_themes = reinterpret_cast<PfnFlushMenuThemes>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
  }
}
}  // namespace

ColorPalette ThemeManager::dark_palette_;
ColorPalette ThemeManager::light_palette_;
bool ThemeManager::initialized_ = false;

void ThemeManager::Initialize() {
  if (initialized_) return;

  InitDarkModeApis();

  // Dark Palette
  dark_palette_.window_background = RGB(32, 32, 32);
  dark_palette_.surface_background = RGB(40, 40, 40);
  dark_palette_.control_background = RGB(45, 45, 48);
  dark_palette_.control_hover = RGB(60, 60, 65);
  dark_palette_.text_primary = RGB(245, 245, 245);
  dark_palette_.text_secondary = RGB(170, 170, 170);
  dark_palette_.border_color = RGB(65, 65, 70);
  dark_palette_.accent_color = RGB(0, 120, 215);
  dark_palette_.accent_hover = RGB(24, 144, 255);
  dark_palette_.selected_item = RGB(0, 90, 158);
  dark_palette_.selected_item_text = RGB(255, 255, 255);
  dark_palette_.strip_background = RGB(28, 28, 28);
  dark_palette_.header_background = RGB(36, 36, 36);
  dark_palette_.header_text = RGB(220, 220, 220);
  dark_palette_.grid_line_color = RGB(50, 50, 55);

  dark_palette_.window_brush = CreateSolidBrush(dark_palette_.window_background);
  dark_palette_.surface_brush = CreateSolidBrush(dark_palette_.surface_background);
  dark_palette_.control_brush = CreateSolidBrush(dark_palette_.control_background);
  dark_palette_.strip_brush = CreateSolidBrush(dark_palette_.strip_background);

  // Light Palette
  light_palette_.window_background = RGB(243, 243, 243);
  light_palette_.surface_background = RGB(255, 255, 255);
  light_palette_.control_background = RGB(255, 255, 255);
  light_palette_.control_hover = RGB(235, 235, 235);
  light_palette_.text_primary = RGB(30, 30, 30);
  light_palette_.text_secondary = RGB(100, 100, 100);
  light_palette_.border_color = RGB(220, 220, 220);
  light_palette_.accent_color = RGB(0, 103, 192);
  light_palette_.accent_hover = RGB(0, 120, 215);
  light_palette_.selected_item = RGB(204, 232, 255);
  light_palette_.selected_item_text = RGB(0, 0, 0);
  light_palette_.strip_background = RGB(245, 245, 245);
  light_palette_.header_background = RGB(240, 240, 240);
  light_palette_.header_text = RGB(40, 40, 40);
  light_palette_.grid_line_color = RGB(230, 230, 230);

  light_palette_.window_brush = CreateSolidBrush(light_palette_.window_background);
  light_palette_.surface_brush = CreateSolidBrush(light_palette_.surface_background);
  light_palette_.control_brush = CreateSolidBrush(light_palette_.control_background);
  light_palette_.strip_brush = CreateSolidBrush(light_palette_.strip_background);

  initialized_ = true;
}

void ThemeManager::Shutdown() {
  if (!initialized_) return;

  if (dark_palette_.window_brush) DeleteObject(dark_palette_.window_brush);
  if (dark_palette_.surface_brush) DeleteObject(dark_palette_.surface_brush);
  if (dark_palette_.control_brush) DeleteObject(dark_palette_.control_brush);
  if (dark_palette_.strip_brush) DeleteObject(dark_palette_.strip_brush);

  if (light_palette_.window_brush) DeleteObject(light_palette_.window_brush);
  if (light_palette_.surface_brush) DeleteObject(light_palette_.surface_brush);
  if (light_palette_.control_brush) DeleteObject(light_palette_.control_brush);
  if (light_palette_.strip_brush) DeleteObject(light_palette_.strip_brush);

  initialized_ = false;
}

const ColorPalette& ThemeManager::GetPalette(AppTheme theme) {
  if (!initialized_) Initialize();
  return (theme == AppTheme::kDark) ? dark_palette_ : light_palette_;
}

void ThemeManager::ApplyTheme(HWND hwnd, AppTheme theme) {
  if (!initialized_) Initialize();

  BOOL dark_mode = (theme == AppTheme::kDark) ? TRUE : FALSE;

  // Set UxTheme PreferredAppMode
  if (pfn_set_preferred_app_mode) {
    pfn_set_preferred_app_mode(dark_mode ? 2 : 3);  // 2 = ForceDark, 3 = ForceLight
  }

  if (pfn_allow_dark_mode_for_window) {
    pfn_allow_dark_mode_for_window(hwnd, dark_mode);
  }

  if (pfn_flush_menu_themes) {
    pfn_flush_menu_themes();
  }

  // DWMWA_USE_IMMERSIVE_DARK_MODE (attribute value 20 on Windows 10 build 18985+ / Windows 11)
  constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
  DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark_mode, sizeof(dark_mode));

  // Also support build 1809 - 18363 (attribute 19)
  constexpr DWORD kDwmwaUseImmersiveDarkModeBefore20H1 = 19;
  DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeBefore20H1, &dark_mode, sizeof(dark_mode));

  // Update window class brush
  const auto& palette = GetPalette(theme);
  SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(palette.window_brush));
}

namespace {
constexpr UINT_PTR kHeaderSubclassId = 0x9001;

struct HeaderSubclassData {
  AppTheme theme{AppTheme::kDark};
  int sort_col{-1};
  bool sort_asc{true};
  int hover_col{-1};
};

LRESULT CALLBACK HeaderSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto* data = reinterpret_cast<HeaderSubclassData*>(dwRefData);

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);

      RECT client_rc;
      GetClientRect(hwnd, &client_rc);

      HDC mem_dc = CreateCompatibleDC(hdc);
      HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, client_rc.right, client_rc.bottom);
      HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bmp));

      bool is_dark = (!data || data->theme == AppTheme::kDark);

      COLORREF bg_color = is_dark ? RGB(36, 36, 40) : RGB(243, 243, 243);
      COLORREF bg_hover = is_dark ? RGB(52, 52, 58) : RGB(225, 225, 225);
      COLORREF border_color = is_dark ? RGB(60, 60, 68) : RGB(218, 218, 218);
      COLORREF text_color = is_dark ? RGB(245, 245, 245) : RGB(30, 30, 30);
      COLORREF arrow_color = is_dark ? RGB(56, 189, 248) : RGB(0, 103, 192);

      // Fill entire header background
      HBRUSH bg_brush = CreateSolidBrush(bg_color);
      FillRect(mem_dc, &client_rc, bg_brush);
      DeleteObject(bg_brush);

      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      if (!font) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, font));

      SetBkMode(mem_dc, TRANSPARENT);

      int count = Header_GetItemCount(hwnd);
      for (int i = 0; i < count; ++i) {
        RECT item_rc;
        Header_GetItemRect(hwnd, i, &item_rc);

        // Hover effect
        if (data && data->hover_col == i) {
          HBRUSH hbrush = CreateSolidBrush(bg_hover);
          FillRect(mem_dc, &item_rc, hbrush);
          DeleteObject(hbrush);
        }

        // Vertical divider line on right side of column
        HPEN pen = CreatePen(PS_SOLID, 1, border_color);
        HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, pen));
        MoveToEx(mem_dc, item_rc.right - 1, item_rc.top + 3, nullptr);
        LineTo(mem_dc, item_rc.right - 1, item_rc.bottom - 3);
        SelectObject(mem_dc, old_pen);
        DeleteObject(pen);

        // Item text & alignment
        wchar_t text[256] = {0};
        HDITEMW hdi = {0};
        hdi.mask = HDI_TEXT | HDI_FORMAT;
        hdi.pszText = text;
        hdi.cchTextMax = 256;
        Header_GetItem(hwnd, i, &hdi);

        RECT text_rc = item_rc;
        text_rc.left += 8;
        text_rc.right -= 8;

        // Draw Sort Indicator Arrow if active
        if (data && data->sort_col == i) {
          text_rc.right -= 14;
          RECT arrow_rc = item_rc;
          arrow_rc.left = text_rc.right;
          arrow_rc.right = item_rc.right - 4;
          SetTextColor(mem_dc, arrow_color);
          DrawTextW(mem_dc, data->sort_asc ? L"▲" : L"▼", -1, &arrow_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        UINT align = DT_LEFT;
        if (hdi.fmt & HDF_RIGHT) align = DT_RIGHT;
        else if (hdi.fmt & HDF_CENTER) align = DT_CENTER;

        SetTextColor(mem_dc, text_color);
        DrawTextW(mem_dc, text, -1, &text_rc, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      }

      // Bottom border line
      HPEN btm_pen = CreatePen(PS_SOLID, 1, border_color);
      HPEN old_btm_pen = static_cast<HPEN>(SelectObject(mem_dc, btm_pen));
      MoveToEx(mem_dc, client_rc.left, client_rc.bottom - 1, nullptr);
      LineTo(mem_dc, client_rc.right, client_rc.bottom - 1);
      SelectObject(mem_dc, old_btm_pen);
      DeleteObject(btm_pen);

      SelectObject(mem_dc, old_font);
      BitBlt(hdc, 0, 0, client_rc.right, client_rc.bottom, mem_dc, 0, 0, SRCCOPY);

      SelectObject(mem_dc, old_bmp);
      DeleteObject(mem_bmp);
      DeleteDC(mem_dc);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_MOUSEMOVE: {
      POINT pt = {LOWORD(lparam), HIWORD(lparam)};
      HDHITTESTINFO hdhti = {0};
      hdhti.pt = pt;
      int hit = static_cast<int>(SendMessageW(hwnd, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hdhti)));
      if (data && data->hover_col != hit) {
        data->hover_col = hit;
        InvalidateRect(hwnd, nullptr, FALSE);

        TRACKMOUSEEVENT tme = {0};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
      }
      break;
    }

    case WM_MOUSELEAVE: {
      if (data && data->hover_col != -1) {
        data->hover_col = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      break;
    }

    case WM_NCDESTROY: {
      if (data) {
        delete data;
      }
      RemoveWindowSubclass(hwnd, HeaderSubclassProc, uIdSubclass);
      break;
    }
  }

  return DefSubclassProc(hwnd, msg, wparam, lparam);
}
}  // namespace

void ThemeManager::ApplyListViewHeaderTheme(
    HWND listview_hwnd, AppTheme theme, int sort_column, bool sort_ascending) {
  if (!listview_hwnd) return;
  HWND header = ListView_GetHeader(listview_hwnd);
  if (!header) return;

  HeaderSubclassData* data = nullptr;
  DWORD_PTR ref_data = 0;
  if (GetWindowSubclass(header, HeaderSubclassProc, kHeaderSubclassId, &ref_data)) {
    data = reinterpret_cast<HeaderSubclassData*>(ref_data);
  } else {
    data = new HeaderSubclassData();
    SetWindowSubclass(header, HeaderSubclassProc, kHeaderSubclassId, reinterpret_cast<DWORD_PTR>(data));
  }

  if (data) {
    data->theme = theme;
    data->sort_col = sort_column;
    data->sort_asc = sort_ascending;
    InvalidateRect(header, nullptr, TRUE);
  }
}

HFONT ThemeManager::CreateAppFont(const std::wstring& font_name, int font_size_pt, bool bold) {
  HDC hdc = GetDC(nullptr);
  int log_y = GetDeviceCaps(hdc, LOGPIXELSY);
  ReleaseDC(nullptr, hdc);

  int height = -MulDiv(font_size_pt > 0 ? font_size_pt : 9, log_y, 72);
  const wchar_t* face = font_name.empty() ? L"Yu Gothic UI" : font_name.c_str();

  return CreateFontW(
      height, 0, 0, 0,
      bold ? FW_BOLD : FW_NORMAL,
      FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      face);
}

}  // namespace lite_proc_manager
