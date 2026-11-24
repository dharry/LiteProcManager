// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <thread>
#include <vector>

#include "../include/performance_data_service.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace LiteProcManagerTests {

TEST_CLASS(PerformanceDataServiceTests) {
 public:
  TEST_METHOD(UpdateCoreHistories_ShouldResizeAndAccumulateUpTo60Samples) {
    std::vector<RingBuffer<double, 60>> histories;
    for (int step = 0; step < 65; ++step) {
      std::vector<double> cores = {
          static_cast<double>(step % 100),
          static_cast<double>((step + 10) % 100),
      };
      PerformanceDataService::UpdateCoreHistories(cores, &histories);
    }

    Assert::AreEqual(static_cast<size_t>(2), histories.size());
    Assert::AreEqual(static_cast<size_t>(60), histories[0].Size());
    Assert::AreEqual(5.0, histories[0].At(0), 0.001);
    Assert::AreEqual(64.0, histories[0].Latest(), 0.001);
    Assert::AreEqual(74.0, histories[1].Latest(), 0.001);
  }

  TEST_METHOD(UpdateNetworkHistories_ShouldPreserveExistingAndPruneStaleAdapters) {
    std::vector<NetworkAdapterHistory> histories;

    NetworkAdapterSample a1;
    a1.luid_value = 1001;
    a1.send_bps = 1200.0;
    a1.recv_bps = 3400.0;

    NetworkAdapterSample a2;
    a2.luid_value = 2002;
    a2.send_bps = 500.0;
    a2.recv_bps = 900.0;

    PerformanceDataService::UpdateNetworkHistories({a1, a2}, &histories);
    Assert::AreEqual(static_cast<size_t>(2), histories.size());

    a1.send_bps = 2400.0;
    a1.recv_bps = 6800.0;
    PerformanceDataService::UpdateNetworkHistories({a1}, &histories);

    Assert::AreEqual(static_cast<size_t>(1), histories.size());
    Assert::AreEqual(static_cast<uint64_t>(1001), histories[0].luid_value);
    Assert::AreEqual(static_cast<size_t>(2),
                     histories[0].send_bps_history.Size());
    Assert::AreEqual(1200.0, histories[0].send_bps_history.At(0), 0.001);
    Assert::AreEqual(2400.0, histories[0].send_bps_history.Latest(), 0.001);
    Assert::AreEqual(6800.0, histories[0].recv_bps_history.Latest(), 0.001);
  }

  TEST_METHOD(LiveSample_ShouldPopulateHistoriesAndStaticInfo) {
    PerformanceDataService service;
    const auto& s1 = service.Sample();
    Assert::IsFalse(s1.cpu_static.brand_name.empty());
    Assert::IsTrue(s1.cpu_static.logical_processor_count >= 1);
    Assert::AreEqual(static_cast<size_t>(1), s1.cpu_overall_history.Size());
    Assert::AreEqual(static_cast<size_t>(1), s1.memory_usage_history.Size());
    Assert::AreEqual(static_cast<size_t>(s1.cpu_static.logical_processor_count),
                     s1.cpu_core_histories.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto& s2 = service.Sample();
    Assert::AreEqual(static_cast<size_t>(2), s2.cpu_overall_history.Size());
    Assert::AreEqual(static_cast<size_t>(2), s2.memory_usage_history.Size());
    Assert::IsTrue(s2.memory.total_physical_bytes > 0);
  }
};

}  // namespace LiteProcManagerTests
