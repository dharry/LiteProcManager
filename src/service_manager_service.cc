// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "service_manager_service.h"

#include <winsvc.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace lite_proc_manager {

namespace {

std::wstring GetSystemErrorMessage(DWORD error_code) {
  wchar_t* msg_buf = nullptr;
  DWORD len = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&msg_buf), 0, nullptr);
  if (len > 0 && msg_buf) {
    std::wstring msg(msg_buf, len);
    LocalFree(msg_buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) {
      msg.pop_back();
    }
    return msg;
  }
  return L"Error code: " + std::to_wstring(error_code);
}

}  // namespace

std::vector<std::shared_ptr<ServiceItem>> ServiceManagerService::GetServicesSnapshot() {
  std::vector<std::shared_ptr<ServiceItem>> result;

  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
  if (!scm) {
    return result;
  }

  DWORD bytes_needed = 0;
  DWORD services_returned = 0;
  DWORD resume_handle = 0;

  // Determine required buffer size
  EnumServicesStatusExW(
      scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
      nullptr, 0, &bytes_needed, &services_returned, &resume_handle, nullptr);

  if (bytes_needed == 0) {
    CloseServiceHandle(scm);
    return result;
  }

  std::vector<BYTE> buffer(bytes_needed);
  if (!EnumServicesStatusExW(
          scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
          buffer.data(), bytes_needed, &bytes_needed, &services_returned,
          &resume_handle, nullptr)) {
    CloseServiceHandle(scm);
    return result;
  }

  auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
  result.reserve(services_returned);

  for (DWORD i = 0; i < services_returned; ++i) {
    auto item = std::make_shared<ServiceItem>();
    item->service_name = services[i].lpServiceName ? services[i].lpServiceName : L"";
    item->display_name = services[i].lpDisplayName ? services[i].lpDisplayName : L"";
    item->state = services[i].ServiceStatusProcess.dwCurrentState;
    item->pid = services[i].ServiceStatusProcess.dwProcessId;

    // Open each service to query startup type, account, and description
    SC_HANDLE svc = OpenServiceW(scm, services[i].lpServiceName, SERVICE_QUERY_CONFIG);
    if (svc) {
      DWORD cfg_bytes_needed = 0;
      QueryServiceConfigW(svc, nullptr, 0, &cfg_bytes_needed);
      if (cfg_bytes_needed > 0) {
        std::vector<BYTE> cfg_buf(cfg_bytes_needed);
        auto* qsc = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(cfg_buf.data());
        if (QueryServiceConfigW(svc, qsc, cfg_bytes_needed, &cfg_bytes_needed)) {
          item->start_type = qsc->dwStartType;
          if (qsc->lpServiceStartName) {
            item->account_name = qsc->lpServiceStartName;
          }
        }
      }

      // Query Description
      DWORD desc_bytes = 0;
      QueryServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &desc_bytes);
      if (desc_bytes > 0) {
        std::vector<BYTE> desc_buf(desc_bytes);
        if (QueryServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, desc_buf.data(), desc_bytes, &desc_bytes)) {
          auto* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(desc_buf.data());
          if (desc && desc->lpDescription) {
            item->description = desc->lpDescription;
          }
        }
      }

      CloseServiceHandle(svc);
    }

    result.push_back(std::move(item));
  }

  CloseServiceHandle(scm);
  return result;
}

bool ServiceManagerService::StartServiceByName(const std::wstring& service_name, std::wstring* error_msg) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    return false;
  }

  SC_HANDLE svc = OpenServiceW(scm, service_name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
  if (!svc) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  bool success = false;
  if (StartServiceW(svc, 0, nullptr)) {
    success = true;
  } else {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_ALREADY_RUNNING) {
      success = true;
    } else {
      if (error_msg) *error_msg = GetSystemErrorMessage(err);
    }
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return success;
}

bool ServiceManagerService::StopServiceByName(const std::wstring& service_name, std::wstring* error_msg) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    return false;
  }

  SC_HANDLE svc = OpenServiceW(scm, service_name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!svc) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  SERVICE_STATUS status = {0};
  bool success = false;
  if (ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
    success = true;
  } else {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_NOT_ACTIVE) {
      success = true;
    } else {
      if (error_msg) *error_msg = GetSystemErrorMessage(err);
    }
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return success;
}

bool ServiceManagerService::RestartServiceByName(const std::wstring& service_name, std::wstring* error_msg) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    return false;
  }

  SC_HANDLE svc = OpenServiceW(scm, service_name.c_str(), SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
  if (!svc) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  // 1. Stop service
  SERVICE_STATUS status = {0};
  ControlService(svc, SERVICE_CONTROL_STOP, &status);

  // Wait for service to stop (up to 5 seconds)
  for (int i = 0; i < 50; ++i) {
    SERVICE_STATUS_PROCESS ssp = {0};
    DWORD bytes = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytes)) {
      if (ssp.dwCurrentState == SERVICE_STOPPED) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 2. Start service
  bool success = false;
  if (StartServiceW(svc, 0, nullptr)) {
    success = true;
  } else {
    DWORD err = GetLastError();
    if (err == ERROR_SERVICE_ALREADY_RUNNING) {
      success = true;
    } else {
      if (error_msg) *error_msg = GetSystemErrorMessage(err);
    }
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return success;
}

bool ServiceManagerService::ChangeStartupType(const std::wstring& service_name, DWORD new_start_type, std::wstring* error_msg) {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    return false;
  }

  SC_HANDLE svc = OpenServiceW(scm, service_name.c_str(), SERVICE_CHANGE_CONFIG);
  if (!svc) {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
    CloseServiceHandle(scm);
    return false;
  }

  bool success = false;
  if (ChangeServiceConfigW(
          svc,
          SERVICE_NO_CHANGE,
          new_start_type,
          SERVICE_NO_CHANGE,
          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
    success = true;
  } else {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return success;
}

}  // namespace lite_proc_manager
