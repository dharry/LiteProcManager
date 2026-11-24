// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PERFORMANCE_VIEW_H_
#define LITE_PROC_MANAGER_PERFORMANCE_VIEW_H_

#include <windows.h>

#include <cstdint>
#include <string>

#include "app_settings.h"
#include "performance_types.h"

namespace lite_proc_manager {

// Custom child window control that renders the Performance tab (sidebar of
// resource cards + detail pane with time-series graphs, KPIs, and hardware info).
class PerformanceView {
 public:
  static constexpr const wchar_t* kWindowClassName =
      L"LiteProcManagerPerformanceView";

  PerformanceView() = default;
  ~PerformanceView();

  PerformanceView(const PerformanceView&) = delete;
  PerformanceView& operator=(const PerformanceView&) = delete;

  bool Create(HWND parent, HINSTANCE instance, int control_id);
  void Destroy();
  HWND Hwnd() const { return hwnd_; }

  void UpdateSnapshot(const PerformanceSnapshot& snapshot);
  void SetTheme(AppTheme theme);
  void SetFonts(HFONT ui_font, const std::wstring& ui_font_name);
  void SetStrings(const PerformanceStrings& strings);
  void SetCpuGraphMode(CpuGraphMode mode);
  CpuGraphMode GetCpuGraphMode() const { return cpu_graph_mode_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void EnsureFonts();
  void DeleteFonts();
  void InvalidateView();

  int GetCardCount() const;
  int GetSelectedCardIndex() const;
  int GetMaxSidebarScrollY(int client_height) const;
  void ClampSidebarScroll(int client_height);
  RECT GetCardRect(int card_index) const;
  int HitTestSidebarCard(POINT client_pt) const;
  void SelectCard(int card_index);

  const NetworkAdapterSample* GetSelectedNetworkAdapter() const;
  const NetworkAdapterHistory* FindNetworkHistory(uint64_t luid_value) const;
  double ComputeNetworkPeakBps(const NetworkAdapterHistory* history) const;

  void ShowCpuContextMenu(POINT screen_pt);

  void Paint(HDC hdc, const RECT& client_rc);
  void PaintSidebar(HDC hdc, const RECT& sidebar_rc);
  void PaintSidebarCard(HDC hdc, const RECT& card_rc, int card_index,
                        bool is_selected, bool is_hovered);
  void PaintDetailPane(HDC hdc, const RECT& detail_rc);
  void PaintCpuDetail(HDC hdc, const RECT& content_rc);
  void PaintMemoryDetail(HDC hdc, const RECT& content_rc);
  void PaintNetworkDetail(HDC hdc, const RECT& content_rc);

  void PaintDetailHeader(HDC hdc, const RECT& content_rc,
                         const std::wstring& title,
                         const std::wstring& subtitle,
                         const std::wstring& graph_caption,
                         const std::wstring& scale_label,
                         int* out_body_top) const;

  HWND hwnd_{nullptr};
  AppTheme theme_{AppTheme::kLight};
  PerformanceSnapshot snapshot_;
  PerformanceStrings strings_;
  CpuGraphMode cpu_graph_mode_{CpuGraphMode::kLogicalProcessors};

  PerformanceResourceType selected_type_{PerformanceResourceType::kCpu};
  uint64_t selected_network_luid_{0};

  int hovered_card_index_{-1};
  bool tracking_mouse_leave_{false};
  int sidebar_scroll_y_{0};

  std::wstring ui_font_name_{L"Yu Gothic UI"};
  int base_font_height_{0};
  HFONT title_font_{nullptr};
  HFONT kpi_font_{nullptr};
  HFONT normal_font_{nullptr};
  HFONT small_font_{nullptr};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PERFORMANCE_VIEW_H_
