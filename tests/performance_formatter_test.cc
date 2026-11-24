// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../include/performance_formatter.h"
#include "../include/performance_types.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace LiteProcManagerTests {

TEST_CLASS(PerformanceFormatterTests) {
 public:
  TEST_METHOD(RingBuffer_ShouldHandleEmptyPushWrapCopyAndClear) {
    RingBuffer<double, 60> buffer;
    Assert::IsTrue(buffer.Empty());
    Assert::AreEqual(static_cast<size_t>(0), buffer.Size());
    Assert::AreEqual(static_cast<size_t>(60), buffer.Capacity());
    Assert::AreEqual(-1.0, buffer.Latest(-1.0));
    Assert::IsTrue(buffer.ToVector().empty());

    std::array<double, 10> copy_buf{};
    Assert::AreEqual(static_cast<size_t>(0),
                     buffer.CopyTo(copy_buf.data(), copy_buf.size()));
    Assert::AreEqual(static_cast<size_t>(0), buffer.CopyTo(nullptr, 5));
    Assert::AreEqual(static_cast<size_t>(0),
                     buffer.CopyTo(copy_buf.data(), 0));

    for (int i = 0; i < 5; ++i) {
      buffer.Push(static_cast<double>(i));
    }
    Assert::IsFalse(buffer.Empty());
    Assert::AreEqual(static_cast<size_t>(5), buffer.Size());
    Assert::AreEqual(0.0, buffer.At(0));
    Assert::AreEqual(4.0, buffer.At(4));
    Assert::AreEqual(4.0, buffer.Latest());

    const size_t copied_partial = buffer.CopyTo(copy_buf.data(), 3);
    Assert::AreEqual(static_cast<size_t>(3), copied_partial);
    Assert::AreEqual(2.0, copy_buf[0]);
    Assert::AreEqual(3.0, copy_buf[1]);
    Assert::AreEqual(4.0, copy_buf[2]);

    const size_t copied_all = buffer.CopyTo(copy_buf.data(), copy_buf.size());
    Assert::AreEqual(static_cast<size_t>(5), copied_all);
    for (size_t i = 0; i < 5; ++i) {
      Assert::AreEqual(static_cast<double>(i), copy_buf[i]);
    }

    // Push past capacity (60) to verify wrap-around behavior.
    for (int i = 5; i < 100; ++i) {
      buffer.Push(static_cast<double>(i));
    }
    Assert::AreEqual(static_cast<size_t>(60), buffer.Size());
    Assert::AreEqual(40.0, buffer.At(0));
    Assert::AreEqual(99.0, buffer.At(59));
    Assert::AreEqual(99.0, buffer.Latest());

    const std::vector<double> vec = buffer.ToVector();
    Assert::AreEqual(static_cast<size_t>(60), vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
      Assert::AreEqual(static_cast<double>(40 + i), vec[i]);
    }

    std::array<double, 60> full_copy{};
    Assert::AreEqual(static_cast<size_t>(60),
                     buffer.CopyTo(full_copy.data(), full_copy.size()));
    Assert::AreEqual(40.0, full_copy[0]);
    Assert::AreEqual(99.0, full_copy[59]);

    buffer.Clear();
    Assert::IsTrue(buffer.Empty());
    Assert::AreEqual(static_cast<size_t>(0), buffer.Size());
    Assert::AreEqual(-1.0, buffer.Latest(-1.0));
  }

  TEST_METHOD(FormatUptime_ShouldFormatDaysHoursMinutesSeconds) {
    Assert::AreEqual(std::wstring(L"0:00:00:00"),
                     PerformanceFormatter::FormatUptime(0));
    Assert::AreEqual(std::wstring(L"0:00:00:00"),
                     PerformanceFormatter::FormatUptime(999));

    const uint64_t one_h_two_m_three_s =
        ((1ULL * 3600ULL) + (2ULL * 60ULL) + 3ULL) * 1000ULL;
    Assert::AreEqual(std::wstring(L"0:01:02:03"),
                     PerformanceFormatter::FormatUptime(one_h_two_m_three_s));

    const uint64_t multi_day =
        (((106ULL * 24ULL + 10ULL) * 3600ULL) + (41ULL * 60ULL) + 51ULL) *
        1000ULL;
    Assert::AreEqual(std::wstring(L"106:10:41:51"),
                     PerformanceFormatter::FormatUptime(multi_day));
  }

  TEST_METHOD(FormatMemoryGB_ShouldFormatMbAndGb) {
    constexpr uint64_t kMB = 1024ULL * 1024ULL;
    constexpr uint64_t kGB = 1024ULL * 1024ULL * 1024ULL;

    Assert::AreEqual(std::wstring(L"0 MB"),
                     PerformanceFormatter::FormatMemoryGB(0));
    Assert::AreEqual(std::wstring(L"512 MB"),
                     PerformanceFormatter::FormatMemoryGB(512ULL * kMB));
    Assert::AreEqual(std::wstring(L"1.0 GB"),
                     PerformanceFormatter::FormatMemoryGB(kGB));
    Assert::AreEqual(std::wstring(L"8.0 GB"),
                     PerformanceFormatter::FormatMemoryGB(8ULL * kGB));

    const uint64_t seven_point_four_gb =
        static_cast<uint64_t>(7.4 * static_cast<double>(kGB));
    Assert::AreEqual(
        std::wstring(L"7.4 GB"),
        PerformanceFormatter::FormatMemoryGB(seven_point_four_gb));
  }

  TEST_METHOD(FormatCacheSize_ShouldFormatNaKbAndMb) {
    Assert::AreEqual(std::wstring(L"N/A"),
                     PerformanceFormatter::FormatCacheSize(0));
    Assert::AreEqual(std::wstring(L"該当なし"),
                     PerformanceFormatter::FormatCacheSize(0, L"該当なし"));
    Assert::AreEqual(std::wstring(L"256 KB"),
                     PerformanceFormatter::FormatCacheSize(256ULL * 1024ULL));
    Assert::AreEqual(std::wstring(L"1 KB"),
                     PerformanceFormatter::FormatCacheSize(128ULL));
    Assert::AreEqual(
        std::wstring(L"1.0 MB"),
        PerformanceFormatter::FormatCacheSize(1024ULL * 1024ULL));
    Assert::AreEqual(
        std::wstring(L"15.0 MB"),
        PerformanceFormatter::FormatCacheSize(15ULL * 1024ULL * 1024ULL));
  }

  TEST_METHOD(FormatFrequencyGHz_ShouldFormatTwoDecimalPlaces) {
    Assert::AreEqual(std::wstring(L"0.00 GHz"),
                     PerformanceFormatter::FormatFrequencyGHz(0));
    Assert::AreEqual(std::wstring(L"2.10 GHz"),
                     PerformanceFormatter::FormatFrequencyGHz(2100));
    Assert::AreEqual(std::wstring(L"3.65 GHz"),
                     PerformanceFormatter::FormatFrequencyGHz(3650));
  }

  TEST_METHOD(FormatBitrate_ShouldClampAndScaleUnits) {
    Assert::AreEqual(std::wstring(L"0 Kbps"),
                     PerformanceFormatter::FormatBitrate(-100.0));
    Assert::AreEqual(
        std::wstring(L"0 Kbps"),
        PerformanceFormatter::FormatBitrate(
            std::numeric_limits<double>::quiet_NaN()));
    Assert::AreEqual(std::wstring(L"0 Kbps"),
                     PerformanceFormatter::FormatBitrate(0.0));
    Assert::AreEqual(std::wstring(L"432 Kbps"),
                     PerformanceFormatter::FormatBitrate(432'000.0));
    Assert::AreEqual(std::wstring(L"12.5 Mbps"),
                     PerformanceFormatter::FormatBitrate(12'500'000.0));
    Assert::AreEqual(std::wstring(L"1.2 Gbps"),
                     PerformanceFormatter::FormatBitrate(1'200'000'000.0));
  }

  TEST_METHOD(FormatLinkSpeed_ShouldHandleZeroAndUnits) {
    Assert::AreEqual(std::wstring(L"N/A"),
                     PerformanceFormatter::FormatLinkSpeed(0));
    Assert::AreEqual(std::wstring(L"なし"),
                     PerformanceFormatter::FormatLinkSpeed(0, L"なし"));
    Assert::AreEqual(std::wstring(L"56 Kbps"),
                     PerformanceFormatter::FormatLinkSpeed(56'000ULL));
    Assert::AreEqual(std::wstring(L"100 Mbps"),
                     PerformanceFormatter::FormatLinkSpeed(100'000'000ULL));
    Assert::AreEqual(std::wstring(L"1.0 Gbps"),
                     PerformanceFormatter::FormatLinkSpeed(1'000'000'000ULL));
    Assert::AreEqual(std::wstring(L"2.5 Gbps"),
                     PerformanceFormatter::FormatLinkSpeed(2'500'000'000ULL));
  }

  TEST_METHOD(FormatIntegerWithCommas_ShouldInsertThousandsSeparators) {
    Assert::AreEqual(std::wstring(L"0"),
                     PerformanceFormatter::FormatIntegerWithCommas(0));
    Assert::AreEqual(std::wstring(L"245"),
                     PerformanceFormatter::FormatIntegerWithCommas(245));
    Assert::AreEqual(std::wstring(L"1,000"),
                     PerformanceFormatter::FormatIntegerWithCommas(1000));
    Assert::AreEqual(std::wstring(L"80,005"),
                     PerformanceFormatter::FormatIntegerWithCommas(80005));
    Assert::AreEqual(std::wstring(L"1,234,567"),
                     PerformanceFormatter::FormatIntegerWithCommas(1234567));
  }

  TEST_METHOD(ComputeCpuGridLayout_ShouldReturnBalancedGrid) {
    auto check_layout = [](size_t cores, int expected_rows, int expected_cols) {
      const CpuGridLayout layout =
          PerformanceFormatter::ComputeCpuGridLayout(cores);
      Assert::AreEqual(expected_rows, layout.rows);
      Assert::AreEqual(expected_cols, layout.cols);
      Assert::IsTrue(layout.rows <= layout.cols);
      if (cores > 0) {
        Assert::IsTrue(static_cast<size_t>(layout.rows) *
                           static_cast<size_t>(layout.cols) >=
                       cores);
      }
    };

    check_layout(0, 1, 1);
    check_layout(1, 1, 1);
    check_layout(2, 1, 2);
    check_layout(3, 2, 2);
    check_layout(4, 2, 2);
    check_layout(5, 2, 3);
    check_layout(6, 2, 3);
    check_layout(7, 2, 4);
    check_layout(8, 2, 4);
    check_layout(9, 3, 4);
    check_layout(10, 3, 4);
    check_layout(12, 3, 4);
    check_layout(13, 4, 4);
    check_layout(16, 4, 4);
    check_layout(20, 4, 5);
    check_layout(24, 5, 5);
    check_layout(32, 6, 6);
    check_layout(64, 8, 8);
  }

  TEST_METHOD(ComputeNetworkAutoScale_ShouldSelectExpectedStep) {
    Assert::AreEqual(100'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(-10.0));
    Assert::AreEqual(100'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(0.0));
    Assert::AreEqual(100'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(100'000.0));
    Assert::AreEqual(500'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(100'001.0));
    Assert::AreEqual(1'000'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(900'000.0));
    Assert::AreEqual(5'000'000.0,
                     PerformanceFormatter::ComputeNetworkAutoScale(2'000'000.0));
    Assert::AreEqual(
        10'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(8'000'000.0));
    Assert::AreEqual(
        50'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(25'000'000.0));
    Assert::AreEqual(
        100'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(75'000'000.0));
    Assert::AreEqual(
        500'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(250'000'000.0));
    Assert::AreEqual(
        1'000'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(900'000'000.0));
    Assert::AreEqual(
        10'000'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(10'000'000'000.0));
    Assert::AreEqual(
        25'000'000'000.0,
        PerformanceFormatter::ComputeNetworkAutoScale(20'000'000'000.0));
  }

  TEST_METHOD(ComputeUsagePercent_ShouldHandleZeroAndClamp) {
    Assert::AreEqual(0.0, PerformanceFormatter::ComputeUsagePercent(50, 0));
    Assert::AreEqual(0.0, PerformanceFormatter::ComputeUsagePercent(0, 100));
    Assert::AreEqual(25.0, PerformanceFormatter::ComputeUsagePercent(50, 200));
    Assert::AreEqual(100.0, PerformanceFormatter::ComputeUsagePercent(100, 100));
    Assert::AreEqual(100.0, PerformanceFormatter::ComputeUsagePercent(250, 100));
  }
};

}  // namespace LiteProcManagerTests
