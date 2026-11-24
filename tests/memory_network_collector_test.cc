// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

#include "../include/memory_network_collector.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace LiteProcManagerTests {

TEST_CLASS(MemoryNetworkCollectorTests) {
 public:
  TEST_METHOD(ComputeMemorySample_NormalValues) {
    constexpr uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    constexpr uint64_t kPageSize = 4096ULL;
    const MemorySample sample = MemoryPerformanceCollector::ComputeMemorySample(
        /*total_physical_bytes=*/16ULL * kGiB,
        /*available_bytes=*/6ULL * kGiB,
        /*commit_total_pages=*/3000000ULL,
        /*commit_limit_pages=*/5000000ULL,
        /*system_cache_pages=*/500000ULL,
        /*kernel_paged_pages=*/100000ULL,
        /*kernel_nonpaged_pages=*/50000ULL,
        /*page_size_bytes=*/kPageSize);

    Assert::AreEqual(16ULL * kGiB, sample.total_physical_bytes);
    Assert::AreEqual(6ULL * kGiB, sample.available_bytes);
    Assert::AreEqual(10ULL * kGiB, sample.in_use_bytes);
    Assert::AreEqual(3000000ULL * kPageSize, sample.committed_bytes);
    Assert::AreEqual(5000000ULL * kPageSize, sample.commit_limit_bytes);
    Assert::AreEqual(500000ULL * kPageSize, sample.cached_bytes);
    Assert::AreEqual(100000ULL * kPageSize, sample.paged_pool_bytes);
    Assert::AreEqual(50000ULL * kPageSize, sample.non_paged_pool_bytes);
    Assert::IsTrue(std::abs(sample.usage_percent - 62.5) < 1e-6);
  }

  TEST_METHOD(ComputeMemorySample_ZeroTotalPhysicalReturnsZeroUsage) {
    const MemorySample sample = MemoryPerformanceCollector::ComputeMemorySample(
        /*total_physical_bytes=*/0ULL,
        /*available_bytes=*/1024ULL,
        /*commit_total_pages=*/10ULL,
        /*commit_limit_pages=*/20ULL,
        /*system_cache_pages=*/5ULL,
        /*kernel_paged_pages=*/2ULL,
        /*kernel_nonpaged_pages=*/1ULL,
        /*page_size_bytes=*/4096ULL);

    Assert::AreEqual(0ULL, sample.total_physical_bytes);
    Assert::AreEqual(0ULL, sample.in_use_bytes);
    Assert::AreEqual(0.0, sample.usage_percent);
  }

  TEST_METHOD(ComputeMemorySample_AvailableExceedsTotalClampsInUseAndPercent) {
    const MemorySample sample = MemoryPerformanceCollector::ComputeMemorySample(
        /*total_physical_bytes=*/4096ULL,
        /*available_bytes=*/8192ULL,
        /*commit_total_pages=*/1ULL,
        /*commit_limit_pages=*/2ULL,
        /*system_cache_pages=*/3ULL,
        /*kernel_paged_pages=*/4ULL,
        /*kernel_nonpaged_pages=*/5ULL,
        /*page_size_bytes=*/4096ULL);

    Assert::AreEqual(0ULL, sample.in_use_bytes);
    Assert::AreEqual(0.0, sample.usage_percent);
  }

  TEST_METHOD(ComputeMemorySample_PageMultiplicationsAreExact) {
    constexpr uint64_t kPageSize = 4096ULL;
    const MemorySample sample = MemoryPerformanceCollector::ComputeMemorySample(
        /*total_physical_bytes=*/1000000ULL,
        /*available_bytes=*/0ULL,
        /*commit_total_pages=*/123456ULL,
        /*commit_limit_pages=*/654321ULL,
        /*system_cache_pages=*/11111ULL,
        /*kernel_paged_pages=*/22222ULL,
        /*kernel_nonpaged_pages=*/33333ULL,
        /*page_size_bytes=*/kPageSize);

    Assert::AreEqual(1000000ULL, sample.in_use_bytes);
    Assert::AreEqual(100.0, sample.usage_percent);
    Assert::AreEqual(123456ULL * kPageSize, sample.committed_bytes);
    Assert::AreEqual(654321ULL * kPageSize, sample.commit_limit_bytes);
    Assert::AreEqual(11111ULL * kPageSize, sample.cached_bytes);
    Assert::AreEqual(22222ULL * kPageSize, sample.paged_pool_bytes);
    Assert::AreEqual(33333ULL * kPageSize, sample.non_paged_pool_bytes);
  }

  TEST_METHOD(ComputeThroughputBps_NormalElapsedAndDelta) {
    // 125,000 bytes over 1.0 second = 1,000,000 bps (1 Mbps).
    const double bps = NetworkPerformanceCollector::ComputeThroughputBps(
        1000ULL, 126000ULL, 1.0);
    Assert::IsTrue(std::abs(bps - 1000000.0) < 1e-6);
  }

  TEST_METHOD(ComputeThroughputBps_ZeroOrNegativeElapsedReturnsZero) {
    Assert::AreEqual(
        0.0, NetworkPerformanceCollector::ComputeThroughputBps(100ULL, 500ULL, 0.0));
    Assert::AreEqual(
        0.0, NetworkPerformanceCollector::ComputeThroughputBps(100ULL, 500ULL, -1.5));
    Assert::AreEqual(
        0.0, NetworkPerformanceCollector::ComputeThroughputBps(
                 100ULL, 500ULL, std::numeric_limits<double>::quiet_NaN()));
    Assert::AreEqual(
        0.0, NetworkPerformanceCollector::ComputeThroughputBps(
                 100ULL, 500ULL, std::numeric_limits<double>::infinity()));
  }

  TEST_METHOD(ComputeThroughputBps_CounterResetOrWrapReturnsZero) {
    Assert::AreEqual(
        0.0, NetworkPerformanceCollector::ComputeThroughputBps(5000ULL, 100ULL, 1.0));
  }

  TEST_METHOD(MemoryCollector_LiveSampleSmokeTest) {
    MemoryPerformanceCollector collector;
    const MemorySample sample = collector.Sample();

    Assert::IsTrue(sample.total_physical_bytes > 0);
    Assert::IsTrue(sample.in_use_bytes <= sample.total_physical_bytes);
    Assert::IsTrue(sample.usage_percent >= 0.0 && sample.usage_percent <= 100.0);
    Assert::IsTrue(sample.commit_limit_bytes > 0);
  }

  TEST_METHOD(NetworkCollector_LiveSampleSmokeTest) {
    NetworkPerformanceCollector collector;
    const auto first = collector.Sample();
    for (const auto& adapter : first) {
      Assert::AreEqual(0.0, adapter.recv_bps);
      Assert::AreEqual(0.0, adapter.send_bps);
      Assert::IsTrue(adapter.connection_type == L"Ethernet" ||
                     adapter.connection_type == L"Wi-Fi");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto second = collector.Sample();
    for (const auto& adapter : second) {
      Assert::IsTrue(adapter.recv_bps >= 0.0 && std::isfinite(adapter.recv_bps));
      Assert::IsTrue(adapter.send_bps >= 0.0 && std::isfinite(adapter.send_bps));
    }
  }
};

}  // namespace LiteProcManagerTests
