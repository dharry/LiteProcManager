// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "service_item.h"

#include <winsvc.h>
#include "language_manager.h"

namespace lite_proc_manager {

std::wstring ServiceItem::GetStateString() const {
  switch (state) {
    case SERVICE_RUNNING:
      return LanguageManager::GetString(StringId::kServiceStateRunning);
    case SERVICE_STOPPED:
      return LanguageManager::GetString(StringId::kServiceStateStopped);
    case SERVICE_START_PENDING:
      return LanguageManager::GetString(StringId::kServiceStateStartPending);
    case SERVICE_STOP_PENDING:
      return LanguageManager::GetString(StringId::kServiceStateStopPending);
    case SERVICE_PAUSED:
      return LanguageManager::GetString(StringId::kServiceStatePaused);
    case SERVICE_PAUSE_PENDING:
      return LanguageManager::GetString(StringId::kServiceStatePausePending);
    case SERVICE_CONTINUE_PENDING:
      return LanguageManager::GetString(StringId::kServiceStateContinuePending);
    default:
      return LanguageManager::GetString(StringId::kServiceStateUnknown);
  }
}

std::wstring ServiceItem::GetStartTypeString() const {
  switch (start_type) {
    case SERVICE_AUTO_START:
      return LanguageManager::GetString(StringId::kServiceStartTypeAuto);
    case SERVICE_DEMAND_START:
      return LanguageManager::GetString(StringId::kServiceStartTypeManual);
    case SERVICE_DISABLED:
      return LanguageManager::GetString(StringId::kServiceStartTypeDisabled);
    case SERVICE_BOOT_START:
      return LanguageManager::GetString(StringId::kServiceStartTypeBoot);
    case SERVICE_SYSTEM_START:
      return LanguageManager::GetString(StringId::kServiceStartTypeSystem);
    default:
      return LanguageManager::GetString(StringId::kServiceStartTypeUnknown);
  }
}

}  // namespace lite_proc_manager
