// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "process_snapshot_service.h"

#include <windows.h>
#include <winternl.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")

namespace lite_proc_manager {

namespace {
uint64_t FileTimeToUint64(const FILETIME& ft) {
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return uli.QuadPart;
}

using PfnGetPackageFullName = LONG(WINAPI*)(HANDLE hProcess, UINT32* packageFullNameLength, PWSTR packageFullName);

PfnGetPackageFullName pfn_get_package_full_name = nullptr;
PfnNtQueryInformationProcess pfn_nt_query_information_process = nullptr;

void InitOptionalApis() {
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (kernel32) {
    pfn_get_package_full_name = reinterpret_cast<PfnGetPackageFullName>(
        GetProcAddress(kernel32, "GetPackageFullName"));
  }

  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll) {
    pfn_nt_query_information_process = reinterpret_cast<PfnNtQueryInformationProcess>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
  }
}

bool AreAllThreadsSuspended(const SYSTEM_PROCESS_INFORMATION_DETAILED* entry,
                            size_t available_bytes) {
  if (!entry || entry->NumberOfThreads == 0) {
    return false;
  }

  const size_t entry_limit =
      (entry->NextEntryOffset > 0 && entry->NextEntryOffset < available_bytes)
          ? static_cast<size_t>(entry->NextEntryOffset)
          : available_bytes;
  const size_t threads_offset = sizeof(SYSTEM_PROCESS_INFORMATION_DETAILED);
  const size_t required_bytes =
      threads_offset +
      static_cast<size_t>(entry->NumberOfThreads) *
          sizeof(SYSTEM_THREAD_INFORMATION_DETAILED);
  if (required_bytes < threads_offset || required_bytes > entry_limit) {
    return false;
  }

  const auto* threads = reinterpret_cast<const SYSTEM_THREAD_INFORMATION_DETAILED*>(
      reinterpret_cast<const uint8_t*>(entry) + threads_offset);
  for (ULONG i = 0; i < entry->NumberOfThreads; ++i) {
    if (threads[i].ThreadState != kThreadStateWaiting ||
        threads[i].WaitReason != kWaitReasonSuspended) {
      return false;
    }
  }
  return true;
}

void EnableDebugPrivilege() {
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
    LUID luid;
    if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
      TOKEN_PRIVILEGES tp;
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Luid = luid;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr);
    }
    CloseHandle(token);
  }
}

bool IsValidParentRelationship(const ProcessItem& parent, const ProcessItem& child) {
  if (parent.process_id == child.process_id ||
      !parent.start_time.has_value() || !child.start_time.has_value()) {
    return false;
  }

  return CompareFileTime(&parent.start_time.value(), &child.start_time.value()) < 0;
}

bool IsSameProcess(const ProcessItem& first, const ProcessItem& second) {
  return first.process_id == second.process_id &&
         first.start_time.has_value() && second.start_time.has_value() &&
         CompareFileTime(&first.start_time.value(), &second.start_time.value()) == 0;
}

bool ParentChainContains(
    uint32_t child_pid,
    uint32_t parent_pid,
    const std::unordered_map<uint32_t, std::shared_ptr<ProcessItem>>& item_map) {
  std::unordered_set<uint32_t> visited;
  uint32_t current_pid = parent_pid;
  while (current_pid != 0) {
    if (current_pid == child_pid || !visited.insert(current_pid).second) {
      return true;
    }

    auto current = item_map.find(current_pid);
    if (current == item_map.end() || !current->second) {
      return false;
    }
    current_pid = current->second->parent_process_id;
  }
  return false;
}

HANDLE OpenVerifiedProcess(const ProcessItem& process, DWORD desired_access) {
  if (process.process_id == 0 || !process.start_time.has_value()) {
    return nullptr;
  }

  HANDLE process_handle = OpenProcess(
      desired_access | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.process_id);
  if (!process_handle) {
    return nullptr;
  }

  FILETIME creation_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  if (!GetProcessTimes(process_handle, &creation_time, &exit_time,
                       &kernel_time, &user_time) ||
      CompareFileTime(&creation_time, &process.start_time.value()) != 0) {
    CloseHandle(process_handle);
    return nullptr;
  }

  return process_handle;
}
}  // namespace

