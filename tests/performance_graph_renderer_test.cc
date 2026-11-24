// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <windows.h>

#include <cstdint>
#include <vector>

#include "../include/performance_graph_renderer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace LiteProcManagerTests {

namespace {

class OffscreenCanvas {
 public:
  OffscreenCanvas(int width, int height) {
    HDC screen_dc = GetDC(nullptr);
    mem_dc_ = CreateCompatibleDC(nullptr);
    if (mem_dc_) {
      HDC format_dc = screen_dc ? screen_dc : mem_dc_;
      bitmap_ = CreateCompatibleBitmap(format_dc, width, height);
      if (bitmap_) {
        old_bitmap_ = SelectObject(mem_dc_, bitmap_);
      }
    }
    if (screen_dc) {
      ReleaseDC(nullptr, screen_dc);
    }
  }

  ~OffscreenCanvas() {
    if (mem_dc_) {
      if (old_bitmap_) {
        SelectObject(mem_dc_, old_bitmap_);
      }
      if (bitmap_) {
        DeleteObject(bitmap_);
      }
      DeleteDC(mem_dc_);
    } else if (bitmap_) {
      DeleteObject(bitmap_);
    }
  }

  OffscreenCanvas(const OffscreenCanvas&) = delete;
  OffscreenCanvas& operator=(const OffscreenCanvas&) = delete;

  HDC GetDCHandle() const noexcept { return mem_dc_; }
  bool IsValid() const noexcept {
    return mem_dc_ != nullptr && bitmap_ != nullptr;
  }

 private:
  HDC mem_dc_{nullptr};
  HBITMAP bitmap_{nullptr};
  HGDIOBJ old_bitmap_{nullptr};
};

}  // namespace

