// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "performance_data_service.h"

#include <algorithm>

namespace lite_proc_manager {

const PerformanceSnapshot& PerformanceDataService::Sample() {
  if (!static_initialized_) {
    snapshot_.cpu_static = cpu_collector_.GetStaticInfo();
    static_initialized_ = true;
  }

  snapshot_.cpu = cpu_collector_.Sample();
  snapshot_.memory = memory_collector_.Sample();
  snapshot_.network_adapters = network_collector_.Sample();

  snapshot_.cpu_overall_history.Push(snapshot_.cpu.overall_usage_percent);
  UpdateCoreHistories(snapshot_.cpu.per_core_usage_percent,
                      &snapshot_.cpu_core_histories);
  snapshot_.memory_usage_history.Push(snapshot_.memory.usage_percent);
  UpdateNetworkHistories(snapshot_.network_adapters,
                         &snapshot_.network_histories);

  return snapshot_;
}

void PerformanceDataService::UpdateCoreHistories(
    const std::vector<double>& per_core_usage,
    std::vector<RingBuffer<double, 60>>* core_histories) {
  if (!core_histories) return;
  if (core_histories->size() != per_core_usage.size()) {
    core_histories->assign(per_core_usage.size(), RingBuffer<double, 60>{});
  }
  for (size_t i = 0; i < per_core_usage.size(); ++i) {
    (*core_histories)[i].Push(per_core_usage[i]);
  }
}

void PerformanceDataService::UpdateNetworkHistories(
    const std::vector<NetworkAdapterSample>& adapters,
    std::vector<NetworkAdapterHistory>* histories) {
  if (!histories) return;

  std::vector<NetworkAdapterHistory> updated;
  updated.reserve(adapters.size());

  for (const auto& adapter : adapters) {
    auto it = std::find_if(histories->begin(), histories->end(),
                           [&](const NetworkAdapterHistory& entry) {
                             return entry.luid_value == adapter.luid_value;
                           });
    NetworkAdapterHistory entry;
    if (it != histories->end()) {
      entry = std::move(*it);
    } else {
      entry.luid_value = adapter.luid_value;
    }
    entry.send_bps_history.Push(adapter.send_bps);
    entry.recv_bps_history.Push(adapter.recv_bps);
    updated.push_back(std::move(entry));
  }

  *histories = std::move(updated);
}

}  // namespace lite_proc_manager
