// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "performance_view.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "performance_formatter.h"
#include "performance_graph_renderer.h"
#include "theme_manager.h"

namespace lite_proc_manager {

namespace {

constexpr int kSidebarWidth = 220;
constexpr int kSidebarPadding = 6;
constexpr int kCardHeight = 58;
constexpr int kCardGap = 2;
constexpr int kPanePaddingX = 20;
constexpr int kPanePaddingY = 14;
constexpr int kCoreGridGap = 6;
constexpr int kWheelScrollStep = 36;

constexpr UINT kMenuIdGraphLogical = 1;
constexpr UINT kMenuIdGraphOverall = 2;

class ScopedSaveDc {
 public:
  explicit ScopedSaveDc(HDC hdc)
      : hdc_(hdc), saved_(hdc ? SaveDC(hdc) : 0) {}
  ~ScopedSaveDc() {
    if (hdc_ && saved_ != 0) {
      RestoreDC(hdc_, saved_);
    }
  }
  ScopedSaveDc(const ScopedSaveDc&) = delete;
  ScopedSaveDc& operator=(const ScopedSaveDc&) = delete;

 private:
  HDC hdc_;
  int saved_;
};

class ScopedBrush {
 public:
  explicit ScopedBrush(COLORREF color) : brush_(CreateSolidBrush(color)) {}
  ~ScopedBrush() {
    if (brush_) DeleteObject(brush_);
  }
  ScopedBrush(const ScopedBrush&) = delete;
  ScopedBrush& operator=(const ScopedBrush&) = delete;
  HBRUSH Get() const { return brush_; }

 private:
  HBRUSH brush_;
};

class ScopedPen {
 public:
  ScopedPen(int style, int width, COLORREF color)
      : pen_(CreatePen(style, width, color)) {}
  ~ScopedPen() {
    if (pen_) DeleteObject(pen_);
  }
  ScopedPen(const ScopedPen&) = delete;
  ScopedPen& operator=(const ScopedPen&) = delete;
  HPEN Get() const { return pen_; }

 private:
  HPEN pen_;
};

class ScopedSelectObject {
 public:
  ScopedSelectObject(HDC hdc, HGDIOBJ obj)
      : hdc_(hdc), old_obj_(hdc && obj ? SelectObject(hdc, obj) : nullptr) {}
  ~ScopedSelectObject() {
    if (hdc_ && old_obj_) {
      SelectObject(hdc_, old_obj_);
    }
  }
  ScopedSelectObject(const ScopedSelectObject&) = delete;
  ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

 private:
  HDC hdc_;
  HGDIOBJ old_obj_;
};

class ScopedCompatibleDc {
 public:
  explicit ScopedCompatibleDc(HDC hdc) : dc_(CreateCompatibleDC(hdc)) {}
  ~ScopedCompatibleDc() {
    if (dc_) DeleteDC(dc_);
  }
  ScopedCompatibleDc(const ScopedCompatibleDc&) = delete;
  ScopedCompatibleDc& operator=(const ScopedCompatibleDc&) = delete;
  HDC Get() const { return dc_; }

 private:
  HDC dc_;
};

class ScopedCompatibleBitmap {
 public:
  ScopedCompatibleBitmap(HDC hdc, int width, int height)
      : bmp_(CreateCompatibleBitmap(hdc, width, height)) {}
  ~ScopedCompatibleBitmap() {
    if (bmp_) DeleteObject(bmp_);
  }
  ScopedCompatibleBitmap(const ScopedCompatibleBitmap&) = delete;
  ScopedCompatibleBitmap& operator=(const ScopedCompatibleBitmap&) = delete;
  HBITMAP Get() const { return bmp_; }

 private:
  HBITMAP bmp_;
};

class ScopedMenu {
 public:
  ScopedMenu() : menu_(CreatePopupMenu()) {}
  ~ScopedMenu() {
    if (menu_) DestroyMenu(menu_);
  }
  ScopedMenu(const ScopedMenu&) = delete;
  ScopedMenu& operator=(const ScopedMenu&) = delete;
  HMENU Get() const { return menu_; }

