// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "performance_graph_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lite_proc_manager {

namespace {

constexpr int kGridVerticalLines = 10;
constexpr int kGridHorizontalLines = 4;

class ScopedPen {
 public:
  explicit ScopedPen(HPEN pen) noexcept : pen_(pen) {}
  ~ScopedPen() {
    if (pen_) {
      DeleteObject(pen_);
    }
  }

  ScopedPen(const ScopedPen&) = delete;
  ScopedPen& operator=(const ScopedPen&) = delete;
  ScopedPen(ScopedPen&&) = delete;
  ScopedPen& operator=(ScopedPen&&) = delete;

  HPEN Get() const noexcept { return pen_; }

 private:
  HPEN pen_{nullptr};
};

class ScopedBrush {
 public:
  explicit ScopedBrush(HBRUSH brush) noexcept : brush_(brush) {}
  ~ScopedBrush() {
    if (brush_) {
      DeleteObject(brush_);
    }
  }

  ScopedBrush(const ScopedBrush&) = delete;
  ScopedBrush& operator=(const ScopedBrush&) = delete;
  ScopedBrush(ScopedBrush&&) = delete;
  ScopedBrush& operator=(ScopedBrush&&) = delete;

  HBRUSH Get() const noexcept { return brush_; }

 private:
  HBRUSH brush_{nullptr};
};

class ScopedSelectObject {
 public:
  ScopedSelectObject(HDC hdc, HGDIOBJ object) noexcept : hdc_(hdc) {
    if (hdc_ && object) {
      old_object_ = SelectObject(hdc_, object);
    }
  }
  ~ScopedSelectObject() {
    if (hdc_ && old_object_) {
      SelectObject(hdc_, old_object_);
    }
  }

  ScopedSelectObject(const ScopedSelectObject&) = delete;
  ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;
  ScopedSelectObject(ScopedSelectObject&&) = delete;
  ScopedSelectObject& operator=(ScopedSelectObject&&) = delete;

 private:
  HDC hdc_{nullptr};
  HGDIOBJ old_object_{nullptr};
};

class ScopedBkMode {
 public:
  ScopedBkMode(HDC hdc, int mode) noexcept : hdc_(hdc) {
    if (hdc_) {
      old_mode_ = SetBkMode(hdc_, mode);
    }
  }
  ~ScopedBkMode() {
    if (hdc_ && old_mode_ != 0) {
      SetBkMode(hdc_, old_mode_);
    }
  }

  ScopedBkMode(const ScopedBkMode&) = delete;
  ScopedBkMode& operator=(const ScopedBkMode&) = delete;
  ScopedBkMode(ScopedBkMode&&) = delete;
  ScopedBkMode& operator=(ScopedBkMode&&) = delete;

 private:
  HDC hdc_{nullptr};
  int old_mode_{0};
};

class ScopedSaveDC {
 public:
  explicit ScopedSaveDC(HDC hdc) noexcept : hdc_(hdc) {
    if (hdc_) {
      saved_state_ = SaveDC(hdc_);
    }
  }
  ~ScopedSaveDC() {
    if (hdc_ && saved_state_ != 0) {
      RestoreDC(hdc_, saved_state_);
    }
  }

  ScopedSaveDC(const ScopedSaveDC&) = delete;
  ScopedSaveDC& operator=(const ScopedSaveDC&) = delete;
  ScopedSaveDC(ScopedSaveDC&&) = delete;
  ScopedSaveDC& operator=(ScopedSaveDC&&) = delete;