ProcessSnapshotService::ProcessSnapshotService() {
  EnableDebugPrivilege();
  InitOptionalApis();

  ntdll_module_ = GetModuleHandleW(L"ntdll.dll");
  if (ntdll_module_) {
    pfn_nt_query_system_information_ = reinterpret_cast<PfnNtQuerySystemInformation>(
        GetProcAddress(ntdll_module_, "NtQuerySystemInformation"));
  }

  HMODULE shcore = GetModuleHandleW(L"shcore.dll");
  if (!shcore) {
    loaded_shcore_module_ = LoadLibraryExW(
        L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    shcore = loaded_shcore_module_;
  }
  if (shcore) {
    pfn_get_process_dpi_awareness_ = reinterpret_cast<PfnGetProcessDpiAwareness>(
        GetProcAddress(shcore, "GetProcessDpiAwareness"));
  }

  buffer_ = VirtualAlloc(nullptr, buffer_size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  processor_count_ = static_cast<int>(si.dwNumberOfProcessors);
  if (processor_count_ < 1) processor_count_ = 1;

  // Initialize System Times baseline
  FILETIME idle_ft, kernel_ft, user_ft;
  if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
    prev_sys_idle_time_ = FileTimeToUint64(idle_ft);
    prev_sys_kernel_time_ = FileTimeToUint64(kernel_ft);
    prev_sys_user_time_ = FileTimeToUint64(user_ft);
    has_prev_sys_times_ = true;
  }
}

ProcessSnapshotService::~ProcessSnapshotService() {
  if (buffer_) {
    VirtualFree(buffer_, 0, MEM_RELEASE);
    buffer_ = nullptr;
  }
  pfn_get_process_dpi_awareness_ = nullptr;
  if (loaded_shcore_module_) {
    FreeLibrary(loaded_shcore_module_);
    loaded_shcore_module_ = nullptr;
  }
}

SnapshotResult ProcessSnapshotService::GetSnapshot(const ProcessSnapshotOptions& options) {
  SnapshotResult result;

  // 1. Calculate high-precision System Total CPU time across all cores
  FILETIME idle_ft, kernel_ft, user_ft;
  uint64_t delta_sys_total = 0;
  uint64_t delta_sys_idle = 0;

  if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
    uint64_t sys_idle = FileTimeToUint64(idle_ft);
    uint64_t sys_kernel = FileTimeToUint64(kernel_ft);
    uint64_t sys_user = FileTimeToUint64(user_ft);

    if (has_prev_sys_times_) {
      uint64_t delta_k = (sys_kernel >= prev_sys_kernel_time_) ? (sys_kernel - prev_sys_kernel_time_) : 0;
      uint64_t delta_u = (sys_user >= prev_sys_user_time_) ? (sys_user - prev_sys_user_time_) : 0;
      delta_sys_idle = (sys_idle >= prev_sys_idle_time_) ? (sys_idle - prev_sys_idle_time_) : 0;

      // In Windows, sys_kernel already includes idle time.
      // Therefore, (delta_k + delta_u) is the exact total CPU time across all cores.
      delta_sys_total = delta_k + delta_u;
    }

    prev_sys_idle_time_ = sys_idle;
    prev_sys_kernel_time_ = sys_kernel;
    prev_sys_user_time_ = sys_user;
    has_prev_sys_times_ = true;
  }

  if (!pfn_nt_query_system_information_ || !buffer_) {
    return result;
  }

  ULONG return_length = 0;
  NTSTATUS status = pfn_nt_query_system_information_(
      kSystemProcessInformation, buffer_, buffer_size_, &return_length);

  while (status == static_cast<NTSTATUS>(kStatusInfoLengthMismatch)) {
    buffer_size_ = (std::max<uint32_t>)(static_cast<uint32_t>(return_length) + 64 * 1024, buffer_size_ * 2);
    if (buffer_) VirtualFree(buffer_, 0, MEM_RELEASE);
    buffer_ = VirtualAlloc(nullptr, buffer_size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer_) return result;

    status = pfn_nt_query_system_information_(
        kSystemProcessInformation, buffer_, buffer_size_, &return_length);
  }

  if (status != static_cast<NTSTATUS>(kStatusSuccess)) {
    return result;
  }

  uintptr_t current_offset = 0;
  std::unordered_map<uint32_t, ProcessTimeTrack> current_times;

  while (true) {
    auto* entry = reinterpret_cast<SYSTEM_PROCESS_INFORMATION_DETAILED*>(
        reinterpret_cast<uint8_t*>(buffer_) + current_offset);

    uint32_t pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry->UniqueProcessId));
    uint32_t parent_pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry->InheritedFromUniqueProcessId));

    auto item = std::make_shared<ProcessItem>();
    item->process_id = pid;
    item->parent_process_id = parent_pid;

    const size_t remaining_bytes =
        (current_offset < buffer_size_) ? (buffer_size_ - current_offset) : 0;
    if (pid == 0) {
      item->name = L"System Idle Process";
      item->status = ProcessExecutionStatus::kRunning;
    } else if (entry->ImageName.Length > 0 && entry->ImageName.Buffer != nullptr) {
      item->name = std::wstring(entry->ImageName.Buffer, entry->ImageName.Length / sizeof(wchar_t));
      item->status = (pid != 4 && AreAllThreadsSuspended(entry, remaining_bytes))
                         ? ProcessExecutionStatus::kSuspended
                         : ProcessExecutionStatus::kRunning;
    } else {
      item->name = L"System";
      item->status = ProcessExecutionStatus::kRunning;
    }

    item->thread_count = entry->NumberOfThreads;
    item->handle_count = entry->HandleCount;
    item->session_id = entry->SessionId;
    item->base_priority_raw = entry->BasePriority;

    // Memory metrics
    item->working_set = static_cast<uint64_t>(entry->WorkingSetSize);
    item->private_working_set = static_cast<uint64_t>(entry->WorkingSetPrivateSize.QuadPart > 0
                                                          ? entry->WorkingSetPrivateSize.QuadPart
                                                          : entry->PrivatePageCount);
    item->peak_working_set = static_cast<uint64_t>(entry->PeakWorkingSetSize);
    item->commit_size = entry->PagefileUsage > 0 ? static_cast<uint64_t>(entry->PagefileUsage)
                                                 : static_cast<uint64_t>(entry->PrivatePageCount);
    item->paged_pool = static_cast<uint64_t>(entry->QuotaPagedPoolUsage);
    item->non_paged_pool = static_cast<uint64_t>(entry->QuotaNonPagedPoolUsage);

    // I/O metrics
    item->io_read_count = static_cast<uint64_t>(entry->ReadOperationCount.QuadPart);
    item->io_write_count = static_cast<uint64_t>(entry->WriteOperationCount.QuadPart);
    item->io_other_count = static_cast<uint64_t>(entry->OtherOperationCount.QuadPart);
    item->io_read_bytes = static_cast<uint64_t>(entry->ReadTransferCount.QuadPart);
    item->io_write_bytes = static_cast<uint64_t>(entry->WriteTransferCount.QuadPart);
    item->io_other_bytes = static_cast<uint64_t>(entry->OtherTransferCount.QuadPart);

    if (entry->CreateTime.QuadPart > 0) {
      FILETIME ft;
      ft.dwLowDateTime = entry->CreateTime.LowPart;
      ft.dwHighDateTime = entry->CreateTime.HighPart;
      item->start_time = ft;
    }

    int64_t raw_kernel_time = entry->KernelTime.QuadPart;
    int64_t raw_user_time = entry->UserTime.QuadPart;
    uint64_t raw_cycle_time = entry->CycleTime;

    ProcessTimeTrack current_track{
        item->start_time, raw_kernel_time, raw_user_time, raw_cycle_time,
        item->working_set};
    current_times[pid] = current_track;

    // Calculate CPU usage accurately against total system time (verifying start_time to prevent PID reuse artifacts)
    auto it_prev = previous_times_.find(pid);
    ApplyProcessMetricsDelta(
        (it_prev != previous_times_.end()) ? &it_prev->second : nullptr,
        current_track, delta_sys_total, item.get());

    EnrichProcessDetails(item.get(), options);

    result.totals.total_threads += item->thread_count;
    result.totals.total_handles += item->handle_count;
    result.items.push_back(item);

    if (entry->NextEntryOffset == 0) break;
    current_offset += entry->NextEntryOffset;
  }

  // Cleanup static cache for terminated processes
  std::unordered_set<uint32_t> alive_pids;
  for (const auto& item : result.items) {
    alive_pids.insert(item->process_id);
  }

  for (auto it = static_cache_.begin(); it != static_cache_.end();) {
    if (alive_pids.find(it->first) == alive_pids.end()) {
      it = static_cache_.erase(it);
    } else {
      ++it;
    }
  }

  previous_times_ = std::move(current_times);

  result.totals.total_processes = static_cast<int>(result.items.size());

  // High-precision Total System CPU Usage (100% - Idle%)
  if (delta_sys_total > 0) {
    double total_cpu = (static_cast<double>(delta_sys_total - delta_sys_idle) / static_cast<double>(delta_sys_total)) * 100.0;
    result.totals.total_cpu_usage = std::clamp(total_cpu, 0.0, 100.0);
  } else {
    result.totals.total_cpu_usage = 0.0;
  }

  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&mem_status)) {
    result.totals.total_physical_memory = mem_status.ullTotalPhys;
    result.totals.used_physical_memory = mem_status.ullTotalPhys - mem_status.ullAvailPhys;
    result.totals.total_commit_limit = mem_status.ullTotalPageFile;
    result.totals.total_committed = mem_status.ullTotalPageFile - mem_status.ullAvailPageFile;
  }

  return result;
}