TEST_CLASS(PerformanceGraphRendererTests) {
 public:
  TEST_METHOD(Styles_ShouldMatchTaskManagerLightAndDarkPalettes) {
    const GraphStyle cpu_light =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kLight);
    Assert::AreEqual(RGB(255, 255, 255), cpu_light.background);
    Assert::AreEqual(RGB(217, 234, 244), cpu_light.grid_line);
    Assert::AreEqual(RGB(17, 125, 187), cpu_light.border);
    Assert::AreEqual(RGB(17, 125, 187), cpu_light.primary_line);
    Assert::AreEqual(RGB(232, 243, 250), cpu_light.primary_fill);

    const GraphStyle cpu_dark =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kDark);
    Assert::AreEqual(RGB(24, 26, 30), cpu_dark.background);
    Assert::AreEqual(RGB(42, 54, 66), cpu_dark.grid_line);
    Assert::AreEqual(RGB(56, 140, 190), cpu_dark.border);
    Assert::AreEqual(RGB(64, 178, 242), cpu_dark.primary_line);
    Assert::AreEqual(RGB(30, 56, 74), cpu_dark.primary_fill);

    const GraphStyle mem_light =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kLight);
    Assert::AreEqual(RGB(255, 255, 255), mem_light.background);
    Assert::AreEqual(RGB(233, 217, 242), mem_light.grid_line);
    Assert::AreEqual(RGB(139, 18, 174), mem_light.border);
    Assert::AreEqual(RGB(139, 18, 174), mem_light.primary_line);
    Assert::AreEqual(RGB(244, 235, 249), mem_light.primary_fill);

    const GraphStyle mem_dark =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kDark);
    Assert::AreEqual(RGB(24, 26, 30), mem_dark.background);
    Assert::AreEqual(RGB(56, 42, 64), mem_dark.grid_line);
    Assert::AreEqual(RGB(152, 72, 180), mem_dark.border);
    Assert::AreEqual(RGB(192, 108, 214), mem_dark.primary_line);
    Assert::AreEqual(RGB(54, 32, 64), mem_dark.primary_fill);

    const GraphStyle net_light =
        PerformanceGraphRenderer::GetNetworkStyle(AppTheme::kLight);
    Assert::AreEqual(RGB(255, 255, 255), net_light.background);
    Assert::AreEqual(RGB(244, 228, 212), net_light.grid_line);
    Assert::AreEqual(RGB(167, 79, 1), net_light.border);
    Assert::AreEqual(RGB(167, 79, 1), net_light.primary_line);
    Assert::AreEqual(RGB(167, 79, 1), net_light.secondary_line);
    Assert::AreEqual(RGB(249, 240, 231), net_light.primary_fill);

    const GraphStyle net_dark =
        PerformanceGraphRenderer::GetNetworkStyle(AppTheme::kDark);
    Assert::AreEqual(RGB(24, 26, 30), net_dark.background);
    Assert::AreEqual(RGB(62, 48, 36), net_dark.grid_line);
    Assert::AreEqual(RGB(184, 108, 44), net_dark.border);
    Assert::AreEqual(RGB(235, 145, 65), net_dark.primary_line);
    Assert::AreEqual(RGB(255, 196, 122), net_dark.secondary_line);
    Assert::AreEqual(RGB(58, 42, 28), net_dark.primary_fill);
  }

  TEST_METHOD(DrawTimeSeriesGraph_ShouldHandleEmptySinglePartialFullAndSecondary) {
    OffscreenCanvas canvas(320, 180);
    Assert::IsTrue(canvas.IsValid());
    HDC hdc = canvas.GetDCHandle();
    const RECT rect{10, 10, 310, 170};
    const GraphStyle cpu_light =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kLight);

    // 0 samples: background, grid, and border are drawn.
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, rect, nullptr, 0, 60, 100.0, cpu_light);
    Assert::AreEqual(cpu_light.border, GetPixel(hdc, 10, 10));
    Assert::AreEqual(cpu_light.border, GetPixel(hdc, 309, 169));
    Assert::AreEqual(cpu_light.background, GetPixel(hdc, 12, 12));

    // 1 sample: safely renders grid and border without curve polygon.
    const double single_sample[1] = {50.0};
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, rect, single_sample, 1, 60, 100.0, cpu_light);
    Assert::AreEqual(cpu_light.border, GetPixel(hdc, 10, 10));

    // Partial (15 samples): right-aligned on right quarter, left side untouched.
    const std::vector<double> partial(15, 50.0);
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, rect, partial.data(), partial.size(), 60, 100.0, cpu_light);
    Assert::AreEqual(cpu_light.background, GetPixel(hdc, 20, 140));
    Assert::AreEqual(cpu_light.primary_fill, GetPixel(hdc, 290, 140));

    // Full (60 samples): entire width filled under constant 50% line.
    const std::vector<double> full_primary(60, 50.0);
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, rect, full_primary.data(), full_primary.size(), 60, 100.0,
        cpu_light);
    Assert::AreEqual(cpu_light.primary_fill, GetPixel(hdc, 20, 140));
    Assert::AreEqual(cpu_light.primary_fill, GetPixel(hdc, 160, 140));
    Assert::AreEqual(cpu_light.background, GetPixel(hdc, 20, 20));

    // Secondary curve in Network Dark style.
    const GraphStyle net_dark =
        PerformanceGraphRenderer::GetNetworkStyle(AppTheme::kDark);
    std::vector<double> full_secondary(60, 25.0);
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, rect, full_primary.data(), full_primary.size(), 60, 100.0,
        net_dark, full_secondary.data());
    Assert::AreEqual(net_dark.border, GetPixel(hdc, 10, 10));
    Assert::AreEqual(net_dark.background, GetPixel(hdc, 20, 20));
  }

  TEST_METHOD(DrawTimeSeriesGraph_ShouldIgnoreInvalidDcAndDegenerateRects) {
    const std::vector<double> samples(10, 50.0);
    const GraphStyle style =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kLight);

    // Null DC must be a safe no-op.
    const RECT valid_rect{0, 0, 100, 100};
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        nullptr, valid_rect, samples.data(), samples.size(), 60, 100.0, style);
    PerformanceGraphRenderer::DrawMiniSparkline(
        nullptr, valid_rect, samples.data(), samples.size(), 60, 100.0, style);
    PerformanceGraphRenderer::DrawMemoryCompositionBar(
        nullptr, valid_rect, 50, 25, 100, style, RGB(240, 240, 240));

    OffscreenCanvas canvas(320, 180);
    Assert::IsTrue(canvas.IsValid());
    HDC hdc = canvas.GetDCHandle();
    const RECT clear_rect{0, 0, 320, 180};
    HBRUSH white_brush = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    FillRect(hdc, &clear_rect, white_brush);

    const RECT zero_rect{0, 0, 0, 0};
    const RECT tiny_rect{10, 10, 12, 12};
    const RECT negative_rect{100, 100, 50, 50};
    const GraphStyle dark_style =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kDark);

    for (const RECT& bad_rect : {zero_rect, tiny_rect, negative_rect}) {
      PerformanceGraphRenderer::DrawTimeSeriesGraph(
          hdc, bad_rect, samples.data(), samples.size(), 60, 100.0, dark_style);
      PerformanceGraphRenderer::DrawMiniSparkline(
          hdc, bad_rect, samples.data(), samples.size(), 60, 100.0, dark_style);
      PerformanceGraphRenderer::DrawMemoryCompositionBar(
          hdc, bad_rect, 50, 25, 100, dark_style, RGB(32, 32, 32));
    }

    Assert::AreEqual(RGB(255, 255, 255), GetPixel(hdc, 11, 11));
    Assert::AreEqual(RGB(255, 255, 255), GetPixel(hdc, 75, 75));
  }

  TEST_METHOD(DrawMiniSparkline_ShouldRenderLightAndDarkStyles) {
    OffscreenCanvas canvas(320, 180);
    Assert::IsTrue(canvas.IsValid());
    HDC hdc = canvas.GetDCHandle();
    const RECT rect{8, 8, 96, 48};
    const std::vector<double> values(60, 50.0);

    const GraphStyle light =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kLight);
    PerformanceGraphRenderer::DrawMiniSparkline(
        hdc, rect, values.data(), values.size(), 60, 100.0, light);
    Assert::AreEqual(light.border, GetPixel(hdc, 8, 8));
    Assert::AreEqual(light.primary_fill, GetPixel(hdc, 50, 40));
    Assert::AreEqual(light.background, GetPixel(hdc, 50, 12));

    const GraphStyle dark =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kDark);
    PerformanceGraphRenderer::DrawMiniSparkline(
        hdc, rect, values.data(), values.size(), 60, 100.0, dark);
    Assert::AreEqual(dark.border, GetPixel(hdc, 8, 8));
    Assert::AreEqual(dark.primary_fill, GetPixel(hdc, 50, 40));
    Assert::AreEqual(dark.background, GetPixel(hdc, 50, 12));
  }

  TEST_METHOD(DrawMemoryCompositionBar_ShouldRenderInUseCachedAndBorder) {
    OffscreenCanvas canvas(320, 180);
    Assert::IsTrue(canvas.IsValid());
    HDC hdc = canvas.GetDCHandle();
    const RECT rect{10, 10, 210, 34};

    const GraphStyle light =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kLight);
    const COLORREF light_track = RGB(240, 240, 240);
    PerformanceGraphRenderer::DrawMemoryCompositionBar(
        hdc, rect, 50, 25, 100, light, light_track);
    Assert::AreEqual(light.border, GetPixel(hdc, 10, 10));
    Assert::AreEqual(light.primary_line, GetPixel(hdc, 40, 20));
    const COLORREF light_cached_pixel = GetPixel(hdc, 130, 20);
    Assert::AreNotEqual(light_track, light_cached_pixel);
    Assert::AreNotEqual(light.primary_line, light_cached_pixel);
    Assert::AreEqual(light_track, GetPixel(hdc, 180, 20));

    const GraphStyle dark =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kDark);
    const COLORREF dark_track = RGB(38, 40, 46);
    PerformanceGraphRenderer::DrawMemoryCompositionBar(
        hdc, rect, 60, 20, 100, dark, dark_track);
    Assert::AreEqual(dark.border, GetPixel(hdc, 10, 10));
    Assert::AreEqual(dark.primary_line, GetPixel(hdc, 40, 20));
    const COLORREF dark_cached_pixel = GetPixel(hdc, 145, 20);
    Assert::AreNotEqual(dark_track, dark_cached_pixel);
    Assert::AreNotEqual(dark.primary_line, dark_cached_pixel);
    Assert::AreEqual(dark_track, GetPixel(hdc, 195, 20));

    // Edge cases: total == 0 and overflow values.
    PerformanceGraphRenderer::DrawMemoryCompositionBar(
        hdc, rect, 100, 100, 0, dark, dark_track);
    Assert::AreEqual(dark_track, GetPixel(hdc, 50, 20));
  }

  TEST_METHOD(RenderingLoop_ShouldNotLeakGdiObjects) {
    OffscreenCanvas canvas(320, 180);
    Assert::IsTrue(canvas.IsValid());
    HDC hdc = canvas.GetDCHandle();

    const RECT graph_rect{0, 0, 320, 180};
    const RECT spark_rect{8, 8, 88, 44};
    const RECT bar_rect{10, 150, 310, 170};

    std::vector<double> primary(60, 0.0);
    std::vector<double> secondary(60, 0.0);
    for (size_t i = 0; i < 60; ++i) {
      primary[i] = static_cast<double>((i * 7) % 100);
      secondary[i] = static_cast<double>((i * 3) % 80);
    }

    const GraphStyle cpu_light =
        PerformanceGraphRenderer::GetCpuStyle(AppTheme::kLight);
    const GraphStyle net_dark =
        PerformanceGraphRenderer::GetNetworkStyle(AppTheme::kDark);
    const GraphStyle mem_light =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kLight);
    const GraphStyle mem_dark =
        PerformanceGraphRenderer::GetMemoryStyle(AppTheme::kDark);

    // Warm-up call before measuring baseline GDI object count.
    PerformanceGraphRenderer::DrawTimeSeriesGraph(
        hdc, graph_rect, primary.data(), primary.size(), 60, 100.0, net_dark,
        secondary.data());

    const DWORD gdi_objects_before =
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    Assert::AreNotEqual(static_cast<DWORD>(0), gdi_objects_before);

    for (int iter = 0; iter < 100; ++iter) {
      PerformanceGraphRenderer::DrawTimeSeriesGraph(
          hdc, graph_rect, primary.data(), primary.size(), 60, 100.0,
          cpu_light);
      PerformanceGraphRenderer::DrawTimeSeriesGraph(
          hdc, graph_rect, primary.data(), primary.size(), 60, 100.0, net_dark,
          secondary.data());
      PerformanceGraphRenderer::DrawMiniSparkline(
          hdc, spark_rect, primary.data(), 15, 60, 100.0, mem_light);
      PerformanceGraphRenderer::DrawMiniSparkline(
          hdc, spark_rect, primary.data(), primary.size(), 60, 100.0, mem_dark);
      PerformanceGraphRenderer::DrawMemoryCompositionBar(
          hdc, bar_rect, 8192, 4096, 16384, mem_light, RGB(240, 240, 240));
      PerformanceGraphRenderer::DrawMemoryCompositionBar(
          hdc, bar_rect, 8192, 4096, 16384, mem_dark, RGB(38, 40, 46));
    }

    const DWORD gdi_objects_after =
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    Assert::AreEqual(gdi_objects_before, gdi_objects_after);
  }
};

}  // namespace LiteProcManagerTests