 private:
  HDC hdc_{nullptr};
  int saved_state_{0};
};

bool IsValidGraphRect(const RECT& rect) noexcept {
  return rect.right > rect.left + 2 && rect.bottom > rect.top + 2;
}

RECT GetInnerRect(const RECT& rect) noexcept {
  return RECT{rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
}

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
  ScopedBrush brush(CreateSolidBrush(color));
  if (brush.Get()) {
    FillRect(hdc, &rect, brush.Get());
  }
}

void DrawBorder(HDC hdc, const RECT& rect, COLORREF color) {
  ScopedBrush brush(CreateSolidBrush(color));
  if (brush.Get()) {
    FrameRect(hdc, &rect, brush.Get());
  }
}

void DrawGridLines(HDC hdc, const RECT& inner_rect, COLORREF grid_color) {
  const LONG width = inner_rect.right - inner_rect.left;
  const LONG height = inner_rect.bottom - inner_rect.top;
  if (width <= 0 || height <= 0) {
    return;
  }

  ScopedPen grid_pen(CreatePen(PS_SOLID, 1, grid_color));
  if (!grid_pen.Get()) {
    return;
  }
  ScopedSelectObject select_pen(hdc, grid_pen.Get());

  for (int i = 1; i <= kGridVerticalLines; ++i) {
    const LONG x = inner_rect.left +
                   static_cast<LONG>((static_cast<int64_t>(width) * i) /
                                     (kGridVerticalLines + 1));
    MoveToEx(hdc, x, inner_rect.top, nullptr);
    LineTo(hdc, x, inner_rect.bottom);
  }

  for (int j = 1; j <= kGridHorizontalLines; ++j) {
    const LONG y = inner_rect.top +
                   static_cast<LONG>((static_cast<int64_t>(height) * j) /
                                     (kGridHorizontalLines + 1));
    MoveToEx(hdc, inner_rect.left, y, nullptr);
    LineTo(hdc, inner_rect.right, y);
  }
}

std::vector<POINT> BuildSeriesPoints(const double* values, size_t offset,
                                     size_t draw_count, size_t capacity,
                                     double max_value,
                                     const RECT& inner_rect) {
  std::vector<POINT> points;
  points.reserve(draw_count);

  const LONG span_x = (inner_rect.right - 1) - inner_rect.left;
  const LONG span_y = (inner_rect.bottom - 1) - inner_rect.top;
  const double denom_x = static_cast<double>(capacity - 1);
  const double effective_max =
      (std::isfinite(max_value) && max_value > 0.0) ? max_value : 0.0;

  for (size_t i = 0; i < draw_count; ++i) {
    const size_t slot = (capacity - draw_count) + i;
    const LONG x =
        inner_rect.left +
        static_cast<LONG>(std::llround(
            (static_cast<double>(slot) * static_cast<double>(span_x)) /
            denom_x));

    const double sample = values[offset + i];
    double ratio = 0.0;
    if (effective_max > 0.0 && std::isfinite(sample) && sample > 0.0) {
      ratio = std::clamp(sample / effective_max, 0.0, 1.0);
    }

    const LONG y =
        (inner_rect.bottom - 1) -
        static_cast<LONG>(std::llround(ratio * static_cast<double>(span_y)));
    points.push_back(POINT{x, y});
  }

  return points;
}

void DrawFilledSeries(HDC hdc, const std::vector<POINT>& points,
                      const RECT& inner_rect, const GraphStyle& style,
                      int line_width) {
  if (points.size() < 2) {
    return;
  }

  std::vector<POINT> polygon_points;
  polygon_points.reserve(points.size() + 2);
  polygon_points.insert(polygon_points.end(), points.begin(), points.end());
  polygon_points.push_back(POINT{points.back().x, inner_rect.bottom});
  polygon_points.push_back(POINT{points.front().x, inner_rect.bottom});

  {
    ScopedBrush fill_brush(CreateSolidBrush(style.primary_fill));
    if (fill_brush.Get()) {
      ScopedSelectObject select_brush(hdc, fill_brush.Get());
      ScopedSelectObject select_null_pen(hdc, GetStockObject(NULL_PEN));
      Polygon(hdc, polygon_points.data(),
              static_cast<int>(polygon_points.size()));
    }
  }

  ScopedPen line_pen(CreatePen(PS_SOLID, line_width, style.primary_line));
  if (line_pen.Get()) {
    ScopedSelectObject select_pen(hdc, line_pen.Get());
    Polyline(hdc, points.data(), static_cast<int>(points.size()));
  }
}

COLORREF BlendColors(COLORREF fg, COLORREF bg, double fg_weight) noexcept {
  const double clamped_weight = std::clamp(fg_weight, 0.0, 1.0);
  const double bg_weight = 1.0 - clamped_weight;
  const BYTE r = static_cast<BYTE>(std::llround(
      GetRValue(fg) * clamped_weight + GetRValue(bg) * bg_weight));
  const BYTE g = static_cast<BYTE>(std::llround(
      GetGValue(fg) * clamped_weight + GetGValue(bg) * bg_weight));
  const BYTE b = static_cast<BYTE>(std::llround(
      GetBValue(fg) * clamped_weight + GetBValue(bg) * bg_weight));
  return RGB(r, g, b);
}

}  // namespace