void ProcessSnapshotService::EnrichProcessDetails(
    ProcessItem* item, const ProcessSnapshotOptions& options) {
  if (item->process_id == 0 || item->process_id == 4) {
    item->user_name = L"NT AUTHORITY\\SYSTEM";
    item->architecture = L"x64";
    item->platform = ProcessBitness::k64Bit;
    item->os_context = ProcessBitness::k64Bit;
    item->elevated = true;
    item->uac_virtualization = ProcessPolicyStatus::kNotApplicable;
    item->dep_status = ProcessPolicyStatus::kEnabledPermanent;
    item->description = item->process_id == 0 ? L"System Idle Process" : L"NT Kernel & System";
    return;
  }

  auto cache_it = static_cache_.find(item->process_id);
  if (cache_it != static_cache_.end()) {
    bool same_start_time = false;
    if (item->start_time.has_value() && cache_it->second.start_time.has_value()) {
      same_start_time = (item->start_time->dwLowDateTime == cache_it->second.start_time->dwLowDateTime &&
                         item->start_time->dwHighDateTime == cache_it->second.start_time->dwHighDateTime);
    } else if (!item->start_time.has_value() && !cache_it->second.start_time.has_value()) {
      same_start_time = true;
    }

    if (same_start_time) {
      item->file_path = cache_it->second.file_path;
      item->command_line = cache_it->second.command_line;
      item->description = cache_it->second.description;
      item->user_name = cache_it->second.user_name;
      item->architecture = cache_it->second.architecture;
      item->platform = cache_it->second.platform;
      item->os_context = cache_it->second.os_context;
      item->elevated = cache_it->second.elevated;
      item->uac_virtualization = cache_it->second.uac_virtualization;
      item->dep_status = cache_it->second.dep_status;
      item->enterprise_context = cache_it->second.enterprise_context;
      item->dpi_awareness = cache_it->second.dpi_awareness;
      item->package_name = cache_it->second.package_name;
      item->priority = cache_it->second.priority;

      if (options.query_priority || options.query_user_objects ||
          options.query_gdi_objects) {
        HANDLE h_quick = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item->process_id);
        if (h_quick) {
          if (options.query_priority) {
            DWORD priority_class = GetPriorityClass(h_quick);
            if (priority_class != 0) {
              item->priority = static_cast<ProcessPriorityClass>(priority_class);
              cache_it->second.priority = item->priority;
            }
          }
          if (options.query_user_objects) {
            item->user_objects = GetGuiResources(h_quick, GR_USEROBJECTS);
          }
          if (options.query_gdi_objects) {
            item->gdi_objects = GetGuiResources(h_quick, GR_GDIOBJECTS);
          }
          CloseHandle(h_quick);
        }
      }
      return;
    }
  }

  ProcessStaticCache new_cache;
  new_cache.start_time = item->start_time;

  HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item->process_id);
  if (process_handle != nullptr) {
    // 1. File Path & Command Line & DPI Awareness
    wchar_t path_buffer[MAX_PATH * 2] = {0};
    DWORD path_size = static_cast<DWORD>(std::size(path_buffer));
    if (QueryFullProcessImageNameW(process_handle, 0, path_buffer, &path_size)) {
      new_cache.file_path = path_buffer;
    }
    new_cache.command_line = QueryProcessCommandLine(process_handle);
    new_cache.dpi_awareness = QueryProcessDpiAwareness(process_handle);

    // 2. Priority
    if (options.query_priority) {
      DWORD priority_class = GetPriorityClass(process_handle);
      if (priority_class != 0) {
        new_cache.priority = static_cast<ProcessPriorityClass>(priority_class);
      }
    }

    // 3. Architecture & Platform
    BOOL is_wow64 = FALSE;
    if (IsWow64Process(process_handle, &is_wow64)) {
      new_cache.architecture = is_wow64 ? L"x86" : L"x64";
      new_cache.platform = is_wow64 ? ProcessBitness::k32Bit
                                    : ProcessBitness::k64Bit;
      new_cache.os_context = new_cache.platform;
    }

    // 4. GUI Objects (User & GDI)
    if (options.query_user_objects) {
      item->user_objects = GetGuiResources(process_handle, GR_USEROBJECTS);
    }
    if (options.query_gdi_objects) {
      item->gdi_objects = GetGuiResources(process_handle, GR_GDIOBJECTS);
    }

    // 5. Description from File Version Info
    if (!new_cache.file_path.empty()) {
      DWORD handle = 0;
      DWORD size = GetFileVersionInfoSizeW(new_cache.file_path.c_str(), &handle);
      if (size > 0) {
        std::vector<BYTE> version_data(size);
        if (GetFileVersionInfoW(new_cache.file_path.c_str(), 0, size, version_data.data())) {
          struct LANGANDCODEPAGE {
            WORD wLanguage;
            WORD wCodePage;
          } *lpTranslate;
          UINT cbTranslate = 0;
          if (VerQueryValueW(version_data.data(), L"\\VarFileInfo\\Translation",
                             reinterpret_cast<LPVOID*>(&lpTranslate), &cbTranslate) &&
              cbTranslate >= sizeof(LANGANDCODEPAGE)) {
            wchar_t sub_block[64];
            swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                       lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
            wchar_t* description = nullptr;
            UINT description_len = 0;
            if (VerQueryValueW(version_data.data(), sub_block,
                               reinterpret_cast<LPVOID*>(&description), &description_len) &&
                description != nullptr) {
              new_cache.description = description;
            }
          }
        }
      }
    }

    // 6. DEP Status
    DWORD dep_flags = 0;
    BOOL dep_perm = FALSE;
    if (GetProcessDEPPolicy(process_handle, &dep_flags, &dep_perm)) {
      if (dep_flags & PROCESS_DEP_ENABLE) {
        new_cache.dep_status = dep_perm
                                   ? ProcessPolicyStatus::kEnabledPermanent
                                   : ProcessPolicyStatus::kEnabled;
      } else {
        new_cache.dep_status = ProcessPolicyStatus::kDisabled;
      }
    }

    // 7. Package Name (UWP)
    if (pfn_get_package_full_name) {
      UINT32 pkg_len = 0;
      LONG pkg_res = pfn_get_package_full_name(process_handle, &pkg_len, nullptr);
      if (pkg_res == ERROR_INSUFFICIENT_BUFFER && pkg_len > 0) {
        std::vector<wchar_t> pkg_buf(pkg_len);
        if (pfn_get_package_full_name(process_handle, &pkg_len, pkg_buf.data()) == ERROR_SUCCESS) {
          new_cache.package_name = pkg_buf.data();
        }
      }
    }

    // 8. Elevation & UAC Virtualization & User Name
    HANDLE token_handle = nullptr;
    if (OpenProcessToken(process_handle, TOKEN_QUERY, &token_handle)) {
      // Elevation
      TOKEN_ELEVATION elevation = {0};
      DWORD ret_len = 0;
      if (GetTokenInformation(token_handle, TokenElevation, &elevation, sizeof(elevation), &ret_len)) {
        new_cache.elevated = elevation.TokenIsElevated != 0;
      }

      // UAC Virtualization
      DWORD virt_enabled = 0;
      if (GetTokenInformation(token_handle, TokenVirtualizationEnabled, &virt_enabled, sizeof(virt_enabled), &ret_len)) {
        new_cache.uac_virtualization =
            virt_enabled ? ProcessPolicyStatus::kEnabled
                         : ProcessPolicyStatus::kDisabled;
      } else {
        new_cache.uac_virtualization = ProcessPolicyStatus::kNotApplicable;
      }

      // User Name (leave as "-" if unavailable rather than misreporting SYSTEM)
      std::wstring queried_user = QueryProcessUser(token_handle);
      if (!queried_user.empty()) {
        new_cache.user_name = std::move(queried_user);
      }

      CloseHandle(token_handle);
    }

    CloseHandle(process_handle);
  }

  static_cache_[item->process_id] = new_cache;

  item->file_path = new_cache.file_path;
  item->command_line = new_cache.command_line;
  item->description = new_cache.description;
  item->user_name = new_cache.user_name;
  item->architecture = new_cache.architecture;
  item->platform = new_cache.platform;
  item->os_context = new_cache.os_context;
  item->elevated = new_cache.elevated;
  item->uac_virtualization = new_cache.uac_virtualization;
  item->dep_status = new_cache.dep_status;
  item->enterprise_context = new_cache.enterprise_context;
  item->dpi_awareness = new_cache.dpi_awareness;
  item->package_name = new_cache.package_name;
  item->priority = new_cache.priority;
}

