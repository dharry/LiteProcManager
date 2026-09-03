// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_APP_SETTINGS_H_
#define LITE_PROC_MANAGER_APP_SETTINGS_H_

#include <string>
#include <vector>

#include "monitor_rule.h"
#include "process_column.h"

namespace lite_proc_manager {

enum class AppTheme {
  kLight,
  kDark,
};

enum class AppLanguage {
  kAuto,
  kJapanese,
  kEnglish,
};

enum class ViewDisplayMode {
  kFlatList,
  kProcessTree,
};

class AppSettings {
 public:
  AppSettings();
  ~AppSettings() = default;

  AppTheme theme{AppTheme::kLight};
  AppLanguage language{AppLanguage::kAuto};
  std::wstring list_font_name{L"Segoe UI"};
  int list_font_size{9};
  std::wstring ui_font_name{L"Yu Gothic UI"};
  int ui_font_size{9};
  ViewDisplayMode display_mode{ViewDisplayMode::kFlatList};
  int refresh_interval_seconds{5};
  bool always_on_top{false};
  bool minimize_to_tray{true};
  bool auto_start{false};
  int window_width{1150};
  int window_height{700};
  bool is_maximized{false};
  std::vector<ProcessColumnInfo> columns;
  std::vector<MonitorRule> monitor_rules;
  bool display_filter_enabled{false};
  MonitorCondition display_filter_condition;
  std::vector<std::wstring> excluded_processes{L"Memory Compression", L"Secure System", L"System Idle Process"};

  static AppSettings Load();
  static AppSettings LoadFrom(const std::wstring& settings_path, const std::wstring& rules_path);
  void SaveSettings() const;
  void SaveMonitorRules() const;
  void Save() const;

  void SaveSettingsTo(const std::wstring& path) const;
  void SaveMonitorRulesTo(const std::wstring& path) const;

  static bool IsAutoStartConfigured();
  static bool SetAutoStart(bool enable);

  static std::wstring GetSettingsJsonPath();
  static std::wstring GetMonitorRulesJsonPath();

 private:
  static std::wstring GetAppDataDirectory();
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_APP_SETTINGS_H_