GraphStyle PerformanceGraphRenderer::GetCpuStyle(AppTheme theme) {
  if (theme == AppTheme::kDark) {
    return GraphStyle{
        RGB(24, 26, 30),    // background
        RGB(42, 54, 66),    // grid_line
        RGB(56, 140, 190),  // border
        RGB(64, 178, 242),  // primary_line
        RGB(30, 56, 74),    // primary_fill
        RGB(64, 178, 242),  // secondary_line
    };
  }
  return GraphStyle{
      RGB(255, 255, 255),  // background
      RGB(217, 234, 244),  // grid_line
      RGB(17, 125, 187),   // border
      RGB(17, 125, 187),   // primary_line
      RGB(232, 243, 250),  // primary_fill
      RGB(17, 125, 187),   // secondary_line
  };
}

GraphStyle PerformanceGraphRenderer::GetMemoryStyle(AppTheme theme) {
  if (theme == AppTheme::kDark) {
    return GraphStyle{
        RGB(24, 26, 30),     // background
        RGB(56, 42, 64),     // grid_line
        RGB(152, 72, 180),   // border
        RGB(192, 108, 214),  // primary_line
        RGB(54, 32, 64),     // primary_fill
        RGB(192, 108, 214),  // secondary_line
    };
  }
  return GraphStyle{
      RGB(255, 255, 255),  // background
      RGB(233, 217, 242),  // grid_line
      RGB(139, 18, 174),   // border
      RGB(139, 18, 174),   // primary_line
      RGB(244, 235, 249),  // primary_fill
      RGB(139, 18, 174),   // secondary_line
  };
}

GraphStyle PerformanceGraphRenderer::GetNetworkStyle(AppTheme theme) {
  if (theme == AppTheme::kDark) {
    return GraphStyle{
        RGB(24, 26, 30),     // background
        RGB(62, 48, 36),     // grid_line
        RGB(184, 108, 44),   // border
        RGB(235, 145, 65),   // primary_line
        RGB(58, 42, 28),     // primary_fill
        RGB(255, 196, 122),  // secondary_line
    };
  }
  return GraphStyle{
      RGB(255, 255, 255),  // background
      RGB(244, 228, 212),  // grid_line
      RGB(167, 79, 1),     // border
      RGB(167, 79, 1),     // primary_line
      RGB(249, 240, 231),  // primary_fill
      RGB(167, 79, 1),     // secondary_line
  };
}