 private:
  HMENU menu_;
};

struct KpiItem {
  std::wstring label;
  std::wstring value;
  bool show_bar{false};
  COLORREF bar_color{0};
  bool bar_dashed{false};
};

struct AttrItem {
  std::wstring key;
  std::wstring value;
};

int FormatRoundedPercent(double percent) {
  const double clamped = std::clamp(percent, 0.0, 100.0);
  return static_cast<int>(std::lround(clamped));
}

void DrawSolidLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color) {
  ScopedPen pen(PS_SOLID, 1, color);
  ScopedSelectObject sel(hdc, pen.Get());
  MoveToEx(hdc, x1, y1, nullptr);
  LineTo(hdc, x2, y2);
}

void DrawKpiGrid(HDC hdc, const RECT& area, int cols, HFONT label_font,
                 HFONT value_font, COLORREF label_color, COLORREF value_color,
                 const std::vector<KpiItem>& items) {
  if (items.empty()) return;
  const int area_w = static_cast<int>(area.right - area.left);
  const int area_h = static_cast<int>(area.bottom - area.top);
  if (area_w <= 0 || area_h <= 0) return;

  const int safe_cols = std::max(1, cols);
  const int count = static_cast<int>(items.size());
  const int rows = (count + safe_cols - 1) / safe_cols;

  constexpr int kColGap = 16;
  constexpr int kRowGap = 8;
  const int cell_w = std::max(1, (area_w - (safe_cols - 1) * kColGap) / safe_cols);
  const int cell_h = std::max(1, (area_h - (rows - 1) * kRowGap) / rows);
  const int label_h = std::min(18, std::max(12, cell_h * 38 / 100));

  for (int i = 0; i < count; ++i) {
    const int r = i / safe_cols;
    const int c = i % safe_cols;
    const int cell_x = static_cast<int>(area.left) + c * (cell_w + kColGap);
    const int cell_y = static_cast<int>(area.top) + r * (cell_h + kRowGap);
    const auto& item = items[static_cast<size_t>(i)];

    int text_x = cell_x;
    if (item.show_bar) {
      const int bar_top = cell_y + 2;
      const int bar_bottom =
          std::min(cell_y + cell_h - 4, cell_y + label_h + 22);
      if (bar_bottom > bar_top) {
        ScopedBrush bar_brush(item.bar_color);
        if (item.bar_dashed) {
          const int seg_h = std::max(3, (bar_bottom - bar_top) / 5);
          for (int y = bar_top; y < bar_bottom; y += seg_h * 2) {
            RECT seg_rc{cell_x, y, cell_x + 3, std::min(bar_bottom, y + seg_h)};
            FillRect(hdc, &seg_rc, bar_brush.Get());
          }
        } else {
          RECT bar_rc{cell_x, bar_top, cell_x + 3, bar_bottom};
          FillRect(hdc, &bar_rc, bar_brush.Get());
        }
      }
      text_x += 9;
    }

    const int text_right = cell_x + cell_w;
    if (text_right <= text_x) continue;

    RECT label_rc{text_x, cell_y, text_right, cell_y + label_h};
    {
      ScopedSelectObject sel_label(hdc, label_font);
      SetTextColor(hdc, label_color);
      DrawTextW(hdc, item.label.c_str(), -1, &label_rc,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }

    RECT value_rc{text_x, cell_y + label_h, text_right, cell_y + cell_h};
    if (value_rc.bottom > value_rc.top) {
      ScopedSelectObject sel_value(hdc, value_font);
      SetTextColor(hdc, value_color);
      DrawTextW(hdc, item.value.c_str(), -1, &value_rc,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }
  }
}

void DrawAttributeList(HDC hdc, const RECT& area, HFONT font,
                       COLORREF key_color, COLORREF value_color,
                       const std::vector<AttrItem>& items) {
  if (items.empty()) return;
  const int area_w = static_cast<int>(area.right - area.left);
  const int area_h = static_cast<int>(area.bottom - area.top);
  if (area_w <= 20 || area_h <= 12) return;

  const int count = static_cast<int>(items.size());
  const int row_h = std::min(22, std::max(14, area_h / count));
  const int key_w = std::min(165, std::max(80, area_w * 48 / 100));

  ScopedSelectObject sel_font(hdc, font);
  for (int i = 0; i < count; ++i) {
    const int y = static_cast<int>(area.top) + i * row_h;
    if (y + row_h > area.bottom + 2) break;

    const auto& item = items[static_cast<size_t>(i)];
    RECT key_rc{area.left, y, area.left + key_w, y + row_h};
    SetTextColor(hdc, key_color);
    DrawTextW(hdc, item.key.c_str(), -1, &key_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

    const int val_left = area.left + key_w + 8;
    if (area.right > val_left) {
      RECT val_rc{val_left, y, area.right, y + row_h};
      SetTextColor(hdc, value_color);
      DrawTextW(hdc, item.value.c_str(), -1, &val_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }
  }
}

}  // namespace

PerformanceView::~PerformanceView() {
  Destroy();
}

bool PerformanceView::Create(HWND parent, HINSTANCE instance, int control_id) {
  if (!parent) return false;
  if (hwnd_) {
    Destroy();
  }

  HINSTANCE mod_instance = instance ? instance : GetModuleHandleW(nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  if (!GetClassInfoExW(mod_instance, kWindowClassName, &wc)) {
    wc = WNDCLASSEXW{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = mod_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  hwnd_ = CreateWindowExW(
      0, kWindowClassName, L"",
      WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      mod_instance, this);

  return hwnd_ != nullptr;
}

void PerformanceView::Destroy() {
  if (hwnd_) {
    HWND target = hwnd_;
    hwnd_ = nullptr;
    if (IsWindow(target)) {
      DestroyWindow(target);
    }
  }
  DeleteFonts();
}

void PerformanceView::UpdateSnapshot(const PerformanceSnapshot& snapshot) {
  snapshot_ = snapshot;

  if (selected_type_ == PerformanceResourceType::kNetwork) {
    if (snapshot_.network_adapters.empty()) {
      selected_type_ = PerformanceResourceType::kCpu;
      selected_network_luid_ = 0;
    } else if (!GetSelectedNetworkAdapter()) {
      selected_network_luid_ = snapshot_.network_adapters.front().luid_value;
    }
  }

  if (hwnd_) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    ClampSidebarScroll(static_cast<int>(rc.bottom - rc.top));
    InvalidateView();
  }
}

void PerformanceView::SetTheme(AppTheme theme) {
  if (theme_ == theme) return;
  theme_ = theme;
  InvalidateView();
}

void PerformanceView::SetFonts(HFONT ui_font,
                               const std::wstring& ui_font_name) {
  std::wstring resolved_name = ui_font_name;
  int resolved_height = 0;
  if (ui_font) {
    LOGFONTW lf{};
    if (GetObjectW(ui_font, sizeof(LOGFONTW), &lf) == sizeof(LOGFONTW)) {
      if (resolved_name.empty() && lf.lfFaceName[0] != L'\0') {
        resolved_name = lf.lfFaceName;
      }
      if (lf.lfHeight != 0) {
        resolved_height = lf.lfHeight;
      }
    }
  }

  if (resolved_name.empty()) {
    resolved_name = L"Yu Gothic UI";
  }

  ui_font_name_ = resolved_name;
  base_font_height_ = resolved_height;

  DeleteFonts();
  EnsureFonts();
  InvalidateView();
}

void PerformanceView::SetStrings(const PerformanceStrings& strings) {
  strings_ = strings;
  InvalidateView();
}

void PerformanceView::SetCpuGraphMode(CpuGraphMode mode) {
  if (cpu_graph_mode_ == mode) return;
  cpu_graph_mode_ = mode;
  InvalidateView();
}

LRESULT CALLBACK PerformanceView::WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                             LPARAM lparam) {
  auto* self = reinterpret_cast<PerformanceView*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_CREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = reinterpret_cast<PerformanceView*>(cs->lpCreateParams);
    if (self) {
      self->hwnd_ = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->EnsureFonts();
    }
    return 0;
  }

  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (self) {
      self->hwnd_ = nullptr;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT PerformanceView::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client_rc{};
      GetClientRect(hwnd, &client_rc);
      const int width = static_cast<int>(client_rc.right - client_rc.left);
      const int height = static_cast<int>(client_rc.bottom - client_rc.top);
      if (width > 0 && height > 0) {
        ScopedCompatibleDc mem_dc(hdc);
        ScopedCompatibleBitmap mem_bmp(hdc, width, height);
        if (mem_dc.Get() && mem_bmp.Get()) {
          ScopedSelectObject sel_bmp(mem_dc.Get(), mem_bmp.Get());
          Paint(mem_dc.Get(), client_rc);
          BitBlt(hdc, 0, 0, width, height, mem_dc.Get(), 0, 0, SRCCOPY);
        } else {
          Paint(hdc, client_rc);
        }
      }
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SIZE: {
      const int client_h = HIWORD(lparam);
      ClampSidebarScroll(client_h);
      InvalidateView();
      return 0;
    }

    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int hit = HitTestSidebarCard(pt);
      if (hit >= 0) {
        SelectCard(hit);
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        if (TrackMouseEvent(&tme)) {
          tracking_mouse_leave_ = true;
        }
      }
      const int hit = HitTestSidebarCard(pt);
      if (hit != hovered_card_index_) {
        hovered_card_index_ = hit;
        InvalidateView();
      }
      return 0;
    }

    case WM_MOUSELEAVE: {
      tracking_mouse_leave_ = false;
      if (hovered_card_index_ != -1) {
        hovered_card_index_ = -1;
        InvalidateView();
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int client_h = static_cast<int>(rc.bottom - rc.top);
      const int max_scroll = GetMaxSidebarScrollY(client_h);
      if (max_scroll > 0) {
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &pt);
        if (pt.x >= 0 && pt.x < kSidebarWidth) {
          const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
          const int step = MulDiv(delta, kWheelScrollStep, WHEEL_DELTA);
          const int next_scroll =
              std::clamp(sidebar_scroll_y_ - step, 0, max_scroll);
          if (next_scroll != sidebar_scroll_y_) {
            sidebar_scroll_y_ = next_scroll;
            InvalidateView();
          }
        }
      }
      return 0;
    }

    case WM_CONTEXTMENU: {
      if (selected_type_ != PerformanceResourceType::kCpu) {
        return 0;
      }

      POINT screen_pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (screen_pt.x == -1 && screen_pt.y == -1) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        POINT pt{std::min<LONG>(rc.right, kSidebarWidth + 40), 40};
        ClientToScreen(hwnd, &pt);
        screen_pt = pt;
      } else {
        POINT client_pt = screen_pt;
        ScreenToClient(hwnd, &client_pt);
        if (client_pt.x < kSidebarWidth) {
          return 0;
        }
      }

      ShowCpuContextMenu(screen_pt);
      return 0;
    }

    case WM_DESTROY:
      tracking_mouse_leave_ = false;
      DeleteFonts();
      return 0;
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void PerformanceView::EnsureFonts() {
  if (title_font_ && kpi_font_ && normal_font_ && small_font_) {
    return;
  }

  HDC screen_dc = GetDC(nullptr);
  const int log_y = screen_dc ? GetDeviceCaps(screen_dc, LOGPIXELSY) : 96;
  if (screen_dc) {
    ReleaseDC(nullptr, screen_dc);
  }

  const wchar_t* face =
      ui_font_name_.empty() ? L"Yu Gothic UI" : ui_font_name_.c_str();

  if (!title_font_) {
    title_font_ = ThemeManager::CreateAppFont(face, 18, false);
  }
  if (!kpi_font_) {
    kpi_font_ = ThemeManager::CreateAppFont(face, 15, false);
  }
  if (!normal_font_) {
    if (base_font_height_ != 0) {
      normal_font_ = CreateFontW(
          base_font_height_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    } else {
      normal_font_ = ThemeManager::CreateAppFont(face, 9, false);
    }
  }
  if (!small_font_) {
    // 8.5 pt = 17 half-points (144 half-points per inch).
    const int small_height = -MulDiv(17, log_y, 144);
    small_font_ = CreateFontW(
        small_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
  }
}

void PerformanceView::DeleteFonts() {
  if (title_font_) {
    DeleteObject(title_font_);
    title_font_ = nullptr;
  }
  if (kpi_font_) {
    DeleteObject(kpi_font_);
    kpi_font_ = nullptr;
  }
  if (normal_font_) {
    DeleteObject(normal_font_);
    normal_font_ = nullptr;
  }
  if (small_font_) {
    DeleteObject(small_font_);
    small_font_ = nullptr;
  }
}

void PerformanceView::InvalidateView() {
  if (hwnd_ && IsWindow(hwnd_)) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

int PerformanceView::GetCardCount() const {
  return 2 + static_cast<int>(snapshot_.network_adapters.size());
}

int PerformanceView::GetSelectedCardIndex() const {
  if (selected_type_ == PerformanceResourceType::kCpu) return 0;
  if (selected_type_ == PerformanceResourceType::kMemory) return 1;
  for (size_t i = 0; i < snapshot_.network_adapters.size(); ++i) {
    if (snapshot_.network_adapters[i].luid_value == selected_network_luid_) {
      return 2 + static_cast<int>(i);
    }
  }
  return snapshot_.network_adapters.empty() ? 0 : 2;
}

int PerformanceView::GetMaxSidebarScrollY(int client_height) const {
  const int card_count = GetCardCount();
  const int content_h = kSidebarPadding * 2 + card_count * kCardHeight +
                        std::max(0, card_count - 1) * kCardGap;
  return std::max(0, content_h - std::max(0, client_height));
}

void PerformanceView::ClampSidebarScroll(int client_height) {
  const int max_scroll = GetMaxSidebarScrollY(client_height);
  sidebar_scroll_y_ = std::clamp(sidebar_scroll_y_, 0, max_scroll);
}

RECT PerformanceView::GetCardRect(int card_index) const {
  const int top = kSidebarPadding +
                  card_index * (kCardHeight + kCardGap) -
                  sidebar_scroll_y_;
  return RECT{kSidebarPadding, top, kSidebarWidth - 1 - kSidebarPadding,
              top + kCardHeight};
}

int PerformanceView::HitTestSidebarCard(POINT client_pt) const {
  if (!hwnd_) return -1;
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int sidebar_right = std::min<int>(kSidebarWidth - 1, rc.right);
  if (client_pt.x < 0 || client_pt.x >= sidebar_right ||
      client_pt.y < 0 || client_pt.y >= rc.bottom) {
    return -1;
  }

  const int card_count = GetCardCount();
  for (int i = 0; i < card_count; ++i) {
    const RECT card_rc = GetCardRect(i);
    if (PtInRect(&card_rc, client_pt)) {
      return i;
    }
  }
  return -1;
}

void PerformanceView::SelectCard(int card_index) {
  if (card_index == 0) {
    if (selected_type_ == PerformanceResourceType::kCpu) return;
    selected_type_ = PerformanceResourceType::kCpu;
    InvalidateView();
    return;
  }
  if (card_index == 1) {
    if (selected_type_ == PerformanceResourceType::kMemory) return;
    selected_type_ = PerformanceResourceType::kMemory;
    InvalidateView();
    return;
  }

  const int adapter_idx = card_index - 2;
  if (adapter_idx >= 0 &&
      static_cast<size_t>(adapter_idx) < snapshot_.network_adapters.size()) {
    const uint64_t next_luid =
        snapshot_.network_adapters[static_cast<size_t>(adapter_idx)].luid_value;
    if (selected_type_ == PerformanceResourceType::kNetwork &&
        selected_network_luid_ == next_luid) {
      return;
    }
    selected_type_ = PerformanceResourceType::kNetwork;
    selected_network_luid_ = next_luid;
    InvalidateView();
  }
}

const NetworkAdapterSample* PerformanceView::GetSelectedNetworkAdapter() const {
  if (snapshot_.network_adapters.empty()) return nullptr;
  for (const auto& adapter : snapshot_.network_adapters) {
    if (adapter.luid_value == selected_network_luid_) {
      return &adapter;
    }
  }
  return &snapshot_.network_adapters.front();
}

const NetworkAdapterHistory* PerformanceView::FindNetworkHistory(
    uint64_t luid_value) const {
  for (const auto& history : snapshot_.network_histories) {
    if (history.luid_value == luid_value) {
      return &history;
    }
  }
  return nullptr;
}

double PerformanceView::ComputeNetworkPeakBps(
    const NetworkAdapterHistory* history) const {
  if (!history) return 0.0;
  double peak = 0.0;
  const size_t recv_n = history->recv_bps_history.Size();
  for (size_t i = 0; i < recv_n; ++i) {
    peak = std::max(peak, history->recv_bps_history.At(i));
  }
  const size_t send_n = history->send_bps_history.Size();
  for (size_t i = 0; i < send_n; ++i) {
    peak = std::max(peak, history->send_bps_history.At(i));
  }
  return peak;
}

void PerformanceView::ShowCpuContextMenu(POINT screen_pt) {
  ScopedMenu menu;
  if (!menu.Get()) return;

  const UINT logical_flags =
      MF_STRING | (cpu_graph_mode_ == CpuGraphMode::kLogicalProcessors
                       ? MF_CHECKED
                       : MF_UNCHECKED);
  const UINT overall_flags =
      MF_STRING | (cpu_graph_mode_ == CpuGraphMode::kOverall ? MF_CHECKED
                                                             : MF_UNCHECKED);

  AppendMenuW(menu.Get(), logical_flags, kMenuIdGraphLogical,
              strings_.menu_graph_logical.c_str());
  AppendMenuW(menu.Get(), overall_flags, kMenuIdGraphOverall,
              strings_.menu_graph_overall.c_str());

  const BOOL cmd = TrackPopupMenuEx(
      menu.Get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
      screen_pt.x, screen_pt.y, hwnd_, nullptr);

  if (cmd == static_cast<BOOL>(kMenuIdGraphLogical)) {
    SetCpuGraphMode(CpuGraphMode::kLogicalProcessors);
  } else if (cmd == static_cast<BOOL>(kMenuIdGraphOverall)) {
    SetCpuGraphMode(CpuGraphMode::kOverall);
  }
}

void PerformanceView::Paint(HDC hdc, const RECT& client_rc) {
  EnsureFonts();
  SetBkMode(hdc, TRANSPARENT);

  const int width = static_cast<int>(client_rc.right - client_rc.left);
  const int height = static_cast<int>(client_rc.bottom - client_rc.top);
  if (width <= 0 || height <= 0) return;

  const int sidebar_w = std::min(kSidebarWidth, width);
  const RECT sidebar_rc{0, 0, sidebar_w, height};
  PaintSidebar(hdc, sidebar_rc);

  if (width > sidebar_w) {
    const RECT detail_rc{sidebar_w, 0, width, height};
    PaintDetailPane(hdc, detail_rc);
  }
}

void PerformanceView::PaintSidebar(HDC hdc, const RECT& sidebar_rc) {
  const auto& palette = ThemeManager::GetPalette(theme_);
  {
    ScopedBrush bg_brush(palette.strip_background);
    FillRect(hdc, &sidebar_rc, bg_brush.Get());
  }

  // Vertical divider between sidebar and detail pane.
  if (sidebar_rc.right > sidebar_rc.left) {
    DrawSolidLine(hdc, sidebar_rc.right - 1, sidebar_rc.top,
                  sidebar_rc.right - 1, sidebar_rc.bottom,
                  palette.border_color);
  }

  ScopedSaveDc save_dc(hdc);
  IntersectClipRect(hdc, sidebar_rc.left, sidebar_rc.top,
                    std::max(sidebar_rc.left, sidebar_rc.right - 1),
                    sidebar_rc.bottom);

  const int card_count = GetCardCount();
  const int selected_idx = GetSelectedCardIndex();
  for (int i = 0; i < card_count; ++i) {
    const RECT card_rc = GetCardRect(i);
    if (card_rc.bottom <= sidebar_rc.top || card_rc.top >= sidebar_rc.bottom) {
      continue;
    }
    PaintSidebarCard(hdc, card_rc, i, i == selected_idx,
                     i == hovered_card_index_);
  }
}

void PerformanceView::PaintSidebarCard(HDC hdc, const RECT& card_rc,
                                       int card_index, bool is_selected,
                                       bool is_hovered) {
  const auto& palette = ThemeManager::GetPalette(theme_);

  if (is_selected) {
    ScopedBrush sel_brush(palette.selected_item);
    FillRect(hdc, &card_rc, sel_brush.Get());

    RECT accent_rc{card_rc.left, card_rc.top + 6, card_rc.left + 3,
                   card_rc.bottom - 6};
    ScopedBrush accent_brush(palette.accent_color);
    FillRect(hdc, &accent_rc, accent_brush.Get());
  } else if (is_hovered) {
    ScopedBrush hover_brush(palette.control_hover);
    FillRect(hdc, &card_rc, hover_brush.Get());
  }

  constexpr int kSparkWidth = 54;
  constexpr int kSparkHeight = 38;
  const int spark_top =
      static_cast<int>(card_rc.top) +
      (static_cast<int>(card_rc.bottom - card_rc.top) - kSparkHeight) / 2;
  const RECT spark_rc{card_rc.left + 8, spark_top, card_rc.left + 8 + kSparkWidth,
                      spark_top + kSparkHeight};

  std::wstring title;
  std::wstring subtitle;
  std::array<double, 60> spark_buf{};

  if (card_index == 0) {
    const GraphStyle style = PerformanceGraphRenderer::GetCpuStyle(theme_);
    const size_t n =
        snapshot_.cpu_overall_history.CopyTo(spark_buf.data(), spark_buf.size());
    PerformanceGraphRenderer::DrawMiniSparkline(
        hdc, spark_rc, spark_buf.data(), n,
        snapshot_.cpu_overall_history.Capacity(), 100.0, style);

    title = strings_.cpu_title;
    const int pct = FormatRoundedPercent(snapshot_.cpu.overall_usage_percent);
    subtitle = std::to_wstring(pct) + L"%";
    if (snapshot_.cpu.current_speed_mhz > 0) {
      subtitle +=
          L"  " +
          PerformanceFormatter::FormatFrequencyGHz(
              snapshot_.cpu.current_speed_mhz);
    }
  } else if (card_index == 1) {
    const GraphStyle style = PerformanceGraphRenderer::GetMemoryStyle(theme_);
    const size_t n =
        snapshot_.memory_usage_history.CopyTo(spark_buf.data(), spark_buf.size());
    PerformanceGraphRenderer::DrawMiniSparkline(
        hdc, spark_rc, spark_buf.data(), n,
        snapshot_.memory_usage_history.Capacity(), 100.0, style);

    title = strings_.memory_title;
    const int pct = FormatRoundedPercent(snapshot_.memory.usage_percent);
    subtitle =
        PerformanceFormatter::FormatMemoryGB(snapshot_.memory.in_use_bytes) +
        L" / " +
        PerformanceFormatter::FormatMemoryGB(
            snapshot_.memory.total_physical_bytes) +
        L" (" + std::to_wstring(pct) + L"%)";
  } else {
    const size_t adapter_idx = static_cast<size_t>(card_index - 2);
    if (adapter_idx < snapshot_.network_adapters.size()) {
      const auto& adapter = snapshot_.network_adapters[adapter_idx];
      const GraphStyle style =
          PerformanceGraphRenderer::GetNetworkStyle(theme_);
      const NetworkAdapterHistory* hist = FindNetworkHistory(adapter.luid_value);
      size_t count = 0;
      double scale_bps = 1.0;
      if (hist) {
        std::array<double, 60> recv_buf{};
        std::array<double, 60> send_buf{};
        const size_t recv_n =
            hist->recv_bps_history.CopyTo(recv_buf.data(), recv_buf.size());
        const size_t send_n =
            hist->send_bps_history.CopyTo(send_buf.data(), send_buf.size());
        count = std::min(recv_n, send_n);
        double peak = 0.0;
        for (size_t i = 0; i < count; ++i) {
          spark_buf[i] = std::max(recv_buf[i], send_buf[i]);
          peak = std::max(peak, spark_buf[i]);
        }
        scale_bps = PerformanceFormatter::ComputeNetworkAutoScale(peak);
      }
      PerformanceGraphRenderer::DrawMiniSparkline(
          hdc, spark_rc, spark_buf.data(), count, 60, scale_bps, style);

      title = adapter.friendly_name.empty() ? strings_.network_fallback_title
                                            : adapter.friendly_name;
      subtitle = strings_.send + L": " +
                 PerformanceFormatter::FormatBitrate(adapter.send_bps) +
                 L"  " + strings_.receive + L": " +
                 PerformanceFormatter::FormatBitrate(adapter.recv_bps);
    }
  }

  const COLORREF title_color =
      is_selected ? palette.selected_item_text : palette.text_primary;
  const COLORREF sub_color =
      is_selected ? palette.selected_item_text : palette.text_secondary;

  const int text_left = spark_rc.right + 10;
  const int text_right = card_rc.right - 8;
  if (text_right > text_left) {
    RECT title_rc{text_left, card_rc.top + 8, text_right, card_rc.top + 28};
    {
      ScopedSelectObject sel(hdc, normal_font_);
      SetTextColor(hdc, title_color);
      DrawTextW(hdc, title.c_str(), -1, &title_rc,
                DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }

    RECT sub_rc{text_left, card_rc.top + 30, text_right, card_rc.bottom - 8};
    {
      ScopedSelectObject sel(hdc, small_font_);
      SetTextColor(hdc, sub_color);
      DrawTextW(hdc, subtitle.c_str(), -1, &sub_rc,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }
  }
}

void PerformanceView::PaintDetailPane(HDC hdc, const RECT& detail_rc) {
  const auto& palette = ThemeManager::GetPalette(theme_);
  {
    ScopedBrush bg_brush(palette.surface_background);
    FillRect(hdc, &detail_rc, bg_brush.Get());
  }

  const int detail_w = static_cast<int>(detail_rc.right - detail_rc.left);
  const int detail_h = static_cast<int>(detail_rc.bottom - detail_rc.top);
  if (detail_w < 80 || detail_h < 80) return;

  const int pad_x = std::min(kPanePaddingX, detail_w / 10);
  const int pad_y = std::min(kPanePaddingY, detail_h / 10);
  const RECT content_rc{detail_rc.left + pad_x, detail_rc.top + pad_y,
                        detail_rc.right - pad_x, detail_rc.bottom - pad_y};
  if (content_rc.right <= content_rc.left ||
      content_rc.bottom <= content_rc.top) {
    return;
  }

  switch (selected_type_) {
    case PerformanceResourceType::kCpu:
      PaintCpuDetail(hdc, content_rc);
      break;
    case PerformanceResourceType::kMemory:
      PaintMemoryDetail(hdc, content_rc);
      break;
    case PerformanceResourceType::kNetwork:
      PaintNetworkDetail(hdc, content_rc);
      break;
  }
}

void PerformanceView::PaintDetailHeader(HDC hdc, const RECT& content_rc,
                                        const std::wstring& title,
                                        const std::wstring& subtitle,
                                        const std::wstring& graph_caption,
                                        const std::wstring& scale_label,
                                        int* out_body_top) const {
  const auto& palette = ThemeManager::GetPalette(theme_);
  const int content_w = static_cast<int>(content_rc.right - content_rc.left);
  constexpr int kHeaderHeight = 34;
  constexpr int kSubHeaderHeight = 18;

  RECT title_measure{0, 0, content_w / 2, kHeaderHeight};
  {
    ScopedSelectObject sel(hdc, title_font_);
    DrawTextW(hdc, title.c_str(), -1, &title_measure,
              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
  }
  const int title_w =
      std::min(content_w * 45 / 100,
               static_cast<int>(title_measure.right - title_measure.left) + 12);

  RECT title_rc{content_rc.left, content_rc.top, content_rc.left + title_w,
                content_rc.top + kHeaderHeight};
  {
    ScopedSelectObject sel(hdc, title_font_);
    SetTextColor(hdc, palette.text_primary);
    DrawTextW(hdc, title.c_str(), -1, &title_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
  }

  if (!subtitle.empty() && title_rc.right + 12 < content_rc.right) {
    RECT sub_rc{title_rc.right + 12, content_rc.top, content_rc.right,
                content_rc.top + kHeaderHeight};
    ScopedSelectObject sel(hdc, normal_font_);
    SetTextColor(hdc, palette.text_secondary);
    DrawTextW(hdc, subtitle.c_str(), -1, &sub_rc,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
  }

  const int sub_top = content_rc.top + kHeaderHeight + 4;
  RECT scale_measure{0, 0, content_w / 2, kSubHeaderHeight};
  {
    ScopedSelectObject sel(hdc, small_font_);
    DrawTextW(hdc, scale_label.c_str(), -1, &scale_measure,
              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
  }
  const int scale_w = std::min(
      content_w / 2,
      static_cast<int>(scale_measure.right - scale_measure.left) + 8);

  RECT caption_rc{content_rc.left, sub_top,
                  std::max(content_rc.left, content_rc.right - scale_w - 8),
                  sub_top + kSubHeaderHeight};
  if (caption_rc.right > caption_rc.left) {
    ScopedSelectObject sel(hdc, small_font_);
    SetTextColor(hdc, palette.text_secondary);
    DrawTextW(hdc, graph_caption.c_str(), -1, &caption_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
  }

  if (!scale_label.empty()) {
    RECT scale_rc{content_rc.right - scale_w, sub_top, content_rc.right,
                  sub_top + kSubHeaderHeight};
    ScopedSelectObject sel(hdc, small_font_);
    SetTextColor(hdc, palette.text_secondary);
    DrawTextW(hdc, scale_label.c_str(), -1, &scale_rc,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  if (out_body_top) {
    *out_body_top = sub_top + kSubHeaderHeight + 4;
  }
}

void PerformanceView::PaintCpuDetail(HDC hdc, const RECT& content_rc) {
  const auto& palette = ThemeManager::GetPalette(theme_);
  const GraphStyle cpu_style = PerformanceGraphRenderer::GetCpuStyle(theme_);

  int body_top = content_rc.top;
  PaintDetailHeader(hdc, content_rc, strings_.cpu_title,
                    snapshot_.cpu_static.brand_name,
                    strings_.graph_60s_cpu_pct, L"100%", &body_top);

  const int content_w = static_cast<int>(content_rc.right - content_rc.left);
  const int avail_h = static_cast<int>(content_rc.bottom - body_top);
  if (avail_h <= 20) return;

  const int stats_h =
      (avail_h >= 280) ? 162 : std::max(84, avail_h * 42 / 100);
  constexpr int kStatsTopGap = 14;
  const int graph_bottom =
      std::max(body_top, static_cast<int>(content_rc.bottom) - stats_h - kStatsTopGap);

  const RECT graph_rc{content_rc.left, body_top, content_rc.right, graph_bottom};
  if (graph_rc.bottom - graph_rc.top > 12 &&
      graph_rc.right - graph_rc.left > 12) {
    std::array<double, 60> buf{};
    if (cpu_graph_mode_ == CpuGraphMode::kLogicalProcessors &&
        !snapshot_.cpu_core_histories.empty()) {
      const size_t core_count = snapshot_.cpu_core_histories.size();
      const CpuGridLayout grid =
          PerformanceFormatter::ComputeCpuGridLayout(core_count);
      const int rows = std::max(1, grid.rows);
      const int cols = std::max(1, grid.cols);

      const int total_w = static_cast<int>(graph_rc.right - graph_rc.left);
      const int total_h = static_cast<int>(graph_rc.bottom - graph_rc.top);
      const int cell_w = (total_w - (cols - 1) * kCoreGridGap) / cols;
      const int cell_h = (total_h - (rows - 1) * kCoreGridGap) / rows;

      if (cell_w > 4 && cell_h > 4) {
        for (size_t i = 0; i < core_count; ++i) {
          const int r = static_cast<int>(i) / cols;
          const int c = static_cast<int>(i) % cols;
          const int x = static_cast<int>(graph_rc.left) +
                        c * (cell_w + kCoreGridGap);
          const int y = static_cast<int>(graph_rc.top) +
                        r * (cell_h + kCoreGridGap);
          const RECT cell_rc{x, y, x + cell_w, y + cell_h};
          const auto& core_hist = snapshot_.cpu_core_histories[i];
          const size_t n = core_hist.CopyTo(buf.data(), buf.size());
          PerformanceGraphRenderer::DrawTimeSeriesGraph(
              hdc, cell_rc, buf.data(), n, core_hist.Capacity(), 100.0,
              cpu_style);
        }
      }
    } else {
      const size_t n =
          snapshot_.cpu_overall_history.CopyTo(buf.data(), buf.size());
      PerformanceGraphRenderer::DrawTimeSeriesGraph(
          hdc, graph_rc, buf.data(), n,
          snapshot_.cpu_overall_history.Capacity(), 100.0, cpu_style);
    }
  }

  const int stats_top = graph_bottom + kStatsTopGap;
  if (stats_top >= content_rc.bottom) return;

  constexpr int kStatsGapX = 24;
  const int kpi_w =
      (content_w >= 360) ? std::max(120, (content_w - kStatsGapX) * 55 / 100)
                         : content_w;
  const RECT kpi_rc{content_rc.left, stats_top, content_rc.left + kpi_w,
                    content_rc.bottom};

  const int cpu_pct = FormatRoundedPercent(snapshot_.cpu.overall_usage_percent);
  const std::vector<KpiItem> kpis{
      {strings_.utilization, std::to_wstring(cpu_pct) + L"%"},
      {strings_.speed,
       PerformanceFormatter::FormatFrequencyGHz(
           snapshot_.cpu.current_speed_mhz)},
      {strings_.processes,
       PerformanceFormatter::FormatIntegerWithCommas(
           snapshot_.cpu.process_count)},
      {strings_.threads,
       PerformanceFormatter::FormatIntegerWithCommas(
           snapshot_.cpu.thread_count)},
      {strings_.handles,
       PerformanceFormatter::FormatIntegerWithCommas(
           snapshot_.cpu.handle_count)},
      {strings_.uptime,
       PerformanceFormatter::FormatUptime(snapshot_.cpu.uptime_ms)},
  };
  DrawKpiGrid(hdc, kpi_rc, 2, small_font_, kpi_font_, palette.text_secondary,
              palette.text_primary, kpis);

  if (content_w >= 360 && kpi_rc.right + kStatsGapX < content_rc.right) {
    const RECT attr_rc{kpi_rc.right + kStatsGapX, stats_top, content_rc.right,
                       content_rc.bottom};
    const std::vector<AttrItem> attrs{
        {strings_.base_speed,
         PerformanceFormatter::FormatFrequencyGHz(
             snapshot_.cpu_static.base_speed_mhz)},
        {strings_.sockets,
         PerformanceFormatter::FormatIntegerWithCommas(
             snapshot_.cpu_static.socket_count)},
        {strings_.cores,
         PerformanceFormatter::FormatIntegerWithCommas(
             snapshot_.cpu_static.physical_core_count)},
        {strings_.logical_processors,
         PerformanceFormatter::FormatIntegerWithCommas(
             snapshot_.cpu_static.logical_processor_count)},
        {strings_.virtual_machine,
         snapshot_.cpu_static.is_virtual_machine ? strings_.yes : strings_.no},
        {strings_.l1_cache,
         PerformanceFormatter::FormatCacheSize(
             snapshot_.cpu_static.l1_cache_bytes, strings_.not_applicable)},
        {strings_.l2_cache,
         PerformanceFormatter::FormatCacheSize(
             snapshot_.cpu_static.l2_cache_bytes, strings_.not_applicable)},
        {strings_.l3_cache,
         PerformanceFormatter::FormatCacheSize(
             snapshot_.cpu_static.l3_cache_bytes, strings_.not_applicable)},
    };
    DrawAttributeList(hdc, attr_rc, small_font_, palette.text_secondary,
                      palette.text_primary, attrs);
  }
}

void PerformanceView::PaintMemoryDetail(HDC hdc, const RECT& content_rc) {
  const auto& palette = ThemeManager::GetPalette(theme_);
  const GraphStyle mem_style = PerformanceGraphRenderer::GetMemoryStyle(theme_);
  const auto& mem = snapshot_.memory;

  const std::wstring total_str =
      (mem.total_physical_bytes > 0)
          ? PerformanceFormatter::FormatMemoryGB(mem.total_physical_bytes)
          : L"";
  const std::wstring scale_str = total_str.empty() ? L"100%" : total_str;

  int body_top = content_rc.top;
  PaintDetailHeader(hdc, content_rc, strings_.memory_title, total_str,
                    strings_.graph_60s_mem, scale_str, &body_top);

  const int content_w = static_cast<int>(content_rc.right - content_rc.left);
  const int avail_h = static_cast<int>(content_rc.bottom - body_top);
  if (avail_h <= 20) return;

  const int stats_h =
      (avail_h >= 300) ? 156 : std::max(84, avail_h * 40 / 100);
  constexpr int kStatsTopGap = 14;
  constexpr int kCompGapTop = 10;
  constexpr int kCompLabelH = 18;
  constexpr int kCompBarH = 24;
  const int comp_total_h = kCompGapTop + kCompLabelH + kCompBarH;

  const int graph_bottom = std::max(
      body_top,
      static_cast<int>(content_rc.bottom) - stats_h - kStatsTopGap - comp_total_h);

  const RECT graph_rc{content_rc.left, body_top, content_rc.right, graph_bottom};
  if (graph_rc.bottom - graph_rc.top > 12 &&
      graph_rc.right - graph_rc.left > 12) {
    std::array<double, 60> buf{};
    const size_t n =
        snapshot_.memory_usage_history.CopyTo(buf.data(), buf.size());
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, graph_rc, buf.data(), n,
        snapshot_.memory_usage_history.Capacity(), 100.0, mem_style);
  }

  const int comp_label_top = graph_bottom + kCompGapTop;
  if (comp_label_top + kCompLabelH + kCompBarH <=
      content_rc.bottom - stats_h) {
    RECT comp_label_rc{content_rc.left, comp_label_top, content_rc.right,
                       comp_label_top + kCompLabelH};
    {
      ScopedSelectObject sel(hdc, small_font_);
      SetTextColor(hdc, palette.text_secondary);
      DrawTextW(hdc, strings_.memory_composition.c_str(), -1, &comp_label_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                    DT_NOPREFIX);
    }

    const RECT comp_bar_rc{content_rc.left, comp_label_rc.bottom + 2,
                           content_rc.right,
                           comp_label_rc.bottom + 2 + kCompBarH};
    PerformanceGraphRenderer::DrawMemoryCompositionBar(
        hdc, comp_bar_rc, mem.in_use_bytes, mem.cached_bytes,
        mem.total_physical_bytes, mem_style, palette.control_background);
  }

  const int stats_top =
      static_cast<int>(content_rc.bottom) - stats_h;
  if (stats_top >= content_rc.bottom) return;

  constexpr int kStatsGapX = 24;
  const int kpi_w =
      (content_w >= 360) ? std::max(120, (content_w - kStatsGapX) * 60 / 100)
                         : content_w;
  const RECT kpi_rc{content_rc.left, stats_top, content_rc.left + kpi_w,
                    content_rc.bottom};

  const std::wstring committed_str =
      PerformanceFormatter::FormatMemoryGB(mem.committed_bytes) + L" / " +
      PerformanceFormatter::FormatMemoryGB(mem.commit_limit_bytes);

  const std::vector<KpiItem> kpis{
      {strings_.in_use, PerformanceFormatter::FormatMemoryGB(mem.in_use_bytes)},
      {strings_.available,
       PerformanceFormatter::FormatMemoryGB(mem.available_bytes)},
      {strings_.committed, committed_str},
      {strings_.cached, PerformanceFormatter::FormatMemoryGB(mem.cached_bytes)},
      {strings_.paged_pool,
       PerformanceFormatter::FormatMemoryGB(mem.paged_pool_bytes)},
      {strings_.non_paged_pool,
       PerformanceFormatter::FormatMemoryGB(mem.non_paged_pool_bytes)},
  };
  DrawKpiGrid(hdc, kpi_rc, 2, small_font_, kpi_font_, palette.text_secondary,
              palette.text_primary, kpis);

  if (content_w >= 360 && kpi_rc.right + kStatsGapX < content_rc.right) {
    const RECT attr_rc{kpi_rc.right + kStatsGapX, stats_top, content_rc.right,
                       content_rc.bottom};
    const std::vector<AttrItem> attrs{
        {strings_.total_physical,
         PerformanceFormatter::FormatMemoryGB(mem.total_physical_bytes)},
    };
    DrawAttributeList(hdc, attr_rc, small_font_, palette.text_secondary,
                      palette.text_primary, attrs);
  }
}

void PerformanceView::PaintNetworkDetail(HDC hdc, const RECT& content_rc) {
  const auto& palette = ThemeManager::GetPalette(theme_);
  const NetworkAdapterSample* adapter = GetSelectedNetworkAdapter();
  if (!adapter) {
    ScopedSelectObject sel(hdc, normal_font_);
    SetTextColor(hdc, palette.text_secondary);
    RECT msg_rc = content_rc;
    DrawTextW(hdc, strings_.no_network_adapters.c_str(), -1, &msg_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }

  const GraphStyle net_style =
      PerformanceGraphRenderer::GetNetworkStyle(theme_);
  const NetworkAdapterHistory* history =
      FindNetworkHistory(adapter->luid_value);
  const double peak_bps = ComputeNetworkPeakBps(history);
  const double scale_bps =
      PerformanceFormatter::ComputeNetworkAutoScale(peak_bps);

  const std::wstring title = adapter->friendly_name.empty()
                                 ? strings_.network_fallback_title
                                 : adapter->friendly_name;
  const std::wstring scale_label =
      PerformanceFormatter::FormatBitrate(scale_bps);

  int body_top = content_rc.top;
  PaintDetailHeader(hdc, content_rc, title, adapter->description,
                    strings_.graph_60s_net, scale_label, &body_top);

  const int content_w = static_cast<int>(content_rc.right - content_rc.left);
  const int avail_h = static_cast<int>(content_rc.bottom - body_top);
  if (avail_h <= 20) return;

  const int stats_h =
      (avail_h >= 240) ? 116 : std::max(72, avail_h * 40 / 100);
  constexpr int kStatsTopGap = 14;
  const int graph_bottom =
      std::max(body_top, static_cast<int>(content_rc.bottom) - stats_h - kStatsTopGap);

  const RECT graph_rc{content_rc.left, body_top, content_rc.right, graph_bottom};
  if (graph_rc.bottom - graph_rc.top > 12 &&
      graph_rc.right - graph_rc.left > 12) {
    std::array<double, 60> recv_buf{};
    std::array<double, 60> send_buf{};
    size_t count = 0;
    size_t capacity = 60;
    if (history) {
      const size_t recv_n =
          history->recv_bps_history.CopyTo(recv_buf.data(), recv_buf.size());
      const size_t send_n =
          history->send_bps_history.CopyTo(send_buf.data(), send_buf.size());
      count = std::min(recv_n, send_n);
      capacity = history->recv_bps_history.Capacity();
    }
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, graph_rc, recv_buf.data(), count, capacity, scale_bps, net_style,
        send_buf.data());
  }

  const int stats_top = graph_bottom + kStatsTopGap;
  if (stats_top >= content_rc.bottom) return;

  constexpr int kStatsGapX = 24;
  const int kpi_w =
      (content_w >= 360) ? std::max(120, (content_w - kStatsGapX) * 44 / 100)
                         : content_w;
  const RECT kpi_rc{content_rc.left, stats_top, content_rc.left + kpi_w,
                    std::min<LONG>(content_rc.bottom, stats_top + 64)};

  const std::vector<KpiItem> kpis{
      {strings_.send, PerformanceFormatter::FormatBitrate(adapter->send_bps),
       true, net_style.secondary_line, true},
      {strings_.receive, PerformanceFormatter::FormatBitrate(adapter->recv_bps),
       true, net_style.primary_line, false},
  };
  DrawKpiGrid(hdc, kpi_rc, 2, small_font_, kpi_font_, palette.text_secondary,
              palette.text_primary, kpis);

  if (content_w >= 360 && kpi_rc.right + kStatsGapX < content_rc.right) {
    const RECT attr_rc{kpi_rc.right + kStatsGapX, stats_top, content_rc.right,
                       content_rc.bottom};
    const std::wstring adapter_name =
        adapter->description.empty() ? title : adapter->description;
    const std::wstring conn_type = adapter->connection_type.empty()
                                       ? strings_.not_applicable
                                       : adapter->connection_type;
    const std::wstring ipv4 = adapter->ipv4_address.empty()
                                  ? strings_.not_applicable
                                  : adapter->ipv4_address;
    const std::wstring ipv6 = adapter->ipv6_address.empty()
                                  ? strings_.not_applicable
                                  : adapter->ipv6_address;

    const std::vector<AttrItem> attrs{
        {strings_.adapter_name, adapter_name},
        {strings_.connection_type, conn_type},
        {strings_.ipv4_address, ipv4},
        {strings_.ipv6_address, ipv6},
        {strings_.link_speed,
         PerformanceFormatter::FormatLinkSpeed(adapter->link_speed_bps,
                                               strings_.not_applicable)},
    };
    DrawAttributeList(hdc, attr_rc, small_font_, palette.text_secondary,
                      palette.text_primary, attrs);
  }
}

}  // namespace lite_proc_manager
