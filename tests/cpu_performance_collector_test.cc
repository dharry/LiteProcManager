// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <chrono>
#include <thread>

#include "../include/cpu_performance_collector.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace LiteProcManagerTests {

TEST_CLASS(CpuPerformanceCollectorTests) {
 public:
  TEST_METHOD(ComputeCoreUsageDelta_ZeroPercentWhenAllTimeIsIdle) {
    // KernelTime includes IdleTime, so 100% idle means delta_kernel == delta_idle
    // and delta_user == 0.
    const double usage = CpuPerformanceCollector::ComputeCoreUsageDelta(
        1000, 1000, 500, 2000, 2000, 500);
    Assert::AreEqual(0.0, usage, 1e-6);
  }

  TEST_METHOD(ComputeCoreUsageDelta_FiftyPercentMixedKernelAndUser) {
    // delta_idle = 500, delta_kernel = 750 (250 active kernel + 500 idle),
    // delta_user = 250 -> total = 1000, active = 500 -> 50%.
    const double usage = CpuPerformanceCollector::ComputeCoreUsageDelta(
        100, 200, 300, 600, 950, 550);
    Assert::AreEqual(50.0, usage, 1e-6);
  }

  TEST_METHOD(ComputeCoreUsageDelta_HundredPercentWhenNoIdleDelta) {
    const double usage = CpuPerformanceCollector::ComputeCoreUsageDelta(
        500, 1000, 2000, 500, 1400, 2600);
    Assert::AreEqual(100.0, usage, 1e-6);
  }

  TEST_METHOD(ComputeCoreUsageDelta_KernelIncludesIdleEdgeCaseClampsToZero) {
    // Even if delta_idle slightly exceeds delta_kernel due to sampling skew,
    // active_kernel must be 0 rather than underflowing unsigned subtraction.
    const double usage = CpuPerformanceCollector::ComputeCoreUsageDelta(
        100, 100, 50, 300, 250, 50);
    Assert::AreEqual(0.0, usage, 1e-6);

    // With user activity present, only user delta contributes.
    const double usage_with_user =
        CpuPerformanceCollector::ComputeCoreUsageDelta(
            100, 100, 50, 300, 250, 200);
    // delta_kernel = 150, delta_user = 150 -> total = 300, active = 150 -> 50%.
    Assert::AreEqual(50.0, usage_with_user, 1e-6);
  }

  TEST_METHOD(ComputeCoreUsageDelta_NonMonotonicCountersReturnZero) {
    Assert::AreEqual(0.0,
                     CpuPerformanceCollector::ComputeCoreUsageDelta(
                         200, 500, 100, 199, 600, 200),
                     1e-6);
    Assert::AreEqual(0.0,
                     CpuPerformanceCollector::ComputeCoreUsageDelta(
                         200, 500, 100, 250, 499, 200),
                     1e-6);
    Assert::AreEqual(0.0,
                     CpuPerformanceCollector::ComputeCoreUsageDelta(
                         200, 500, 100, 250, 600, 99),
                     1e-6);
  }

  TEST_METHOD(ComputeCoreUsageDelta_ZeroTotalReturnsZero) {
    const double usage = CpuPerformanceCollector::ComputeCoreUsageDelta(
        100, 200, 300, 100, 200, 300);
    Assert::AreEqual(0.0, usage, 1e-6);
  }

  TEST_METHOD(LiveSmokeTest_StaticInfoAndTwoSamples) {
    CpuPerformanceCollector collector;
    const CpuStaticInfo static_info = collector.GetStaticInfo();
    const CpuStaticInfo cached_info = collector.GetStaticInfo();

    Assert::IsTrue(static_info.logical_processor_count >=
                   static_info.physical_core_count);
    Assert::IsTrue(static_info.physical_core_count >= 1);
    Assert::IsTrue(static_info.socket_count >= 1);
    Assert::IsFalse(static_info.brand_name.empty());
    Assert::AreEqual(static_info.brand_name, cached_info.brand_name);
    Assert::AreEqual(static_info.logical_processor_count,
                     cached_info.logical_processor_count);

    const CpuSample first = collector.Sample();
    Assert::AreEqual(static_cast<size_t>(static_info.logical_processor_count),
                     first.per_core_usage_percent.size());
    Assert::AreEqual(0.0, first.overall_usage_percent, 1e-6);
    for (double core_usage : first.per_core_usage_percent) {
      Assert::AreEqual(0.0, core_usage, 1e-6);
    }
    Assert::IsTrue(first.process_count > 0);
    Assert::IsTrue(first.thread_count > 0);
    Assert::IsTrue(first.handle_count > 0);
    Assert::IsTrue(first.uptime_ms > 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const CpuSample second = collector.Sample();
    Assert::AreEqual(static_cast<size_t>(static_info.logical_processor_count),
                     second.per_core_usage_percent.size());
    Assert::IsTrue(second.overall_usage_percent >= 0.0 &&
                   second.overall_usage_percent <= 100.0);
    for (double core_usage : second.per_core_usage_percent) {
      Assert::IsTrue(core_usage >= 0.0 && core_usage <= 100.0);
    }
    Assert::IsTrue(second.process_count > 0);
    Assert::IsTrue(second.thread_count > 0);
    Assert::IsTrue(second.handle_count > 0);
    Assert::IsTrue(second.uptime_ms >= first.uptime_ms);
  }
};

}  // namespace LiteProcManagerTests
