// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_SERVICE_ENUMERATION_HELPER_H_
#define LITE_PROC_MANAGER_SERVICE_ENUMERATION_HELPER_H_

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace lite_proc_manager {

struct ServiceStatusRecord {
  std::wstring service_name;
  std::wstring display_name;
  DWORD state{0};
  DWORD pid{0};
};

// Returns ERROR_SUCCESS or a Win32 error code. The callback has the same
// output semantics as EnumServicesStatusExW for buffer sizing and resumption.
using EnumServiceStatusPageCallback = std::function<DWORD(
    BYTE* buffer, DWORD buffer_size, DWORD* bytes_needed,
    DWORD* services_returned, DWORD* resume_handle)>;

bool EnumerateServiceStatusRecords(
    const EnumServiceStatusPageCallback& enumerate_page,
    const std::atomic_bool* cancellation,
    std::vector<ServiceStatusRecord>* records);

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_SERVICE_ENUMERATION_HELPER_H_
