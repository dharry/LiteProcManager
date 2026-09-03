// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "language_manager.h"

#include <windows.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "resource.h"

namespace lite_proc_manager {

namespace {

AppLanguage g_configured_lang = AppLanguage::kAuto;
bool g_active_is_japanese = true;
LANGID g_active_langid = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);

std::mutex g_cache_mutex;
std::unordered_map<UINT, std::wstring> g_string_cache;

HMODULE GetResourceModule() {
  HMODULE hModule = nullptr;
  GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&GetResourceModule), &hModule);
  return hModule ? hModule : GetModuleHandleW(nullptr);
}

// Load string directly from specified LANGID resource in STRINGTABLE
bool LoadStringExactLang(HMODULE hModule, UINT uId, LANGID langId, std::wstring* out_str) {
  if (!hModule || !out_str) return false;

  // Resource ID is stored in blocks of 16 strings
  UINT blockId = (uId >> 4) + 1;
  UINT indexInBlock = uId & 0x0F;

  HRSRC hRes = FindResourceExW(hModule, RT_STRING, MAKEINTRESOURCEW(blockId), langId);
  if (!hRes) {
    // Try SUBLANG_NEUTRAL / generic language
    hRes = FindResourceExW(hModule, RT_STRING, MAKEINTRESOURCEW(blockId), MAKELANGID(PRIMARYLANGID(langId), SUBLANG_NEUTRAL));
  }
  if (!hRes) {
    // Try English as default fallback
    hRes = FindResourceExW(hModule, RT_STRING, MAKEINTRESOURCEW(blockId), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
  }
  if (!hRes) {
    // Try Japanese as final fallback
    hRes = FindResourceExW(hModule, RT_STRING, MAKEINTRESOURCEW(blockId), MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));
  }
  if (!hRes) return false;

  HGLOBAL hGlobal = LoadResource(hModule, hRes);
  if (!hGlobal) return false;

  const wchar_t* pStr = static_cast<const wchar_t*>(LockResource(hGlobal));
  if (!pStr) return false;

  // Walk through Pascal-style counted strings in the block
  for (UINT i = 0; i < indexInBlock; ++i) {
    WORD len = static_cast<WORD>(*pStr++);
    pStr += len;
  }

  WORD len = static_cast<WORD>(*pStr++);
  if (len > 0) {
    *out_str = std::wstring(pStr, len);
  } else {
    out_str->clear();
  }
  return true;
}