std::wstring ProcessSnapshotService::QueryProcessUser(HANDLE token_handle) {
  if (!token_handle) {
    return L"-";
  }

  std::wstring result = L"-";
  DWORD return_length = 0;
  GetTokenInformation(token_handle, TokenUser, nullptr, 0, &return_length);

  if (return_length > 0) {
    std::vector<BYTE> token_user_buf(return_length);
    if (GetTokenInformation(token_handle, TokenUser, token_user_buf.data(), return_length, &return_length)) {
      auto* token_user = reinterpret_cast<TOKEN_USER*>(token_user_buf.data());
      wchar_t name_buf[256] = {0};
      wchar_t domain_buf[256] = {0};
      DWORD cch_name = static_cast<DWORD>(std::size(name_buf));
      DWORD cch_domain = static_cast<DWORD>(std::size(domain_buf));
      SID_NAME_USE sid_use;

      if (LookupAccountSidW(nullptr, token_user->User.Sid, name_buf, &cch_name,
                            domain_buf, &cch_domain, &sid_use)) {
        if (_wcsicmp(name_buf, L"SYSTEM") == 0) {
          result = L"SYSTEM";
        } else if (cch_domain > 0) {
          result = std::wstring(domain_buf) + L"\\" + name_buf;
        } else {
          result = name_buf;
        }
      }
    }
  }

  return result;
}

