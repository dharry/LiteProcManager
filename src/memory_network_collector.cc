// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Winsock / IP Helper headers must precede <windows.h>.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <windows.h>
#include <psapi.h>

#include "memory_network_collector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "psapi.lib")

namespace lite_proc_manager {

namespace {

struct MibIfTable2Deleter {
  void operator()(PMIB_IF_TABLE2 table) const noexcept {
    if (table != nullptr) {
      FreeMibTable(table);
    }
  }
};

using UniqueMibIfTable2 = std::unique_ptr<MIB_IF_TABLE2, MibIfTable2Deleter>;

struct AdapterAddressMetadata {
  std::wstring friendly_name;
  std::wstring description;
  std::wstring ipv4_address;
  std::wstring ipv6_address;
};

std::wstring FormatIpAddress(const SOCKET_ADDRESS& socket_address,
                             bool* is_link_local_v6) {
  if (is_link_local_v6 != nullptr) {
    *is_link_local_v6 = false;
  }
  if (socket_address.lpSockaddr == nullptr) {
    return {};
  }

  const ADDRESS_FAMILY family = socket_address.lpSockaddr->sa_family;
  if (family == AF_INET) {
    if (socket_address.iSockaddrLength < static_cast<INT>(sizeof(sockaddr_in))) {
      return {};
    }
    const auto* addr_in =
        reinterpret_cast<const sockaddr_in*>(socket_address.lpSockaddr);
    wchar_t buffer[INET_ADDRSTRLEN]{};
    if (InetNtopW(AF_INET, &addr_in->sin_addr, buffer, INET_ADDRSTRLEN) !=
        nullptr) {
      return std::wstring(buffer);
    }
  } else if (family == AF_INET6) {
    if (socket_address.iSockaddrLength <
        static_cast<INT>(sizeof(sockaddr_in6))) {
      return {};
    }
    const auto* addr_in6 =
        reinterpret_cast<const sockaddr_in6*>(socket_address.lpSockaddr);
    if (is_link_local_v6 != nullptr) {
      *is_link_local_v6 = IN6_IS_ADDR_LINKLOCAL(&addr_in6->sin6_addr) != 0;
    }
    wchar_t buffer[INET6_ADDRSTRLEN]{};
    if (InetNtopW(AF_INET6, &addr_in6->sin6_addr, buffer, INET6_ADDRSTRLEN) !=
        nullptr) {
      return std::wstring(buffer);
    }
  }

  return {};
}

std::unordered_map<uint64_t, AdapterAddressMetadata> QueryAdapterMetadata() {
  std::unordered_map<uint64_t, AdapterAddressMetadata> metadata_by_luid;

  ULONG buffer_size = 16384;
  std::vector<uint8_t> buffer(buffer_size);
  DWORD result = ERROR_BUFFER_OVERFLOW;

  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    auto* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    result = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, addresses, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW && buffer_size > 0) {
      buffer.resize(buffer_size);
      continue;
    }
    break;
  }

  if (result != ERROR_SUCCESS) {
    return metadata_by_luid;
  }

  const auto* current =
      reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
  while (current != nullptr) {
    AdapterAddressMetadata entry;
    if (current->FriendlyName != nullptr) {
      entry.friendly_name = current->FriendlyName;
    }
    if (current->Description != nullptr) {
      entry.description = current->Description;
    }

    bool has_non_link_local_v6 = false;
    const IP_ADAPTER_UNICAST_ADDRESS* unicast = current->FirstUnicastAddress;
    while (unicast != nullptr) {
      if (unicast->Address.lpSockaddr != nullptr) {
        const ADDRESS_FAMILY family = unicast->Address.lpSockaddr->sa_family;
        if (family == AF_INET && entry.ipv4_address.empty()) {
          entry.ipv4_address = FormatIpAddress(unicast->Address, nullptr);
        } else if (family == AF_INET6) {
          bool is_link_local = false;
          std::wstring v6 = FormatIpAddress(unicast->Address, &is_link_local);
          if (!v6.empty()) {
            // Prefer routable IPv6 addresses over link-local (fe80::) when both exist.
            if (entry.ipv6_address.empty() ||
                (!has_non_link_local_v6 && !is_link_local)) {
              entry.ipv6_address = std::move(v6);
              has_non_link_local_v6 = !is_link_local;
            }
          }
        }
      }
      unicast = unicast->Next;
    }

    metadata_by_luid[current->Luid.Value] = std::move(entry);
    current = current->Next;
  }

