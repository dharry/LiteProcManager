// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_MEMORY_NETWORK_COLLECTOR_H_
#define LITE_PROC_MANAGER_MEMORY_NETWORK_COLLECTOR_H_

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "performance_types.h"

namespace lite_proc_manager {

// Collects system physical and commit memory metrics.
class MemoryPerformanceCollector {
 public:
  MemorySample Sample() const;

  static MemorySample ComputeMemorySample(
      uint64_t total_physical_bytes,
      uint64_t available_bytes,
      uint64_t commit_total_pages,
      uint64_t commit_limit_pages,
      uint64_t system_cache_pages,
      uint64_t kernel_paged_pages,
      uint64_t kernel_nonpaged_pages,
      uint64_t page_size_bytes);
};

// Collects per-adapter network metadata and send/receive throughput.
class NetworkPerformanceCollector {
 public:
  std::vector<NetworkAdapterSample> Sample();

  static double ComputeThroughputBps(uint64_t prev_bytes,
                                     uint64_t cur_bytes,
                                     double elapsed_seconds);

 private:
  struct AdapterByteCounters {
    uint64_t in_octets{0};
    uint64_t out_octets{0};
    std::chrono::steady_clock::time_point timestamp{};
  };

  std::unordered_map<uint64_t, AdapterByteCounters> prev_counters_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_MEMORY_NETWORK_COLLECTOR_H_