UINT StringIdToResourceId(StringId id) {
  switch (id) {
    case StringId::kAppTitle: return IDS_APP_TITLE;
    case StringId::kSearchPlaceholder: return IDS_SEARCH_PLACEHOLDER;
    case StringId::kLabelSearch: return IDS_LABEL_SEARCH;
    case StringId::kLabelInterval: return IDS_LABEL_INTERVAL;
    case StringId::kIntervalPause: return IDS_INTERVAL_PAUSE;
    case StringId::kStatusProcesses: return IDS_STATUS_PROCESSES;
    case StringId::kStatusThreads: return IDS_STATUS_THREADS;
    case StringId::kStatusHandles: return IDS_STATUS_HANDLES;
    case StringId::kStatusCpu: return IDS_STATUS_CPU;
    case StringId::kStatusMemory: return IDS_STATUS_MEMORY;
    case StringId::kStatusCommit: return IDS_STATUS_COMMIT;

    // Menus
    case StringId::kMenuFile: return IDS_MENU_FILE;
    case StringId::kMenuView: return IDS_MENU_VIEW;
    case StringId::kMenuOptions: return IDS_MENU_OPTIONS;
    case StringId::kMenuMonitorRules: return IDS_MENU_MONITOR_RULES;
    case StringId::kMenuSelectColumns: return IDS_MENU_SELECT_COLUMNS;
    case StringId::kMenuAlwaysOnTop: return IDS_MENU_ALWAYS_ON_TOP;
    case StringId::kMenuToggleTheme: return IDS_MENU_TOGGLE_THEME;
    case StringId::kMenuExit: return IDS_MENU_EXIT;
    case StringId::kMenuOpen: return IDS_MENU_OPEN;
    case StringId::kMenuRefreshNow: return IDS_MENU_REFRESH_NOW;
    case StringId::kMenuEndProcess: return IDS_MENU_END_PROCESS;
    case StringId::kMenuOpenLocation: return IDS_MENU_OPEN_LOCATION;
    case StringId::kMenuOnlineSearch: return IDS_MENU_ONLINE_SEARCH;
    case StringId::kMenuProperties: return IDS_MENU_PROPERTIES;
    case StringId::kMenuSetPriority: return IDS_MENU_SET_PRIORITY;
    case StringId::kMenuCopy: return IDS_MENU_COPY;
    case StringId::kMenuCopyCommandLine: return IDS_MENU_COPY_CMD;
    case StringId::kMenuCopyPath: return IDS_MENU_COPY_PATH;
    case StringId::kMenuEndProcessTree: return IDS_MENU_END_TREE;
    case StringId::kMenuRestartAsAdmin: return IDS_MENU_RESTART_AS_ADMIN;
    case StringId::kMenuCopyJson: return IDS_MENU_COPY_JSON;
    case StringId::kMenuCopyTsv: return IDS_MENU_COPY_TSV;
    case StringId::kMenuHelp: return IDS_MENU_HELP;
    case StringId::kMenuAbout: return IDS_MENU_ABOUT;
    case StringId::kAboutTitle: return IDS_ABOUT_TITLE;
    case StringId::kAboutAppName: return IDS_ABOUT_APP_NAME;

    // Tooltips
    case StringId::kTooltipThemeLight: return IDS_TOOLTIP_THEME_LIGHT;
    case StringId::kTooltipThemeDark: return IDS_TOOLTIP_THEME_DARK;
    case StringId::kTooltipRefresh: return IDS_TOOLTIP_REFRESH;
    case StringId::kTooltipOptions: return IDS_TOOLTIP_OPTIONS;
    case StringId::kTooltipMonitor: return IDS_TOOLTIP_MONITOR;
    case StringId::kTooltipColumns: return IDS_TOOLTIP_COLUMNS;
    case StringId::kTooltipAlwaysOnTop: return IDS_TOOLTIP_ALWAYS_ON_TOP;
    case StringId::kTooltipViewTree: return IDS_TOOLTIP_VIEW_TREE;
    case StringId::kTooltipViewList: return IDS_TOOLTIP_VIEW_LIST;
    case StringId::kTooltipTopmostOff: return IDS_TOOLTIP_TOPMOST_OFF;
    case StringId::kTooltipTopmostOn: return IDS_TOOLTIP_TOPMOST_ON;
    case StringId::kTooltipEndProcess: return IDS_TOOLTIP_END_PROCESS;
    case StringId::kTooltipQuickFilter: return IDS_TOOLTIP_QUICK_FILTER;
    case StringId::kTooltipInterval: return IDS_TOOLTIP_INTERVAL;
    case StringId::kTooltipRestartAdmin: return IDS_TOOLTIP_RESTART_ADMIN;
    case StringId::kTooltipRunningAsAdmin: return IDS_TOOLTIP_RUNNING_AS_ADMIN;

    // Dialog Common
    case StringId::kBtnOk: return IDS_BTN_OK;
    case StringId::kBtnCancel: return IDS_BTN_CANCEL;
    case StringId::kBtnSave: return IDS_BTN_SAVE;
    case StringId::kBtnAdd: return IDS_BTN_ADD;
    case StringId::kBtnEdit: return IDS_BTN_EDIT;
    case StringId::kBtnDelete: return IDS_BTN_DELETE;
    case StringId::kBtnRemove: return IDS_BTN_REMOVE;
    case StringId::kBtnToggle: return IDS_BTN_TOGGLE;
    case StringId::kBtnMoveUp: return IDS_BTN_MOVE_UP;
    case StringId::kBtnMoveDown: return IDS_BTN_MOVE_DOWN;
    case StringId::kBtnDefault: return IDS_BTN_DEFAULT;
    case StringId::kBtnTestWarn: return IDS_BTN_TEST_WARN;
    case StringId::kBtnTestErr: return IDS_BTN_TEST_ERR;
    case StringId::kConfirmDelete: return IDS_CONFIRM_DELETE;
    case StringId::kConfirmDeleteMultiple: return IDS_CONFIRM_DELETE_MULTIPLE;
    case StringId::kConfirmRestartTheme: return IDS_CONFIRM_RESTART_THEME;
    case StringId::kRestartNoticeTitle: return IDS_RESTART_NOTICE_TITLE;
    case StringId::kMsgConfirmExit: return IDS_CONFIRM_EXIT;

    // Options Dialog
    case StringId::kDlgOptionsTitle: return IDS_DLG_OPTIONS_TITLE;
    case StringId::kLabelLanguage: return IDS_LABEL_LANGUAGE;
    case StringId::kLabelTheme: return IDS_LABEL_THEME;
    case StringId::kLabelListFont: return IDS_LABEL_LIST_FONT;
    case StringId::kLabelUiFont: return IDS_LABEL_UI_FONT;
    case StringId::kLabelFont: return IDS_LABEL_FONT;
    case StringId::kBtnChangeFont: return IDS_BTN_CHANGE_FONT;
    case StringId::kLabelRefreshInterval: return IDS_LABEL_REFRESH_INTERVAL;
    case StringId::kLabelAlwaysOnTop: return IDS_LABEL_ALWAYS_ON_TOP;
    case StringId::kLabelMinimizeToTray: return IDS_LABEL_MINIMIZE_TO_TRAY;
    case StringId::kLabelAutoStart: return IDS_LABEL_AUTO_START;
    case StringId::kLangAuto: return IDS_LANG_AUTO;
    case StringId::kLangJapanese: return IDS_LANG_JAPANESE;
    case StringId::kLangEnglish: return IDS_LANG_ENGLISH;
    case StringId::kThemeLight: return IDS_THEME_LIGHT;
    case StringId::kThemeDark: return IDS_THEME_DARK;
    case StringId::kSecondsUnit: return IDS_SECONDS_UNIT;
    case StringId::kLabelExcludedProcesses: return IDS_LABEL_EXCLUDED_PROCESSES;
    case StringId::kDlgAddExcludedTitle: return IDS_DLG_ADD_EXCLUDED_TITLE;
    case StringId::kLabelAddExcludedPrompt: return IDS_LABEL_ADD_EXCLUDED_PROMPT;

    // Monitor Dialog
    case StringId::kDlgMonitorTitle: return IDS_DLG_MONITOR_TITLE;
    case StringId::kColStatus: return IDS_COL_STATUS;
    case StringId::kColRuleName: return IDS_COL_RULE_NAME;
    case StringId::kColLevel: return IDS_COL_LEVEL;
    case StringId::kColTarget: return IDS_COL_TARGET;
    case StringId::kColCondition: return IDS_COL_CONDITION;
    case StringId::kStatusEnabled: return IDS_STATUS_ENABLED;
    case StringId::kStatusDisabled: return IDS_STATUS_DISABLED;
    case StringId::kLevelWarning: return IDS_LEVEL_WARNING;
    case StringId::kLevelCritical: return IDS_LEVEL_CRITICAL;
    case StringId::kMatchExact: return IDS_MATCH_EXACT;
    case StringId::kMatchContains: return IDS_MATCH_CONTAINS;
    case StringId::kMatchStartsWith: return IDS_MATCH_STARTS_WITH;
    case StringId::kMatchEndsWith: return IDS_MATCH_ENDS_WITH;
    case StringId::kMatchTargetProcessName: return IDS_MATCH_TARGET_PROCESS_NAME;
    case StringId::kMatchTargetPid: return IDS_MATCH_TARGET_PID;
    case StringId::kRuleEditTitleAdd: return IDS_RULE_EDIT_TITLE_ADD;
    case StringId::kRuleEditTitleEdit: return IDS_RULE_EDIT_TITLE_EDIT;
    case StringId::kRuleNameLabel: return IDS_RULE_NAME_LABEL;
    case StringId::kTargetTypeLabel: return IDS_TARGET_TYPE_LABEL;
    case StringId::kMatchTypeLabel: return IDS_MATCH_TYPE_LABEL;
    case StringId::kPatternLabel: return IDS_PATTERN_LABEL;
    case StringId::kConditionLabel: return IDS_CONDITION_LABEL;
    case StringId::kCooldownLabel: return IDS_COOLDOWN_LABEL;
    case StringId::kUnitSecondsCooldown: return IDS_UNIT_SECONDS_COOLDOWN;
    case StringId::kLogicAnd: return IDS_LOGIC_AND;
    case StringId::kLogicOr: return IDS_LOGIC_OR;
    case StringId::kLabelRuleName: return IDS_LABEL_RULE_NAME;
    case StringId::kLabelTarget: return IDS_LABEL_TARGET;
    case StringId::kLabelLevel: return IDS_LABEL_LEVEL;
    case StringId::kLabelLogic: return IDS_LABEL_LOGIC;
    case StringId::kOpContains: return IDS_OP_CONTAINS;
    case StringId::kItemDefault: return IDS_ITEM_DEFAULT;
    case StringId::kMsgRuleNameEmpty: return IDS_MSG_RULE_NAME_EMPTY;
    case StringId::kMsgPatternEmpty: return IDS_MSG_PATTERN_EMPTY;
    case StringId::kMsgSelectRuleToEdit: return IDS_MSG_SELECT_RULE_TO_EDIT;
    case StringId::kMsgSelectRuleToDelete: return IDS_MSG_SELECT_RULE_TO_DELETE;
    case StringId::kMsgSelectRuleToToggle: return IDS_MSG_SELECT_RULE_TO_TOGGLE;
    case StringId::kLabelNotifyIfNotFound: return IDS_LABEL_NOTIFY_IF_NOT_FOUND;
    case StringId::kMsgProcessNotFound: return IDS_MSG_PROCESS_NOT_FOUND;

    // Column Selector Dialog
    case StringId::kDlgColumnSelectorTitle: return IDS_DLG_COL_SELECTOR_TITLE;
    case StringId::kSelectColumnsInstruction: return IDS_SELECT_COLUMNS_INSTRUCTION;
    case StringId::kBtnSelectAll: return IDS_BTN_SELECT_ALL;
    case StringId::kBtnDeselectAll: return IDS_BTN_DESELECT_ALL;

    // Priority Names
    case StringId::kPriorityRealtime: return IDS_PRIORITY_REALTIME;
    case StringId::kPriorityHigh: return IDS_PRIORITY_HIGH;
    case StringId::kPriorityAboveNormal: return IDS_PRIORITY_ABOVE_NORMAL;
    case StringId::kPriorityNormal: return IDS_PRIORITY_NORMAL;
    case StringId::kPriorityBelowNormal: return IDS_PRIORITY_BELOW_NORMAL;
    case StringId::kPriorityLow: return IDS_PRIORITY_LOW;

    // Process Details & Snapshot Status
    case StringId::kStatusRunning: return IDS_STATUS_RUNNING;
    case StringId::kStatusSuspended: return IDS_STATUS_SUSPENDED;
    case StringId::kYes: return IDS_YES;
    case StringId::kNo: return IDS_NO;
    case StringId::kEnabled: return IDS_ENABLED;
    case StringId::kDisabled: return IDS_DISABLED;
    case StringId::kEnabledPermanent: return IDS_ENABLED_PERMANENT;
    case StringId::kNotApplicable: return IDS_NOT_APPLICABLE;
    case StringId::kPlatform32Bit: return IDS_PLATFORM_32BIT;
    case StringId::kPlatform64Bit: return IDS_PLATFORM_64BIT;
    case StringId::kPropertiesTitlePrefix: return IDS_PROPERTIES_TITLE_PREFIX;
    case StringId::kPropertiesHeader: return IDS_PROPERTIES_HEADER;
    case StringId::kPropImageName: return IDS_PROP_IMAGE_NAME;
    case StringId::kPropPid: return IDS_PROP_PID;
    case StringId::kPropParentPid: return IDS_PROP_PARENT_PID;
    case StringId::kPropStatus: return IDS_PROP_STATUS;
    case StringId::kPropUser: return IDS_PROP_USER;
    case StringId::kPropDescription: return IDS_PROP_DESCRIPTION;
    case StringId::kPropArchitecture: return IDS_PROP_ARCHITECTURE;
    case StringId::kPropPriority: return IDS_PROP_PRIORITY;
    case StringId::kPropThreads: return IDS_PROP_THREADS;
    case StringId::kPropHandles: return IDS_PROP_HANDLES;
    case StringId::kPropCpu: return IDS_PROP_CPU;
    case StringId::kPropWorkingSet: return IDS_PROP_WORKING_SET;
    case StringId::kPropCommit: return IDS_PROP_COMMIT;
    case StringId::kPropStartTime: return IDS_PROP_START_TIME;
    case StringId::kPropFilePath: return IDS_PROP_FILE_PATH;

    // Message Boxes & Prompts
    case StringId::kMsgConfirmEndProcess: return IDS_MSG_CONFIRM_END_PROCESS;
    case StringId::kMsgConfirmEndProcessMultiple: return IDS_MSG_CONFIRM_END_PROCESS_MULTIPLE;
    case StringId::kTitleConfirmEndProcess: return IDS_TITLE_CONFIRM_END_PROCESS;
    case StringId::kMsgEndProcessError: return IDS_MSG_END_PROCESS_ERROR;
    case StringId::kTitleError: return IDS_TITLE_ERROR;
    case StringId::kMsgConfirmEndTree: return IDS_MSG_CONFIRM_END_TREE;
    case StringId::kTitleConfirmEndTree: return IDS_TITLE_CONFIRM_END_TREE;
    case StringId::kMsgEndTreePartial: return IDS_MSG_END_TREE_PARTIAL;
    case StringId::kTitleNotice: return IDS_TITLE_NOTICE;
    case StringId::kTitleInfo: return IDS_TITLE_INFO;
    case StringId::kMsgWarnRealtime: return IDS_MSG_WARN_REALTIME;
    case StringId::kTitleChangePriority: return IDS_TITLE_CHANGE_PRIORITY;
    case StringId::kMsgChangePriorityError: return IDS_MSG_CHANGE_PRIORITY_ERROR;
    case StringId::kMsgAccessDenied: return IDS_MSG_ACCESS_DENIED;
    case StringId::kMsgFileNotFound: return IDS_MSG_FILE_NOT_FOUND;
    case StringId::kMsgRealtimeWarning: return IDS_MSG_REALTIME_WARNING;
    case StringId::kMsgTrayMinimized: return IDS_MSG_TRAY_MINIMIZED;
    case StringId::kMsgError: return IDS_MSG_ERROR;
    case StringId::kMsgInfo: return IDS_MSG_INFO;
    case StringId::kMsgWarning: return IDS_MSG_WARNING;

    // Services Management
    case StringId::kTabProcesses: return IDS_TAB_PROCESSES;
    case StringId::kTabServices: return IDS_TAB_SERVICES;
    case StringId::kSvcColName: return IDS_SVC_COL_NAME;
    case StringId::kSvcColDisplayName: return IDS_SVC_COL_DISPLAY_NAME;
    case StringId::kSvcColPid: return IDS_SVC_COL_PID;
    case StringId::kSvcColState: return IDS_SVC_COL_STATE;
    case StringId::kSvcColStartType: return IDS_SVC_COL_START_TYPE;
    case StringId::kSvcColAccount: return IDS_SVC_COL_ACCOUNT;
    case StringId::kSvcColDescription: return IDS_SVC_COL_DESCRIPTION;
    case StringId::kServiceStateRunning: return IDS_SVC_STATE_RUNNING;
    case StringId::kServiceStateStopped: return IDS_SVC_STATE_STOPPED;
    case StringId::kServiceStateStartPending: return IDS_SVC_STATE_START_PENDING;
    case StringId::kServiceStateStopPending: return IDS_SVC_STATE_STOP_PENDING;
    case StringId::kServiceStatePaused: return IDS_SVC_STATE_PAUSED;
    case StringId::kServiceStatePausePending: return IDS_SVC_STATE_PAUSE_PENDING;
    case StringId::kServiceStateContinuePending: return IDS_SVC_STATE_CONTINUE_PENDING;
    case StringId::kServiceStateUnknown: return IDS_SVC_STATE_UNKNOWN;
    case StringId::kServiceStartTypeAuto: return IDS_SVC_START_TYPE_AUTO;
    case StringId::kServiceStartTypeManual: return IDS_SVC_START_TYPE_MANUAL;
    case StringId::kServiceStartTypeDisabled: return IDS_SVC_START_TYPE_DISABLED;
    case StringId::kServiceStartTypeBoot: return IDS_SVC_START_TYPE_BOOT;
    case StringId::kServiceStartTypeSystem: return IDS_SVC_START_TYPE_SYSTEM;
    case StringId::kServiceStartTypeUnknown: return IDS_SVC_START_TYPE_UNKNOWN;
    case StringId::kMenuServiceStart: return IDS_MENU_SERVICE_START;
    case StringId::kMenuServiceStop: return IDS_MENU_SERVICE_STOP;
    case StringId::kMenuServiceRestart: return IDS_MENU_SERVICE_RESTART;
    case StringId::kMenuServiceStartup: return IDS_MENU_SERVICE_STARTUP;
    case StringId::kMenuServiceGoToProcess: return IDS_MENU_SERVICE_GO_TO_PROCESS;
    case StringId::kMenuProcessGoToService: return IDS_MENU_PROCESS_GO_TO_SERVICE;
    case StringId::kStatusServiceCounts: return IDS_STATUS_SERVICE_COUNTS;
  }
  return 0;
}