  return metadata_by_luid;
}

}  // namespace

MemorySample MemoryPerformanceCollector::Sample() const {
  uint64_t total_physical_bytes = 0;
  uint64_t available_bytes = 0;

  MEMORYSTATUSEX mem_status{};
  mem_status.dwLength = sizeof(mem_status);
  const bool got_mem_status = GlobalMemoryStatusEx(&mem_status) != FALSE;
  if (got_mem_status) {
    total_physical_bytes = static_cast<uint64_t>(mem_status.ullTotalPhys);
    available_bytes = static_cast<uint64_t>(mem_status.ullAvailPhys);
  }

  uint64_t commit_total_pages = 0;
  uint64_t commit_limit_pages = 0;
  uint64_t system_cache_pages = 0;
  uint64_t kernel_paged_pages = 0;
  uint64_t kernel_nonpaged_pages = 0;
  uint64_t page_size_bytes = 0;

  PERFORMANCE_INFORMATION perf_info{};
  perf_info.cb = sizeof(perf_info);
  if (GetPerformanceInfo(&perf_info, sizeof(perf_info)) != FALSE) {
    commit_total_pages = static_cast<uint64_t>(perf_info.CommitTotal);
    commit_limit_pages = static_cast<uint64_t>(perf_info.CommitLimit);
    system_cache_pages = static_cast<uint64_t>(perf_info.SystemCache);
    kernel_paged_pages = static_cast<uint64_t>(perf_info.KernelPaged);
    kernel_nonpaged_pages = static_cast<uint64_t>(perf_info.KernelNonpaged);
    page_size_bytes = static_cast<uint64_t>(perf_info.PageSize);

    if (!got_mem_status) {
      total_physical_bytes =
          static_cast<uint64_t>(perf_info.PhysicalTotal) * page_size_bytes;
      available_bytes =
          static_cast<uint64_t>(perf_info.PhysicalAvailable) * page_size_bytes;
    }
  }

  return ComputeMemorySample(total_physical_bytes, available_bytes,
                             commit_total_pages, commit_limit_pages,
                             system_cache_pages, kernel_paged_pages,
                             kernel_nonpaged_pages, page_size_bytes);
}

MemorySample MemoryPerformanceCollector::ComputeMemorySample(
    uint64_t total_physical_bytes,
    uint64_t available_bytes,
    uint64_t commit_total_pages,
    uint64_t commit_limit_pages,
    uint64_t system_cache_pages,
    uint64_t kernel_paged_pages,
    uint64_t kernel_nonpaged_pages,
    uint64_t page_size_bytes) {
  MemorySample sample;
  sample.total_physical_bytes = total_physical_bytes;
  sample.available_bytes = available_bytes;
  sample.in_use_bytes = (total_physical_bytes >= available_bytes)
                            ? (total_physical_bytes - available_bytes)
                            : 0;
  sample.committed_bytes = commit_total_pages * page_size_bytes;
  sample.commit_limit_bytes = commit_limit_pages * page_size_bytes;
  sample.cached_bytes = system_cache_pages * page_size_bytes;
  sample.paged_pool_bytes = kernel_paged_pages * page_size_bytes;
  sample.non_paged_pool_bytes = kernel_nonpaged_pages * page_size_bytes;

  if (total_physical_bytes > 0) {
    const double raw_percent =
        (static_cast<double>(sample.in_use_bytes) /
         static_cast<double>(total_physical_bytes)) *
        100.0;
    sample.usage_percent = std::clamp(raw_percent, 0.0, 100.0);
  } else {
    sample.usage_percent = 0.0;
  }

  return sample;
}