std::wstring ProcessSnapshotService::QueryProcessCommandLine(HANDLE process_handle) {
  if (!process_handle || !pfn_nt_query_information_process) {
    return L"";
  }

  ULONG return_length = 0;
  NTSTATUS status = pfn_nt_query_information_process(
      process_handle, kProcessCommandLineInformation, nullptr, 0, &return_length);
  if ((status != kStatusInfoLengthMismatch &&
       status != kStatusBufferTooSmall &&
       status != kStatusBufferOverflow) ||
      return_length < sizeof(UNICODE_STRING) || return_length > 64 * 1024) {
    return L"";
  }

  std::vector<BYTE> buffer(return_length);
  status = pfn_nt_query_information_process(
      process_handle, kProcessCommandLineInformation, buffer.data(),
      static_cast<ULONG>(buffer.size()), &return_length);
  if (status != kStatusSuccess) {
    return L"";
  }

  const auto* cmd = reinterpret_cast<const UNICODE_STRING*>(buffer.data());
  if (!cmd->Buffer || cmd->Length == 0) {
    return L"";
  }

  const auto* buf_begin = buffer.data();
  const auto* buf_end = buffer.data() + buffer.size();
  const auto* str_begin = reinterpret_cast<const BYTE*>(cmd->Buffer);
  if (str_begin < buf_begin || str_begin >= buf_end ||
      cmd->Length > static_cast<size_t>(buf_end - str_begin)) {
    return L"";
  }

  return std::wstring(cmd->Buffer, cmd->Length / sizeof(wchar_t));
}

