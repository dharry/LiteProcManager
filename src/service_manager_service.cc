// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "service_manager_service.h"

#include <winsvc.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <vector>

#include "language_manager.h"
#include "service_enumeration_helper.h"

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
  return LanguageManager::GetString(StringId::kSystemErrorCodePrefix) +
         std::to_wstring(error_code);
}

}  // namespace

std::vector<std::shared_ptr<ServiceItem>> ServiceManagerService::GetServicesSnapshot(
    const std::atomic_bool* cancellation) {
  std::vector<std::shared_ptr<ServiceItem>> result;
  if (cancellation && cancellation->load(std::memory_order_relaxed)) {
    return result;
  }

  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
  if (!scm) {
    return result;
  }

  std::vector<ServiceStatusRecord> service_statuses;
  bool enumeration_succeeded = EnumerateServiceStatusRecords(
      [scm](BYTE* buffer, DWORD buffer_size, DWORD* bytes_needed,
            DWORD* services_returned, DWORD* resume_handle) {
        if (EnumServicesStatusExW(
                scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                buffer, buffer_size, bytes_needed, services_returned,
                resume_handle, nullptr)) {
          return static_cast<DWORD>(ERROR_SUCCESS);
        }
        return GetLastError();
      },
      cancellation, &service_statuses);
  if (!enumeration_succeeded) {
    CloseServiceHandle(scm);
    return result;
  }

  result.reserve(service_statuses.size());
  std::unordered_set<std::wstring> active_service_names;
  active_service_names.reserve(service_statuses.size());
  bool cancelled = false;

  constexpr auto kConfigCacheLifetime = std::chrono::minutes(5);
  auto now = std::chrono::steady_clock::now();

  for (const auto& service_status : service_statuses) {
    if (cancellation && cancellation->load(std::memory_order_relaxed)) {
      cancelled = true;
      break;
    }

    auto item = std::make_shared<ServiceItem>();
    item->service_name = service_status.service_name;
    item->display_name = service_status.display_name;
    item->state = service_status.state;
    item->pid = service_status.pid;
    active_service_names.insert(item->service_name);

    CachedServiceConfig config;
    bool has_cached_config = false;
    uint64_t cache_revision = 0;
    {
      std::lock_guard<std::mutex> lock(config_cache_mutex_);
      cache_revision = config_cache_revision_;
      auto cached = config_cache_.find(item->service_name);
      if (cached != config_cache_.end() &&
          now - cached->second.refreshed_at < kConfigCacheLifetime) {
        config = cached->second;
        has_cached_config = true;
      }
    }

    if (!has_cached_config) {
      config.refreshed_at = now;

      // Static configuration is queried only on a cache miss or after expiry.
      SC_HANDLE svc = OpenServiceW(
          scm, service_status.service_name.c_str(), SERVICE_QUERY_CONFIG);
      if (svc) {
        DWORD cfg_bytes_needed = 0;
        if (!QueryServiceConfigW(svc, nullptr, 0, &cfg_bytes_needed) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            cfg_bytes_needed >= sizeof(QUERY_SERVICE_CONFIGW)) {
          std::vector<BYTE> cfg_buf(cfg_bytes_needed);
          auto* qsc = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(cfg_buf.data());
          if (QueryServiceConfigW(svc, qsc, cfg_bytes_needed, &cfg_bytes_needed)) {
            config.start_type = qsc->dwStartType;
            if (qsc->lpServiceStartName) {
              config.account_name = qsc->lpServiceStartName;
            }
          }
        }

        SERVICE_DESCRIPTIONW initial_desc{};
        DWORD desc_bytes = 0;
        if (QueryServiceConfig2W(
                svc, SERVICE_CONFIG_DESCRIPTION,
                reinterpret_cast<LPBYTE>(&initial_desc),
                static_cast<DWORD>(sizeof(initial_desc)), &desc_bytes)) {
          if (initial_desc.lpDescription) {
            config.description = initial_desc.lpDescription;
          }
        } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
                   desc_bytes >= sizeof(SERVICE_DESCRIPTIONW)) {
          std::vector<BYTE> desc_buf(desc_bytes);
          if (QueryServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, desc_buf.data(),
                                   desc_bytes, &desc_bytes)) {
            auto* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(desc_buf.data());
            if (desc && desc->lpDescription) {
              config.description = desc->lpDescription;
            }
          }
        }

        CloseServiceHandle(svc);
      }

      std::lock_guard<std::mutex> lock(config_cache_mutex_);
      if (cache_revision == config_cache_revision_) {
        config_cache_[item->service_name] = config;
      }
    }

    item->start_type = config.start_type;
    item->account_name = config.account_name;
    item->description = config.description;
    result.push_back(std::move(item));
  }

  CloseServiceHandle(scm);

  if (!cancelled) {
    std::lock_guard<std::mutex> lock(config_cache_mutex_);
    for (auto cached = config_cache_.begin(); cached != config_cache_.end();) {
      if (active_service_names.find(cached->first) == active_service_names.end()) {
        cached = config_cache_.erase(cached);
      } else {
        ++cached;
      }
    }
  }

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

