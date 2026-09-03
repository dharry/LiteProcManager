// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "toast_window.h"

#include <dwmapi.h>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace lite_proc_manager {

namespace {
constexpr UINT_PTR IDT_AUTO_CLOSE = 101;
constexpr UINT_PTR IDT_FADE_OUT = 102;
constexpr int TOAST_WIDTH = 360;
constexpr int TOAST_HEIGHT = 95;

std::vector<HWND> active_toasts;

void RepositionToasts() {
  RECT work_area = {0};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

  int margin_right = 16;
  int margin_bottom = 16;
  int gap = 10;

  int y = work_area.bottom - margin_bottom;

  // Clean up destroyed handles
  std::vector<HWND> valid;
  for (HWND h : active_toasts) {
    if (IsWindow(h)) {
      valid.push_back(h);
    }
  }
  active_toasts = valid;

  for (size_t i = 0; i < active_toasts.size(); ++i) {
    HWND h = active_toasts[active_toasts.size() - 1 - i];
    y -= TOAST_HEIGHT;
    int x = work_area.right - TOAST_WIDTH - margin_right;
    SetWindowPos(h, HWND_TOPMOST, x, y, TOAST_WIDTH, TOAST_HEIGHT, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    y -= gap;
  }
}
}  // namespace

void ToastWindow::ShowToast(
    EventLevel level,
    const std::wstring& title,
    const std::wstring& message,
    HWND main_hwnd) {
  auto* toast = new ToastWindow(level, title, message, main_hwnd);
  if (toast->hwnd_) {
    active_toasts.push_back(toast->hwnd_);
    RepositionToasts();
    // Play subtle system alert sound
    MessageBeep((level == EventLevel::kCritical) ? MB_ICONHAND : MB_ICONEXCLAMATION);
  }
}

ToastWindow::ToastWindow(
    EventLevel level,
    const std::wstring& title,
    const std::wstring& message,
    HWND main_hwnd)
    : level_(level), title_(title), message_(message), main_hwnd_(main_hwnd) {
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
  wc.lpszClassName = L"ProcessManagerToastWindow";

  RegisterClassExW(&wc);

  font_title_ = CreateFontW(
      16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  font_text_ = CreateFontW(
      13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  font_icon_ = CreateFontW(
      22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");

  RECT work_area = {0};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
  int x = work_area.right - TOAST_WIDTH - 16;
  int y = work_area.bottom - TOAST_HEIGHT - 16;

  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      wc.lpszClassName, L"ProcessManagerAlert",
      WS_POPUP | WS_CLIPSIBLINGS,
      x, y, TOAST_WIDTH, TOAST_HEIGHT,
      nullptr, nullptr, instance, this);

  if (hwnd_) {
    SetLayeredWindowAttributes(hwnd_, 0, 245, LWA_ALPHA);

    // Apply modern rounded corners on Windows 11
    DWM_WINDOW_CORNER_PREFERENCE corner_pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_pref, sizeof(corner_pref));

    SetTimer(hwnd_, IDT_AUTO_CLOSE, 5000, nullptr);  // 5 seconds auto-close
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
  }
}

ToastWindow::~ToastWindow() {
  if (font_title_) DeleteObject(font_title_);
  if (font_text_) DeleteObject(font_text_);
  if (font_icon_) DeleteObject(font_icon_);
}

LRESULT CALLBACK ToastWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<ToastWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_CREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = reinterpret_cast<ToastWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    return 0;
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ToastWindow::Close() {
  if (!closing_) {
    closing_ = true;
    KillTimer(hwnd_, IDT_AUTO_CLOSE);
    SetTimer(hwnd_, IDT_FADE_OUT, 20, nullptr);
  }
}

LRESULT ToastWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      Paint(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_LBUTTONUP: {
      // If close 'X' button clicked (top right 24x24)
      RECT rc;
      GetClientRect(hwnd, &rc);
      POINT pt = {LOWORD(lparam), HIWORD(lparam)};
      if (pt.x >= rc.right - 28 && pt.y <= 28) {
        Close();
        return 0;
      }

      // If body clicked, restore main window
      if (main_hwnd_ && IsWindow(main_hwnd_)) {
        ShowWindow(main_hwnd_, SW_SHOW);
        ShowWindow(main_hwnd_, SW_RESTORE);
        SetForegroundWindow(main_hwnd_);
      }
      Close();
      return 0;
    }

    case WM_TIMER: {
      if (wparam == IDT_AUTO_CLOSE) {
        Close();
      } else if (wparam == IDT_FADE_OUT) {
        alpha_ -= 25;
        if (alpha_ <= 0) {
          KillTimer(hwnd, IDT_FADE_OUT);
          DestroyWindow(hwnd);
        } else {
          SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha_), LWA_ALPHA);
        }
      }
      return 0;
    }

