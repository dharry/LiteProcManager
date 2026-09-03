// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_NATIVE_TYPES_H_
#define LITE_PROC_MANAGER_NATIVE_TYPES_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winternl.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <cstdint>

namespace lite_proc_manager {

// Priority classes enum matching Windows definition
enum class ProcessPriorityClass : uint32_t {
  kIdle = IDLE_PRIORITY_CLASS,
  kBelowNormal = BELOW_NORMAL_PRIORITY_CLASS,
  kNormal = NORMAL_PRIORITY_CLASS,
  kAboveNormal = ABOVE_NORMAL_PRIORITY_CLASS,
  kHigh = HIGH_PRIORITY_CLASS,
  kRealtime = REALTIME_PRIORITY_CLASS,
};

// NT Query System Information structures
constexpr ULONG kSystemProcessInformation = 5;
constexpr NTSTATUS kStatusInfoLengthMismatch = 0xC0000004L;
constexpr NTSTATUS kStatusSuccess = 0x00000000L;

#pragma pack(push, 8)
struct SYSTEM_PROCESS_INFORMATION_DETAILED {
  ULONG NextEntryOffset;
  ULONG NumberOfThreads;
  LARGE_INTEGER WorkingSetPrivateSize;
  ULONG HardFaultCount;
  ULONG NumberOfThreadsHighWatermark;
  ULONGLONG CycleTime;
  LARGE_INTEGER CreateTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER KernelTime;
  UNICODE_STRING ImageName;
  KPRIORITY BasePriority;
  HANDLE UniqueProcessId;
  HANDLE InheritedFromUniqueProcessId;
  ULONG HandleCount;
  ULONG SessionId;
  ULONG_PTR UniqueProcessKey;
  SIZE_T PeakVirtualSize;
  SIZE_T VirtualSize;
  ULONG PageFaultCount;
  SIZE_T PeakWorkingSetSize;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
  SIZE_T PrivatePageCount;
  LARGE_INTEGER ReadOperationCount;
  LARGE_INTEGER WriteOperationCount;
  LARGE_INTEGER OtherOperationCount;
  LARGE_INTEGER ReadTransferCount;
  LARGE_INTEGER WriteTransferCount;
  LARGE_INTEGER OtherTransferCount;
};
#pragma pack(pop)

typedef NTSTATUS(NTAPI* PfnNtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_NATIVE_TYPES_H_