std::wstring ProcessSnapshotService::QueryProcessDpiAwareness(HANDLE process_handle) const {
  if (!process_handle || !pfn_get_process_dpi_awareness_) {
    return L"-";
  }

  int awareness = -1;
  if (FAILED(pfn_get_process_dpi_awareness_(process_handle, &awareness))) {
    return L"-";
  }

  switch (awareness) {
    case 0:  // PROCESS_DPI_UNAWARE
      return L"Unaware";
    case 1:  // PROCESS_SYSTEM_DPI_AWARE
      return L"System";
    case 2:  // PROCESS_PER_MONITOR_DPI_AWARE
      return L"Per-Monitor";
    default:
      return L"-";
  }
}

void ProcessSnapshotService::ApplyProcessMetricsDelta(
    const ProcessTimeTrack* previous,
    const ProcessTimeTrack& current,
    uint64_t delta_sys_total,
    ProcessItem* item) {
  if (!item) return;

  item->cpu_percent = 0.0;
  item->working_set_delta = 0;
  if (!previous) return;

  // Reject delta calculation if PID was reused by a different process instance
  if (previous->start_time.has_value() != current.start_time.has_value()) {
    return;
  }
  if (previous->start_time.has_value() &&
      CompareFileTime(&previous->start_time.value(),
                      &current.start_time.value()) != 0) {
    return;
  }

  int64_t delta_kernel = current.kernel_time - previous->kernel_time;
  int64_t delta_user = current.user_time - previous->user_time;
  int64_t total_proc_delta = delta_kernel + delta_user;

  if (total_proc_delta > 0 && delta_sys_total > 0) {
    double pct = (static_cast<double>(total_proc_delta) /
                  static_cast<double>(delta_sys_total)) *
                 100.0;
    item->cpu_percent = std::clamp(pct, 0.0, 100.0);
  }

  item->working_set_delta = static_cast<int64_t>(current.working_set) -
                            static_cast<int64_t>(previous->working_set);
}