UINT ProcessColumnIdToResourceId(ProcessColumnId id) {
  switch (id) {
    case ProcessColumnId::kName: return IDS_COL_HDR_NAME;
    case ProcessColumnId::kPid: return IDS_COL_HDR_PID;
    case ProcessColumnId::kStatus: return IDS_COL_HDR_STATUS;
    case ProcessColumnId::kUserName: return IDS_COL_HDR_USER_NAME;
    case ProcessColumnId::kCpu: return IDS_COL_HDR_CPU;
    case ProcessColumnId::kWorkingSet: return IDS_COL_HDR_WORKING_SET;
    case ProcessColumnId::kPrivateWorkingSet: return IDS_COL_HDR_PRIVATE_WS;
    case ProcessColumnId::kPeakWorkingSet: return IDS_COL_HDR_PEAK_WS;
    case ProcessColumnId::kWorkingSetDelta: return IDS_COL_HDR_WS_DELTA;
    case ProcessColumnId::kCommitSize: return IDS_COL_HDR_COMMIT_SIZE;
    case ProcessColumnId::kPagedPool: return IDS_COL_HDR_PAGED_POOL;
    case ProcessColumnId::kNonPagedPool: return IDS_COL_HDR_NON_PAGED_POOL;
    case ProcessColumnId::kThreads: return IDS_COL_HDR_THREADS;
    case ProcessColumnId::kHandles: return IDS_COL_HDR_HANDLES;
    case ProcessColumnId::kBasePriority: return IDS_COL_HDR_BASE_PRIORITY;
    case ProcessColumnId::kDescription: return IDS_COL_HDR_DESCRIPTION;
    case ProcessColumnId::kOsContext: return IDS_COL_HDR_OS_CONTEXT;
    case ProcessColumnId::kFilePath: return IDS_COL_HDR_FILE_PATH;
    case ProcessColumnId::kCommandLine: return IDS_COL_HDR_COMMAND_LINE;
    case ProcessColumnId::kUserObjects: return IDS_COL_HDR_USER_OBJECTS;
    case ProcessColumnId::kGdiObjects: return IDS_COL_HDR_GDI_OBJECTS;
    case ProcessColumnId::kIoReadCount: return IDS_COL_HDR_IO_READ_COUNT;
    case ProcessColumnId::kIoReadBytes: return IDS_COL_HDR_IO_READ_BYTES;
    case ProcessColumnId::kIoWriteCount: return IDS_COL_HDR_IO_WRITE_COUNT;
    case ProcessColumnId::kIoWriteBytes: return IDS_COL_HDR_IO_WRITE_BYTES;
    case ProcessColumnId::kIoOtherCount: return IDS_COL_HDR_IO_OTHER_COUNT;
    case ProcessColumnId::kIoOtherBytes: return IDS_COL_HDR_IO_OTHER_BYTES;
    case ProcessColumnId::kElevated: return IDS_COL_HDR_ELEVATED;
    case ProcessColumnId::kUacVirtualization: return IDS_COL_HDR_UAC_VIRTUALIZATION;
    case ProcessColumnId::kDepStatus: return IDS_COL_HDR_DEP_STATUS;
    case ProcessColumnId::kEnterpriseContext: return IDS_COL_HDR_ENTERPRISE_CONTEXT;
    case ProcessColumnId::kDpiAwareness: return IDS_COL_HDR_DPI_AWARENESS;
    case ProcessColumnId::kPackageName: return IDS_COL_HDR_PACKAGE_NAME;
    case ProcessColumnId::kArchitecture: return IDS_COL_HDR_ARCHITECTURE;
    case ProcessColumnId::kPlatform: return IDS_COL_HDR_PLATFORM;
    case ProcessColumnId::kGpuUsage: return IDS_COL_HDR_GPU_USAGE;
    case ProcessColumnId::kGpuEngine: return IDS_COL_HDR_GPU_ENGINE;
    case ProcessColumnId::kDedicatedGpuMemory: return IDS_COL_HDR_DEDICATED_GPU_MEM;
    case ProcessColumnId::kSharedGpuMemory: return IDS_COL_HDR_SHARED_GPU_MEM;
    case ProcessColumnId::kSessionId: return IDS_COL_HDR_SESSION_ID;
    case ProcessColumnId::kCreateTime: return IDS_COL_HDR_CREATE_TIME;
  }
  return 0;
}

}  // namespace

