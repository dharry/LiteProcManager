// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_CPU_PERFORMANCE_COLLECTOR_H_
#define LITE_PROC_MANAGER_CPU_PERFORMANCE_COLLECTOR_H_

#include <cstdint>
#include <vector>

#include "performance_types.h"

namespace lite_proc_manager {

// Collects static CPU hardware topology/metadata and periodic CPU utilization
// and system-wide performance samples.
class CpuPerformanceCollector {
 public:
  CpuPerformanceCollector();
  ~CpuPerformanceCollector() = default;

  CpuPerformanceCollector(const CpuPerformanceCollector&) = delete;
  CpuPerformanceCollector& operator=(const CpuPerformanceCollector&) = delete;
  CpuPerformanceCollector(CpuPerformanceCollector&&) = default;
  CpuPerformanceCollector& operator=(CpuPerformanceCollector&&) = default;

  // Returns static CPU metadata, caching the result after the first call.
  CpuStaticInfo GetStaticInfo();

  // Samples current per-core and overall CPU utilization, speed, counts, and
  // system uptime. The first call records baseline times and returns 0.0%
  // utilization.
  CpuSample Sample();

  // Computes core utilization percentage in [0.0, 100.0] from two snapshots of
  // SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION counters. KernelTime includes
  // IdleTime in Windows, so active kernel time is delta_kernel - delta_idle.
  static double ComputeCoreUsageDelta(uint64_t prev_idle,
                                      uint64_t prev_kernel,
                                      uint64_t prev_user,
                                      uint64_t cur_idle,
                                      uint64_t cur_kernel,
                                      uint64_t cur_user);

 private:
  struct CoreTimeSample {
    uint64_t idle_time{0};
    uint64_t kernel_time{0};
    uint64_t user_time{0};
  };

  static CpuStaticInfo QueryStaticInfo();

  bool static_info_cached_{false};
  CpuStaticInfo cached_static_info_;
  std::vector<CoreTimeSample> prev_core_times_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_CPU_PERFORMANCE_COLLECTOR_H_
