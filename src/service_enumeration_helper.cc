// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "service_enumeration_helper.h"

#include <winsvc.h>

#include <algorithm>
#include <utility>

namespace lite_proc_manager {

bool EnumerateServiceStatusRecords(
    const EnumServiceStatusPageCallback& enumerate_page,
    const std::atomic_bool* cancellation,
    std::vector<ServiceStatusRecord>* records) {
  if (!enumerate_page || !records) return false;
  records->clear();

  constexpr DWORD kInitialBufferSize = 64 * 1024;
  constexpr DWORD kMaximumBufferSize = 256 * 1024;
  constexpr size_t kMaximumPageCount = 4096;
  std::vector<BYTE> buffer(kInitialBufferSize);
  DWORD resume_handle = 0;

  for (size_t page = 0; page < kMaximumPageCount; ++page) {
    if (cancellation && cancellation->load(std::memory_order_relaxed)) {
      records->clear();
      return false;
    }

    DWORD bytes_needed = 0;
    DWORD services_returned = 0;
    DWORD resume_before_call = resume_handle;
    DWORD error = enumerate_page(
        buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_needed,
        &services_returned, &resume_handle);

    size_t maximum_returned =
        buffer.size() / sizeof(ENUM_SERVICE_STATUS_PROCESSW);
    if (services_returned > maximum_returned) {
      records->clear();
      return false;
    }

    auto* services =
        reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    for (DWORD index = 0; index < services_returned; ++index) {
      ServiceStatusRecord record;
      if (services[index].lpServiceName) {
        record.service_name = services[index].lpServiceName;
      }
      if (services[index].lpDisplayName) {
        record.display_name = services[index].lpDisplayName;
      }
      record.state = services[index].ServiceStatusProcess.dwCurrentState;
      record.pid = services[index].ServiceStatusProcess.dwProcessId;
      records->push_back(std::move(record));
    }

    if (error == ERROR_SUCCESS) {
      if (resume_handle == 0) return true;
      if (services_returned == 0 && resume_handle == resume_before_call) {
        records->clear();
        return false;
      }
      continue;
    }
    if (error != ERROR_MORE_DATA) {
      records->clear();
      return false;
    }

    if (services_returned > 0 && resume_handle == resume_before_call) {
      records->clear();
      return false;
    }
    if (services_returned == 0) {
      // Nothing was consumed, so retry the same page after growing the
      // buffer even if the API changed the output resume handle.
      resume_handle = resume_before_call;
      if (bytes_needed <= buffer.size()) {
        records->clear();
        return false;
      }
    }

    size_t doubled_size = std::min<size_t>(
        buffer.size() * 2, static_cast<size_t>(kMaximumBufferSize));
    size_t requested_size = std::min<size_t>(
        bytes_needed, static_cast<size_t>(kMaximumBufferSize));
    size_t next_size = std::max(doubled_size, requested_size);
    if (next_size > buffer.size()) {
      buffer.resize(next_size);
    } else if (services_returned == 0) {
      records->clear();
      return false;
    }
  }

  records->clear();
  return false;
}

}  // namespace lite_proc_manager