void LanguageManager::Initialize(AppLanguage lang) {
  SetLanguage(lang);
}

void LanguageManager::SetLanguage(AppLanguage lang) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_configured_lang = lang;
  UpdateActiveLanguage();
  g_string_cache.clear();
}

AppLanguage LanguageManager::GetConfiguredLanguage() {
  return g_configured_lang;
}

bool LanguageManager::IsJapanese() {
  return g_active_is_japanese;
}

void LanguageManager::UpdateActiveLanguage() {
  if (g_configured_lang == AppLanguage::kJapanese) {
    g_active_is_japanese = true;
    g_active_langid = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
  } else if (g_configured_lang == AppLanguage::kEnglish) {
    g_active_is_japanese = false;
    g_active_langid = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
  } else {
    // Auto-detect from system
    LANGID sys_lang = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(sys_lang) == LANG_JAPANESE || GetACP() == 932) {
      g_active_is_japanese = true;
      g_active_langid = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
    } else {
      g_active_is_japanese = false;
      g_active_langid = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    }
  }

  SetThreadUILanguage(g_active_langid);
}

const wchar_t* LanguageManager::GetString(StringId id) {
  UINT resId = StringIdToResourceId(id);
  if (resId == 0) return L"";

  std::lock_guard<std::mutex> lock(g_cache_mutex);
  auto it = g_string_cache.find(resId);
  if (it != g_string_cache.end()) {
    return it->second.c_str();
  }

  std::wstring str;
  HMODULE hMod = GetResourceModule();
  if (!LoadStringExactLang(hMod, resId, g_active_langid, &str)) {
    wchar_t buf[512] = {0};
    int len = LoadStringW(hMod, resId, buf, 512);
    if (len > 0) str = buf;
  }

  auto inserted = g_string_cache.emplace(resId, std::move(str));
  return inserted.first->second.c_str();
}

std::wstring LanguageManager::GetColumnHeaderText(ProcessColumnId id) {
  UINT resId = ProcessColumnIdToResourceId(id);
  if (resId == 0) return L"";

  std::lock_guard<std::mutex> lock(g_cache_mutex);
  auto it = g_string_cache.find(resId);
  if (it != g_string_cache.end()) {
    return it->second;
  }

  std::wstring str;
  HMODULE hMod = GetResourceModule();
  if (!LoadStringExactLang(hMod, resId, g_active_langid, &str)) {
    wchar_t buf[256] = {0};
    int len = LoadStringW(hMod, resId, buf, 256);
    if (len > 0) str = buf;
  }

  g_string_cache.emplace(resId, str);
  return str;
}

}  // namespace lite_proc_manager