bool ProcessSnapshotService::TerminateProcess(const ProcessItem& process) {
  HANDLE process_handle = OpenVerifiedProcess(process, PROCESS_TERMINATE);
  if (!process_handle) return false;

  BOOL success = ::TerminateProcess(process_handle, 1);
  CloseHandle(process_handle);
  return success != FALSE;
}

bool ProcessSnapshotService::TerminateProcessTree(
    const ProcessItem& root_process,
    const std::vector<std::shared_ptr<ProcessItem>>& all_items) {
  std::unordered_map<uint32_t, std::shared_ptr<ProcessItem>> item_map;
  std::unordered_set<uint32_t> duplicate_pids;
  for (const auto& item : all_items) {
    if (!item) continue;
    auto insertion = item_map.emplace(item->process_id, item);
    if (!insertion.second) {
      duplicate_pids.insert(item->process_id);
    }
  }

  std::unordered_map<const ProcessItem*, std::vector<const ProcessItem*>> child_map;
  for (const auto& item : all_items) {
    if (!item || duplicate_pids.find(item->process_id) != duplicate_pids.end() ||
        duplicate_pids.find(item->parent_process_id) != duplicate_pids.end()) {
      continue;
    }

    auto parent = item_map.find(item->parent_process_id);
    if (parent != item_map.end() && parent->second &&
        IsValidParentRelationship(*parent->second, *item) &&
        !ParentChainContains(item->process_id, item->parent_process_id, item_map)) {
      child_map[parent->second.get()].push_back(item.get());
    }
  }

  const ProcessItem* traversal_root = &root_process;
  auto current_root = item_map.find(root_process.process_id);
  if (current_root != item_map.end() && current_root->second &&
      duplicate_pids.find(root_process.process_id) == duplicate_pids.end() &&
      IsSameProcess(root_process, *current_root->second)) {
    traversal_root = current_root->second.get();
  }

  std::vector<std::pair<const ProcessItem*, bool>> pending;
  pending.emplace_back(traversal_root, false);
  std::unordered_set<uint32_t> visited;
  bool success = true;

  while (!pending.empty()) {
    auto [process, children_queued] = pending.back();
    pending.pop_back();

    if (children_queued) {
      if (!TerminateProcess(*process)) {
        success = false;
      }
      continue;
    }

    if (!visited.insert(process->process_id).second) {
      continue;
    }

    pending.emplace_back(process, true);
    auto children = child_map.find(process);
    if (children != child_map.end()) {
      for (const ProcessItem* child : children->second) {
        pending.emplace_back(child, false);
      }
    }
  }

  return success;
}

