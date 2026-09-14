// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_
#define LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "service_item.h"

namespace lite_proc_manager {

class ServiceManagerService {
 public:
  ServiceManagerService() = default;
  ~ServiceManagerService() = default;

  // Retrieve current snapshot of all services
  std::vector<std::shared_ptr<ServiceItem>> GetServicesSnapshot(
      const std::atomic_bool* cancellation = nullptr);

  // Control operations
  bool StartServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool StopServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool RestartServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool ChangeStartupType(const std::wstring& service_name, DWORD new_start_type, std::wstring* error_msg = nullptr);

 private:
  struct CachedServiceConfig {
    DWORD start_type{SERVICE_DEMAND_START};
    std::wstring account_name;
    std::wstring description;
    std::chrono::steady_clock::time_point refreshed_at;
  };

  std::mutex config_cache_mutex_;
  std::unordered_map<std::wstring, CachedServiceConfig> config_cache_;
  uint64_t config_cache_revision_{0};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_
