// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_SERVICE_ITEM_H_
#define LITE_PROC_MANAGER_SERVICE_ITEM_H_

#include <windows.h>
#include <string>

namespace lite_proc_manager {

struct ServiceItem {
  std::wstring service_name;
  std::wstring display_name;
  DWORD pid{0};
  DWORD state{SERVICE_STOPPED};
  DWORD start_type{SERVICE_DEMAND_START};
  std::wstring account_name;
  std::wstring description;

  std::wstring GetStateString() const;
  std::wstring GetStartTypeString() const;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_SERVICE_ITEM_H_