std::vector<NetworkAdapterSample> NetworkPerformanceCollector::Sample() {
  PMIB_IF_TABLE2 raw_table = nullptr;
  const DWORD table_status = GetIfTable2(&raw_table);
  UniqueMibIfTable2 table(raw_table);
  if (table_status != NO_ERROR || !table) {
    return {};
  }

  const auto metadata_by_luid = QueryAdapterMetadata();
  const auto now = std::chrono::steady_clock::now();

  std::vector<NetworkAdapterSample> samples;
  std::unordered_map<uint64_t, AdapterByteCounters> next_counters;

  constexpr uint64_t kUnknownLinkSpeed =
      (std::numeric_limits<uint64_t>::max)();

  for (ULONG i = 0; i < table->NumEntries; ++i) {
    const MIB_IF_ROW2& row = table->Table[i];

    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) {
      continue;
    }

    if (row.InterfaceAndOperStatusFlags.HardwareInterface != TRUE) {
      continue;
    }

    if (row.Type != IF_TYPE_ETHERNET_CSMACD &&
        row.Type != IF_TYPE_IEEE80211) {
      continue;
    }

    NetworkAdapterSample adapter;
    adapter.luid_value = row.InterfaceLuid.Value;
    adapter.connection_type =
        (row.Type == IF_TYPE_IEEE80211) ? L"Wi-Fi" : L"Ethernet";
    adapter.is_connected = (row.OperStatus == IfOperStatusUp);

    if (row.TransmitLinkSpeed != kUnknownLinkSpeed &&
        row.TransmitLinkSpeed > 0) {
      adapter.link_speed_bps = row.TransmitLinkSpeed;
    } else if (row.ReceiveLinkSpeed != kUnknownLinkSpeed &&
               row.ReceiveLinkSpeed > 0) {
      adapter.link_speed_bps = row.ReceiveLinkSpeed;
    }

    const auto meta_it = metadata_by_luid.find(adapter.luid_value);
    if (meta_it != metadata_by_luid.end()) {
      adapter.friendly_name = meta_it->second.friendly_name;
      adapter.description = meta_it->second.description;
      adapter.ipv4_address = meta_it->second.ipv4_address;
      adapter.ipv6_address = meta_it->second.ipv6_address;
    }

    if (adapter.friendly_name.empty()) {
      adapter.friendly_name = row.Alias;
    }
    if (adapter.description.empty()) {
      adapter.description = row.Description;
    }

    const auto prev_it = prev_counters_.find(adapter.luid_value);
    if (prev_it != prev_counters_.end()) {
      const double elapsed_seconds =
          std::chrono::duration<double>(now - prev_it->second.timestamp).count();
      adapter.recv_bps = ComputeThroughputBps(
          prev_it->second.in_octets, row.InOctets, elapsed_seconds);
      adapter.send_bps = ComputeThroughputBps(
          prev_it->second.out_octets, row.OutOctets, elapsed_seconds);
    }

    next_counters[adapter.luid_value] =
        AdapterByteCounters{row.InOctets, row.OutOctets, now};
    samples.push_back(std::move(adapter));
  }

  prev_counters_ = std::move(next_counters);

  std::sort(samples.begin(), samples.end(),
            [](const NetworkAdapterSample& a, const NetworkAdapterSample& b) {
              if (a.is_connected != b.is_connected) {
                return a.is_connected && !b.is_connected;
              }
              if (a.friendly_name != b.friendly_name) {
                return a.friendly_name < b.friendly_name;
              }
              return a.luid_value < b.luid_value;
            });

  return samples;
}

double NetworkPerformanceCollector::ComputeThroughputBps(
    uint64_t prev_bytes,
    uint64_t cur_bytes,
    double elapsed_seconds) {
  if (!(elapsed_seconds > 0.0) || !std::isfinite(elapsed_seconds) ||
      cur_bytes < prev_bytes) {
    return 0.0;
  }
  return static_cast<double>(cur_bytes - prev_bytes) * 8.0 / elapsed_seconds;
}

}  // namespace lite_proc_manager