bool ServiceManagerService::ExecuteServiceRestart(
    const RestartCallbacks& callbacks, std::wstring* error_msg) {
  if (!callbacks.stop_service || !callbacks.query_state || !callbacks.start_service) {
    if (error_msg) *error_msg = GetSystemErrorMessage(ERROR_INVALID_PARAMETER);
    return false;
  }

  // 1. Request service stop and verify result
  DWORD stop_err = callbacks.stop_service();
  if (stop_err != ERROR_SUCCESS && stop_err != ERROR_SERVICE_NOT_ACTIVE) {
    if (error_msg) *error_msg = GetSystemErrorMessage(stop_err);
    return false;
  }

  // 2. Wait until service reaches SERVICE_STOPPED
  bool stopped = (stop_err == ERROR_SERVICE_NOT_ACTIVE);
  if (!stopped) {
    int iterations = (std::max)(1, callbacks.max_wait_iterations);
    for (int i = 0; i < iterations; ++i) {
      DWORD current_state = 0;
      if (callbacks.query_state(&current_state) == ERROR_SUCCESS &&
          current_state == SERVICE_STOPPED) {
        stopped = true;
        break;
      }
      if (i + 1 < iterations && callbacks.sleep) {
        callbacks.sleep(callbacks.poll_interval_ms);
      }
    }
  }

  if (!stopped) {
    if (error_msg) *error_msg = GetSystemErrorMessage(ERROR_SERVICE_REQUEST_TIMEOUT);
    return false;
  }

  // 3. Start service only after confirming it has stopped
  DWORD start_err = callbacks.start_service();
  if (start_err != ERROR_SUCCESS) {
    if (error_msg) *error_msg = GetSystemErrorMessage(start_err);
    return false;
  }

  return true;
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

  RestartCallbacks callbacks;
  callbacks.stop_service = [svc]() -> DWORD {
    SERVICE_STATUS status = {0};
    return ControlService(svc, SERVICE_CONTROL_STOP, &status)
               ? ERROR_SUCCESS
               : GetLastError();
  };
  callbacks.query_state = [svc](DWORD* current_state) -> DWORD {
    SERVICE_STATUS_PROCESS ssp = {0};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp),
                              &bytes)) {
      return GetLastError();
    }
    if (current_state) *current_state = ssp.dwCurrentState;
    return ERROR_SUCCESS;
  };
  callbacks.start_service = [svc]() -> DWORD {
    return StartServiceW(svc, 0, nullptr) ? ERROR_SUCCESS : GetLastError();
  };
  callbacks.sleep = [](DWORD milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  };

  bool success = ExecuteServiceRestart(callbacks, error_msg);

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
    std::lock_guard<std::mutex> lock(config_cache_mutex_);
    ++config_cache_revision_;
    config_cache_.erase(service_name);
  } else {
    if (error_msg) *error_msg = GetSystemErrorMessage(GetLastError());
  }

  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return success;
}

}  // namespace lite_proc_manager
