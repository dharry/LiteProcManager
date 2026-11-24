// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PERFORMANCE_DATA_SERVICE_H_
#define LITE_PROC_MANAGER_PERFORMANCE_DATA_SERVICE_H_

#include <vector>

#include "cpu_performance_collector.h"
#include "memory_network_collector.h"
#include "performance_types.h"

namespace lite_proc_manager {

class PerformanceDataService {
 public:
  PerformanceDataService() = default;
  ~PerformanceDataService() = default;

  const PerformanceSnapshot& Sample();
  const PerformanceSnapshot& GetSnapshot() const { return snapshot_; }

  static void UpdateCoreHistories(
      const std::vector<double>& per_core_usage,
      std::vector<RingBuffer<double, 60>>* core_histories);

  static void UpdateNetworkHistories(
      const std::vector<NetworkAdapterSample>& adapters,
      std::vector<NetworkAdapterHistory>* histories);

 private:
  CpuPerformanceCollector cpu_collector_;
  MemoryPerformanceCollector memory_collector_;
  NetworkPerformanceCollector network_collector_;
  PerformanceSnapshot snapshot_;
  bool static_initialized_{false};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PERFORMANCE_DATA_SERVICE_H_