bool ProcessSnapshotService::SetPriority(
    const ProcessItem& process, ProcessPriorityClass priority) {
  HANDLE process_handle = OpenVerifiedProcess(process, PROCESS_SET_INFORMATION);
  if (!process_handle) return false;

  BOOL success = SetPriorityClass(process_handle, static_cast<DWORD>(priority));
  CloseHandle(process_handle);
  return success != FALSE;
}

std::vector<std::shared_ptr<ProcessItem>> ProcessSnapshotService::BuildProcessTree(
    const std::vector<std::shared_ptr<ProcessItem>>& flat_items) {
  std::unordered_map<uint32_t, std::shared_ptr<ProcessItem>> item_map;
  std::unordered_set<uint32_t> duplicate_pids;
  for (const auto& item : flat_items) {
    if (!item) continue;
    item->children.clear();
    auto insertion = item_map.emplace(item->process_id, item);
    if (!insertion.second) {
      duplicate_pids.insert(item->process_id);
    }
  }

  std::vector<std::shared_ptr<ProcessItem>> roots;
  for (const auto& item : flat_items) {
    if (!item) continue;
    if (item->parent_process_id != 0) {
      auto it = item_map.find(item->parent_process_id);
      if (duplicate_pids.find(item->process_id) == duplicate_pids.end() &&
          duplicate_pids.find(item->parent_process_id) == duplicate_pids.end() &&
          it != item_map.end() && it->second != item &&
          IsValidParentRelationship(*it->second, *item) &&
          !ParentChainContains(item->process_id, item->parent_process_id, item_map)) {
        it->second->children.push_back(item);
        continue;
      }
    }
    roots.push_back(item);
  }

  return roots;
}

}  // namespace lite_proc_manager
