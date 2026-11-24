// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_PERFORMANCE_TYPES_H_
#define LITE_PROC_MANAGER_PERFORMANCE_TYPES_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lite_proc_manager {

enum class PerformanceResourceType {
  kCpu = 0,
  kMemory = 1,
  kNetwork = 2,
};

enum class CpuGraphMode {
  kLogicalProcessors = 0,
  kOverall = 1,
};

template <typename T, size_t kCapacity>
class RingBuffer {
  static_assert(kCapacity > 0, "RingBuffer capacity must be positive");

 public:
  void Push(const T& value) {
    buffer_[head_] = value;
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) {
      ++count_;
    }
  }

  size_t Size() const { return count_; }
  static constexpr size_t Capacity() { return kCapacity; }
  bool Empty() const { return count_ == 0; }

  // Index 0 is the oldest sample; Size() - 1 is the newest sample.
  const T& At(size_t index) const {
    const size_t oldest = (count_ == kCapacity) ? head_ : 0;
    const size_t actual = (oldest + index) % kCapacity;
    return buffer_[actual];
  }

  T Latest(const T& default_value = T{}) const {
    if (count_ == 0) return default_value;
    return At(count_ - 1);
  }

  void Clear() {
    head_ = 0;
    count_ = 0;
    buffer_.fill(T{});
  }

  std::vector<T> ToVector() const {
    std::vector<T> result;
    result.reserve(count_);
    for (size_t i = 0; i < count_; ++i) {
      result.push_back(At(i));
    }
    return result;
  }

  size_t CopyTo(T* out, size_t max_count) const {
    if (!out || max_count == 0) return 0;
    const size_t n = std::min(count_, max_count);
    const size_t start_offset = count_ - n;
    for (size_t i = 0; i < n; ++i) {
      out[i] = At(start_offset + i);
    }
    return n;
  }

 private:
  std::array<T, kCapacity> buffer_{};
  size_t head_{0};
  size_t count_{0};
};

struct CpuStaticInfo {
  std::wstring brand_name{L"Unknown CPU"};
  uint32_t base_speed_mhz{0};
  uint32_t socket_count{1};
  uint32_t physical_core_count{1};
  uint32_t logical_processor_count{1};
  bool is_virtual_machine{false};
  uint64_t l1_cache_bytes{0};
  uint64_t l2_cache_bytes{0};
  uint64_t l3_cache_bytes{0};
};

struct CpuSample {
  double overall_usage_percent{0.0};
  std::vector<double> per_core_usage_percent;
  uint32_t current_speed_mhz{0};
  uint32_t process_count{0};
  uint32_t thread_count{0};
  uint32_t handle_count{0};
  uint64_t uptime_ms{0};
};

struct MemorySample {
  uint64_t total_physical_bytes{0};
  uint64_t in_use_bytes{0};
  uint64_t available_bytes{0};
  uint64_t committed_bytes{0};
  uint64_t commit_limit_bytes{0};
  uint64_t cached_bytes{0};
  uint64_t paged_pool_bytes{0};
  uint64_t non_paged_pool_bytes{0};
  double usage_percent{0.0};
};

struct NetworkAdapterSample {
  uint64_t luid_value{0};
  std::wstring friendly_name;
  std::wstring description;
  std::wstring connection_type;
  std::wstring ipv4_address;
  std::wstring ipv6_address;
  uint64_t link_speed_bps{0};
  double send_bps{0.0};
  double recv_bps{0.0};
  bool is_connected{false};
};

struct NetworkAdapterHistory {
  uint64_t luid_value{0};
  RingBuffer<double, 60> send_bps_history;
  RingBuffer<double, 60> recv_bps_history;
};

struct PerformanceSnapshot {
  CpuStaticInfo cpu_static;
  CpuSample cpu;
  MemorySample memory;
  std::vector<NetworkAdapterSample> network_adapters;
  RingBuffer<double, 60> cpu_overall_history;
  std::vector<RingBuffer<double, 60>> cpu_core_histories;
  RingBuffer<double, 60> memory_usage_history;
  std::vector<NetworkAdapterHistory> network_histories;
};

struct PerformanceStrings {
  std::wstring cpu_title{L"CPU"};
  std::wstring memory_title{L"Memory"};
  std::wstring network_fallback_title{L"Ethernet"};
  std::wstring utilization{L"Utilization"};
  std::wstring speed{L"Speed"};
  std::wstring processes{L"Processes"};
  std::wstring threads{L"Threads"};
  std::wstring handles{L"Handles"};
  std::wstring uptime{L"Up time"};
  std::wstring base_speed{L"Base speed:"};
  std::wstring sockets{L"Sockets:"};
  std::wstring cores{L"Cores:"};
  std::wstring logical_processors{L"Logical processors:"};
  std::wstring virtual_machine{L"Virtual machine:"};
  std::wstring l1_cache{L"L1 cache:"};
  std::wstring l2_cache{L"L2 cache:"};
  std::wstring l3_cache{L"L3 cache:"};
  std::wstring yes{L"Yes"};
  std::wstring no{L"No"};
  std::wstring not_applicable{L"N/A"};
  std::wstring graph_60s_cpu_pct{L"% Utilization over 60 seconds"};
  std::wstring graph_60s_mem{L"Memory usage"};
  std::wstring graph_60s_net{L"Throughput"};
  std::wstring memory_composition{L"Memory composition"};
  std::wstring in_use{L"In use"};
  std::wstring available{L"Available"};
  std::wstring committed{L"Committed"};
  std::wstring cached{L"Cached"};
  std::wstring paged_pool{L"Paged pool"};
  std::wstring non_paged_pool{L"Non-paged pool"};
  std::wstring total_physical{L"Total physical:"};
  std::wstring send{L"Send"};
  std::wstring receive{L"Receive"};
  std::wstring adapter_name{L"Adapter name:"};
  std::wstring connection_type{L"Connection type:"};
  std::wstring ipv4_address{L"IPv4 address:"};
  std::wstring ipv6_address{L"IPv6 address:"};
  std::wstring link_speed{L"Link speed:"};
  std::wstring menu_graph_logical{L"Logical processors"};
  std::wstring menu_graph_overall{L"Overall utilization"};
  std::wstring no_network_adapters{L"No active network adapters"};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_PERFORMANCE_TYPES_H_