void PerformanceGraphRenderer::DrawTimeSeriesGraph(
    HDC hdc, const RECT& rect, const double* primary_values, size_t count,
    size_t capacity, double max_value, const GraphStyle& style,
    const double* secondary_values) {
  if (!hdc || !IsValidGraphRect(rect)) {
    return;
  }

  FillSolidRect(hdc, rect, style.background);

  const RECT inner_rect = GetInnerRect(rect);
  DrawGridLines(hdc, inner_rect, style.grid_line);

  const size_t draw_count = (std::min)(count, capacity);
  if (capacity >= 2 && draw_count >= 2 && primary_values != nullptr) {
    const size_t offset = count - draw_count;

    ScopedSaveDC save_dc(hdc);
    IntersectClipRect(hdc, inner_rect.left, inner_rect.top, inner_rect.right,
                      inner_rect.bottom);

    const std::vector<POINT> primary_points = BuildSeriesPoints(
        primary_values, offset, draw_count, capacity, max_value, inner_rect);
    DrawFilledSeries(hdc, primary_points, inner_rect, style, 2);

    if (secondary_values != nullptr) {
      const std::vector<POINT> secondary_points =
          BuildSeriesPoints(secondary_values, offset, draw_count, capacity,
                            max_value, inner_rect);
      ScopedBkMode bk_mode(hdc, TRANSPARENT);
      ScopedPen secondary_pen(CreatePen(PS_DASH, 1, style.secondary_line));
      if (secondary_pen.Get()) {
        ScopedSelectObject select_pen(hdc, secondary_pen.Get());
        Polyline(hdc, secondary_points.data(),
                 static_cast<int>(secondary_points.size()));
      }
    }
  }

  DrawBorder(hdc, rect, style.border);
}

void PerformanceGraphRenderer::DrawMiniSparkline(HDC hdc, const RECT& rect,
                                                 const double* values,
                                                 size_t count, size_t capacity,
                                                 double max_value,
                                                 const GraphStyle& style) {
  if (!hdc || !IsValidGraphRect(rect)) {
    return;
  }

  FillSolidRect(hdc, rect, style.background);

  const RECT inner_rect = GetInnerRect(rect);
  const size_t draw_count = (std::min)(count, capacity);
  if (capacity >= 2 && draw_count >= 2 && values != nullptr) {
    const size_t offset = count - draw_count;

    ScopedSaveDC save_dc(hdc);
    IntersectClipRect(hdc, inner_rect.left, inner_rect.top, inner_rect.right,
                      inner_rect.bottom);

    const std::vector<POINT> points = BuildSeriesPoints(
        values, offset, draw_count, capacity, max_value, inner_rect);
    DrawFilledSeries(hdc, points, inner_rect, style, 1);
  }

  DrawBorder(hdc, rect, style.border);
}

void PerformanceGraphRenderer::DrawMemoryCompositionBar(
    HDC hdc, const RECT& rect, uint64_t in_use, uint64_t cached, uint64_t total,
    const GraphStyle& style, COLORREF track_bg) {
  if (!hdc || !IsValidGraphRect(rect)) {
    return;
  }

  FillSolidRect(hdc, rect, track_bg);

  const RECT inner_rect = GetInnerRect(rect);
  const int inner_width = static_cast<int>(inner_rect.right - inner_rect.left);
  if (total > 0 && inner_width > 0) {
    const uint64_t clamped_in_use = (std::min)(in_use, total);
    const uint64_t remaining = total - clamped_in_use;
    const uint64_t clamped_cached = (std::min)(cached, remaining);
    const double total_d = static_cast<double>(total);

    const int in_use_px = std::clamp(
        static_cast<int>(std::llround(
            (static_cast<double>(clamped_in_use) / total_d) *
            static_cast<double>(inner_width))),
        0, inner_width);
    const int cached_px = std::clamp(
        static_cast<int>(std::llround(
            (static_cast<double>(clamped_cached) / total_d) *
            static_cast<double>(inner_width))),
        0, inner_width - in_use_px);

    if (in_use_px > 0) {
      const RECT in_use_rect{inner_rect.left, inner_rect.top,
                             inner_rect.left + in_use_px, inner_rect.bottom};
      FillSolidRect(hdc, in_use_rect, style.primary_line);
    }

    if (cached_px > 0) {
      const RECT cached_rect{inner_rect.left + in_use_px, inner_rect.top,
                             inner_rect.left + in_use_px + cached_px,
                             inner_rect.bottom};
      const COLORREF cached_color =
          BlendColors(style.primary_line, track_bg, 0.40);
      FillSolidRect(hdc, cached_rect, cached_color);
    }
  }

  DrawBorder(hdc, rect, style.border);
}

}  // namespace lite_proc_manager
