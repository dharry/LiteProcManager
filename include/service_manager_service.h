// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_
#define LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

#include "service_item.h"

namespace lite_proc_manager {

class ServiceManagerService {
 public:
  ServiceManagerService() = default;
  ~ServiceManagerService() = default;

  // Retrieve current snapshot of all services
  std::vector<std::shared_ptr<ServiceItem>> GetServicesSnapshot();

  // Control operations
  bool StartServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool StopServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool RestartServiceByName(const std::wstring& service_name, std::wstring* error_msg = nullptr);
  bool ChangeStartupType(const std::wstring& service_name, DWORD new_start_type, std::wstring* error_msg = nullptr);
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_SERVICE_MANAGER_SERVICE_H_
