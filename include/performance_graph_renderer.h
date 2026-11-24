// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PERFORMANCE_GRAPH_RENDERER_H_
#define LITE_PROC_MANAGER_PERFORMANCE_GRAPH_RENDERER_H_

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "app_settings.h"

namespace lite_proc_manager {

// Color configuration used to render a performance graph or composition bar.
struct GraphStyle {
  COLORREF background{RGB(255, 255, 255)};
  COLORREF grid_line{RGB(217, 234, 244)};
  COLORREF border{RGB(17, 125, 187)};
  COLORREF primary_line{RGB(17, 125, 187)};
  COLORREF primary_fill{RGB(241, 246, 250)};
  COLORREF secondary_line{RGB(17, 125, 187)};
};

// Stateless GDI renderer for Task Manager-style time-series graphs, compact
// sidebar sparklines, and memory composition bars.
class PerformanceGraphRenderer {
 public:
  static GraphStyle GetCpuStyle(AppTheme theme);
  static GraphStyle GetMemoryStyle(AppTheme theme);
  static GraphStyle GetNetworkStyle(AppTheme theme);

  static void DrawTimeSeriesGraph(HDC hdc, const RECT& rect,
                                  const double* primary_values, size_t count,
                                  size_t capacity, double max_value,
                                  const GraphStyle& style,
                                  const double* secondary_values = nullptr);

  static void DrawMiniSparkline(HDC hdc, const RECT& rect,
                                const double* values, size_t count,
                                size_t capacity, double max_value,
                                const GraphStyle& style);

  static void DrawMemoryCompositionBar(HDC hdc, const RECT& rect,
                                       uint64_t in_use, uint64_t cached,
                                       uint64_t total, const GraphStyle& style,
                                       COLORREF track_bg);
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PERFORMANCE_GRAPH_RENDERER_H_