    case WM_DESTROY: {
      // Remove from active list
      for (auto it = active_toasts.begin(); it != active_toasts.end(); ++it) {
        if (*it == hwnd_) {
          active_toasts.erase(it);
          break;
        }
      }
      RepositionToasts();
      delete this;
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ToastWindow::Paint(HDC hdc) {
  RECT rc;
  GetClientRect(hwnd_, &rc);

  // Double buffering
  HDC mem_dc = CreateCompatibleDC(hdc);
  HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bmp));

  // Background color palette
  COLORREF bg_color = (level_ == EventLevel::kCritical) ? RGB(36, 20, 20) : RGB(36, 32, 20);
  COLORREF border_color = (level_ == EventLevel::kCritical) ? RGB(239, 68, 68) : RGB(245, 158, 11);
  COLORREF accent_color = (level_ == EventLevel::kCritical) ? RGB(239, 68, 68) : RGB(245, 158, 11);
  COLORREF title_color = (level_ == EventLevel::kCritical) ? RGB(252, 165, 165) : RGB(253, 224, 71);
  COLORREF text_color = RGB(241, 245, 249);

  // Draw Background Box
  HBRUSH bg_brush = CreateSolidBrush(bg_color);
  HPEN border_pen = CreatePen(PS_SOLID, 1, border_color);
  HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(mem_dc, bg_brush));
  HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, border_pen));

  RoundRect(mem_dc, 0, 0, rc.right, rc.bottom, 8, 8);

  // Left Accent Bar (5px)
  HBRUSH accent_brush = CreateSolidBrush(accent_color);
  RECT bar_rc = {0, 0, 6, rc.bottom};
  FillRect(mem_dc, &bar_rc, accent_brush);
  DeleteObject(accent_brush);

  // Icon (Emoji / Symbol)
  SetBkMode(mem_dc, TRANSPARENT);
  SelectObject(mem_dc, font_icon_);
  SetTextColor(mem_dc, accent_color);
  RECT icon_rc = {14, 18, 48, 60};
  const wchar_t* icon_text = (level_ == EventLevel::kCritical) ? L"⛔" : L"⚠️";
  DrawTextW(mem_dc, icon_text, -1, &icon_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // Title
  SelectObject(mem_dc, font_title_);
  SetTextColor(mem_dc, title_color);
  RECT title_rc = {52, 12, rc.right - 30, 32};
  DrawTextW(mem_dc, title_.c_str(), -1, &title_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

  // Message Body
  SelectObject(mem_dc, font_text_);
  SetTextColor(mem_dc, text_color);
  RECT text_rc = {52, 36, rc.right - 14, rc.bottom - 8};
  DrawTextW(mem_dc, message_.c_str(), -1, &text_rc, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

  // Close 'X' Button in top right
  SelectObject(mem_dc, font_title_);
  SetTextColor(mem_dc, RGB(148, 163, 184));
  RECT close_rc = {rc.right - 26, 6, rc.right - 6, 26};
  DrawTextW(mem_dc, L"✕", -1, &close_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // Copy to Screen
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);

  // Cleanup
  SelectObject(mem_dc, old_brush);
  SelectObject(mem_dc, old_pen);
  DeleteObject(bg_brush);
  DeleteObject(border_pen);
  SelectObject(mem_dc, old_bmp);
  DeleteObject(mem_bmp);
  DeleteDC(mem_dc);
}

}  // namespace lite_proc_manager
