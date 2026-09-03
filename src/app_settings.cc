// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "app_settings.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "json_helper.h"

namespace lite_proc_manager {

namespace {
std::wstring ReadUtf8File(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return L"";

  std::string utf8_content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  if (utf8_content.empty()) return L"";

  // Strip UTF-8 BOM if present
  if (utf8_content.size() >= 3 &&
      static_cast<unsigned char>(utf8_content[0]) == 0xEF &&
      static_cast<unsigned char>(utf8_content[1]) == 0xBB &&
      static_cast<unsigned char>(utf8_content[2]) == 0xBF) {
    utf8_content = utf8_content.substr(3);
  }

  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_content.c_str(), -1, nullptr, 0);
  if (wlen <= 1) return L"";

  std::wstring wide_content(wlen - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8_content.c_str(), -1, &wide_content[0], wlen);
  return wide_content;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& wide_content) {
  int ulen = WideCharToMultiByte(CP_UTF8, 0, wide_content.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (ulen <= 1) return false;

  std::string utf8_content(ulen - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide_content.c_str(), -1, &utf8_content[0], ulen, nullptr, nullptr);

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;

  file.write(utf8_content.data(), utf8_content.size());
  return true;
}
}  // namespace

AppSettings::AppSettings() : columns(ProcessColumnInfo::GetDefaultColumns()) {}

std::wstring AppSettings::GetAppDataDirectory() {
  wchar_t app_data[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, app_data))) {
    std::wstring dir = std::wstring(app_data) + L"\\ProcessManager";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
  }
  return L".";
}

std::wstring AppSettings::GetSettingsJsonPath() {
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  wchar_t* last_slash = wcsrchr(exe_path, L'\\');
  if (last_slash) {
    *last_slash = L'\0';
    std::wstring local_path = std::wstring(exe_path) + L"\\LPMSettings.json";
    if (GetFileAttributesW(local_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return local_path;
    }
    std::wstring legacy_local = std::wstring(exe_path) + L"\\PMSettings.json";
    if (GetFileAttributesW(legacy_local.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return legacy_local;
    }
  }

  std::wstring appdata_path = GetAppDataDirectory() + L"\\LPMSettings.json";
  if (GetFileAttributesW(appdata_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return appdata_path;
  }
  std::wstring legacy_appdata = GetAppDataDirectory() + L"\\PMSettings.json";
  if (GetFileAttributesW(legacy_appdata.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return legacy_appdata;
  }

  return appdata_path;
}

std::wstring AppSettings::GetMonitorRulesJsonPath() {
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  wchar_t* last_slash = wcsrchr(exe_path, L'\\');
  if (last_slash) {
    *last_slash = L'\0';
    std::wstring local_path = std::wstring(exe_path) + L"\\LPMMonitorRules.json";
    if (GetFileAttributesW(local_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return local_path;
    }
    std::wstring legacy_local = std::wstring(exe_path) + L"\\PMMonitorRules.json";
    if (GetFileAttributesW(legacy_local.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return legacy_local;
    }
  }

  std::wstring appdata_path = GetAppDataDirectory() + L"\\LPMMonitorRules.json";
  if (GetFileAttributesW(appdata_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return appdata_path;
  }
  std::wstring legacy_appdata = GetAppDataDirectory() + L"\\PMMonitorRules.json";
  if (GetFileAttributesW(legacy_appdata.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return legacy_appdata;
  }

  return appdata_path;
}

AppSettings AppSettings::Load() {
  return LoadFrom(GetSettingsJsonPath(), GetMonitorRulesJsonPath());
}

AppSettings AppSettings::LoadFrom(const std::wstring& settings_path, const std::wstring& rules_path) {
  AppSettings settings;

  // 1. Load LPMSettings.json (General and Columns)
  std::wstring settings_json_str = ReadUtf8File(settings_path);

  if (!settings_json_str.empty()) {
    JsonValue root = JsonValue::Parse(settings_json_str);
    if (root.is_object()) {
      // General
      if (root.has_key(L"General")) {
        const auto& gen = root[L"General"];
        std::wstring lang_str = gen[L"Language"].as_string(L"Auto");
        if (lang_str == L"Japanese") settings.language = AppLanguage::kJapanese;
        else if (lang_str == L"English") settings.language = AppLanguage::kEnglish;
        else settings.language = AppLanguage::kAuto;

        std::wstring theme_str = gen[L"Theme"].as_string(L"Light");
        settings.theme = (theme_str == L"Dark") ? AppTheme::kDark : AppTheme::kLight;

        // List Font (Mono font preferred for table alignment)
        if (gen.has_key(L"ListFontName")) {
          settings.list_font_name = gen[L"ListFontName"].as_string(L"Segoe UI");
        } else if (gen.has_key(L"FontName")) {
          settings.list_font_name = gen[L"FontName"].as_string(L"Segoe UI");
        } else {
          settings.list_font_name = L"Segoe UI";
        }

        if (gen.has_key(L"ListFontSize")) {
          settings.list_font_size = gen[L"ListFontSize"].as_int(9);
        } else if (gen.has_key(L"FontSize")) {
          settings.list_font_size = gen[L"FontSize"].as_int(9);
        } else {
          settings.list_font_size = 9;
        }

        // UI Font
        if (gen.has_key(L"UIFontName")) {
          settings.ui_font_name = gen[L"UIFontName"].as_string(L"Yu Gothic UI");
        } else if (gen.has_key(L"FontName")) {
          settings.ui_font_name = gen[L"FontName"].as_string(L"Yu Gothic UI");
        } else {
          settings.ui_font_name = L"Yu Gothic UI";
        }

        if (gen.has_key(L"UIFontSize")) {
          settings.ui_font_size = gen[L"UIFontSize"].as_int(9);
        } else if (gen.has_key(L"FontSize")) {
          settings.ui_font_size = gen[L"FontSize"].as_int(9);
        } else {
          settings.ui_font_size = 9;
        }

        std::wstring mode_str = gen[L"DisplayMode"].as_string(L"FlatList");
        settings.display_mode = (mode_str == L"ProcessTree") ? ViewDisplayMode::kProcessTree : ViewDisplayMode::kFlatList;

        settings.refresh_interval_seconds = gen[L"RefreshIntervalSeconds"].as_int(2);
        settings.always_on_top = gen[L"AlwaysOnTop"].as_bool(false);
        settings.minimize_to_tray = gen[L"MinimizeToTray"].as_bool(true);
        settings.auto_start = IsAutoStartConfigured();
        settings.window_width = gen[L"WindowWidth"].as_int(1150);
        settings.window_height = gen[L"WindowHeight"].as_int(700);
        settings.is_maximized = gen[L"IsMaximized"].as_bool(false);
      }

      // Columns
      if (root.has_key(L"Columns") && root[L"Columns"].is_array()) {
        const auto& cols_arr = root[L"Columns"].as_array();
        auto default_cols = ProcessColumnInfo::GetDefaultColumns();
        std::vector<ProcessColumnInfo> loaded_cols;

        for (const auto& col_val : cols_arr) {
          int id = col_val[L"Id"].as_int(-1);
          if (id < 0) continue;

          auto it = std::find_if(default_cols.begin(), default_cols.end(),
                                 [id](const ProcessColumnInfo& c) { return static_cast<int>(c.id) == id; });
          if (it != default_cols.end()) {
            ProcessColumnInfo info = *it;
            info.visible = col_val[L"Visible"].as_bool(info.visible);
            info.default_width = col_val[L"Width"].as_int(info.default_width);
            info.order = col_val[L"Order"].as_int(info.order);
            loaded_cols.push_back(info);
          }
        }

        // Add any missing columns from defaults
        for (const auto& def_col : default_cols) {
          auto it = std::find_if(loaded_cols.begin(), loaded_cols.end(),
                                 [&def_col](const ProcessColumnInfo& c) { return c.id == def_col.id; });
          if (it == loaded_cols.end()) {
            loaded_cols.push_back(def_col);
          }
        }

        std::sort(loaded_cols.begin(), loaded_cols.end(),
                  [](const ProcessColumnInfo& a, const ProcessColumnInfo& b) { return a.order < b.order; });

        settings.columns = loaded_cols;
      }

      // DisplayFilter
      if (root.has_key(L"DisplayFilter")) {
        const auto& df = root[L"DisplayFilter"];
        settings.display_filter_enabled = df[L"Enabled"].as_bool(false);
        settings.display_filter_condition.column_id =
            static_cast<ProcessColumnId>(df[L"ColumnId"].as_int(static_cast<int>(ProcessColumnId::kCpu)));

        std::wstring op_str = df[L"Operator"].as_string(L">");
        if (op_str == L">") settings.display_filter_condition.op = ComparisonOperator::kGreaterThan;
        else if (op_str == L">=") settings.display_filter_condition.op = ComparisonOperator::kGreaterThanOrEqual;
        else if (op_str == L"<") settings.display_filter_condition.op = ComparisonOperator::kLessThan;
        else if (op_str == L"<=") settings.display_filter_condition.op = ComparisonOperator::kLessThanOrEqual;
        else if (op_str == L"==") settings.display_filter_condition.op = ComparisonOperator::kEqual;
        else if (op_str == L"!=") settings.display_filter_condition.op = ComparisonOperator::kNotEqual;
        else if (op_str == L"Contains") settings.display_filter_condition.op = ComparisonOperator::kContains;

        settings.display_filter_condition.numeric_value = df[L"NumericValue"].as_double(0.0);
        settings.display_filter_condition.string_value = df[L"StringValue"].as_string(L"");
      }

      // ExcludedProcesses
      if (root.has_key(L"ExcludedProcesses") && root[L"ExcludedProcesses"].is_array()) {
        std::vector<std::wstring> loaded_excl;
        for (const auto& item : root[L"ExcludedProcesses"].as_array()) {
          if (item.is_string() && !item.as_string().empty()) {
            loaded_excl.push_back(item.as_string());
          }
        }
        settings.excluded_processes = loaded_excl;
      }
    }
  }

  // 2. Load LPMMonitorRules.json (MonitorRules)
  std::wstring rules_json_str = ReadUtf8File(rules_path);

  if (!rules_json_str.empty()) {
    JsonValue rules_root = JsonValue::Parse(rules_json_str);
    if (rules_root.is_object() && rules_root.has_key(L"MonitorRules") && rules_root[L"MonitorRules"].is_array()) {
      const auto& rules_arr = rules_root[L"MonitorRules"].as_array();
      for (const auto& r_val : rules_arr) {
        MonitorRule rule;
        rule.id = r_val[L"Id"].as_string(rule.id);
        rule.name = r_val[L"Name"].as_string(L"監視ルール");
        rule.enabled = r_val[L"Enabled"].as_bool(true);

        std::wstring target_str = r_val[L"MatchTarget"].as_string(L"ProcessName");
        rule.match_target = (target_str == L"Pid") ? ProcessMatchTarget::kPid : ProcessMatchTarget::kProcessName;

        std::wstring match_str = r_val[L"MatchType"].as_string(L"Contains");
        if (match_str == L"Exact") rule.match_type = ProcessMatchType::kExact;
        else if (match_str == L"StartsWith") rule.match_type = ProcessMatchType::kStartsWith;
        else if (match_str == L"EndsWith") rule.match_type = ProcessMatchType::kEndsWith;
        else rule.match_type = ProcessMatchType::kContains;

        rule.target_pattern = r_val[L"Pattern"].as_string(L"");

        std::wstring level_str = r_val[L"Level"].as_string(L"Warning");
        rule.level = (level_str == L"Critical") ? EventLevel::kCritical : EventLevel::kWarning;

        std::wstring logic_str = r_val[L"Logic"].as_string(L"And");
        rule.logical_op = (logic_str == L"Or") ? LogicalOperator::kOr : LogicalOperator::kAnd;

        rule.cooldown_seconds = r_val[L"CooldownSeconds"].as_int(30);
        rule.notify_if_not_found = r_val[L"NotifyIfNotFound"].as_bool(false);

        if (r_val.has_key(L"Conditions") && r_val[L"Conditions"].is_array()) {
          const auto& conds_arr = r_val[L"Conditions"].as_array();
          for (const auto& c_val : conds_arr) {
            MonitorCondition cond;
            cond.column_id = static_cast<ProcessColumnId>(c_val[L"ColumnId"].as_int(static_cast<int>(ProcessColumnId::kCpu)));

            std::wstring op_str = c_val[L"Operator"].as_string(L">=");
            if (op_str == L">") cond.op = ComparisonOperator::kGreaterThan;
            else if (op_str == L">=") cond.op = ComparisonOperator::kGreaterThanOrEqual;
            else if (op_str == L"<") cond.op = ComparisonOperator::kLessThan;
            else if (op_str == L"<=") cond.op = ComparisonOperator::kLessThanOrEqual;
            else if (op_str == L"==") cond.op = ComparisonOperator::kEqual;
            else if (op_str == L"!=") cond.op = ComparisonOperator::kNotEqual;
            else if (op_str == L"Contains") cond.op = ComparisonOperator::kContains;

            cond.numeric_value = c_val[L"NumericValue"].as_double(80.0);
            cond.string_value = c_val[L"StringValue"].as_string(L"");

            rule.conditions.push_back(cond);
          }
        }

        settings.monitor_rules.push_back(rule);
      }
    }
  }

  return settings;
}

void AppSettings::SaveSettings() const {
  SaveSettingsTo(GetSettingsJsonPath());
}

void AppSettings::SaveSettingsTo(const std::wstring& path) const {
  JsonObject root_obj;

  // General Object
  JsonObject gen_obj;
  std::wstring lang_str = L"Auto";
  if (language == AppLanguage::kJapanese) lang_str = L"Japanese";
  else if (language == AppLanguage::kEnglish) lang_str = L"English";
  gen_obj.push_back({L"Language", lang_str});
  gen_obj.push_back({L"Theme", (theme == AppTheme::kLight) ? L"Light" : L"Dark"});
  gen_obj.push_back({L"ListFontName", list_font_name});
  gen_obj.push_back({L"ListFontSize", list_font_size});
  gen_obj.push_back({L"UIFontName", ui_font_name});
  gen_obj.push_back({L"UIFontSize", ui_font_size});
  gen_obj.push_back({L"DisplayMode", (display_mode == ViewDisplayMode::kProcessTree) ? L"ProcessTree" : L"FlatList"});
  gen_obj.push_back({L"RefreshIntervalSeconds", refresh_interval_seconds});
  gen_obj.push_back({L"AlwaysOnTop", always_on_top});
  gen_obj.push_back({L"MinimizeToTray", minimize_to_tray});
  gen_obj.push_back({L"AutoStart", auto_start});
  gen_obj.push_back({L"WindowWidth", window_width});
  gen_obj.push_back({L"WindowHeight", window_height});
  gen_obj.push_back({L"IsMaximized", is_maximized});
  root_obj.push_back({L"General", JsonValue(gen_obj)});

  // Columns Array
  JsonArray cols_arr;
  for (size_t i = 0; i < columns.size(); ++i) {
    const auto& col = columns[i];
    JsonObject col_obj;
    col_obj.push_back({L"Id", static_cast<int>(col.id)});
    col_obj.push_back({L"Name", col.header_text});
    col_obj.push_back({L"Visible", col.visible});
    col_obj.push_back({L"Width", col.default_width});
    col_obj.push_back({L"Order", col.order});
    cols_arr.push_back(JsonValue(col_obj));
  }
  root_obj.push_back({L"Columns", JsonValue(cols_arr)});

  // DisplayFilter Object
  {
    std::wstring op_str = MonitorRule::OperatorToString(display_filter_condition.op);
    JsonObject df_obj;
    df_obj.push_back({L"Enabled", display_filter_enabled});
    df_obj.push_back({L"ColumnId", static_cast<int>(display_filter_condition.column_id)});
    df_obj.push_back({L"Operator", op_str});
    df_obj.push_back({L"NumericValue", display_filter_condition.numeric_value});
    df_obj.push_back({L"StringValue", display_filter_condition.string_value});
    root_obj.push_back({L"DisplayFilter", JsonValue(df_obj)});
  }

  // ExcludedProcesses Array
  {
    JsonArray excl_arr;
    for (const auto& proc_name : excluded_processes) {
      if (!proc_name.empty()) {
        excl_arr.push_back(JsonValue(proc_name));
      }
    }
    root_obj.push_back({L"ExcludedProcesses", JsonValue(excl_arr)});
  }

  JsonValue root(root_obj);
  std::wstring json_str = root.Serialize(2);
  WriteUtf8File(path, json_str);
}

void AppSettings::SaveMonitorRules() const {
  SaveMonitorRulesTo(GetMonitorRulesJsonPath());
}

void AppSettings::SaveMonitorRulesTo(const std::wstring& path) const {
  JsonObject root_obj;
  JsonArray rules_arr;

  for (const auto& rule : monitor_rules) {
    JsonObject r_obj;
    r_obj.push_back({L"Id", rule.id});
    r_obj.push_back({L"Name", rule.name});
    r_obj.push_back({L"Enabled", rule.enabled});
    r_obj.push_back({L"MatchTarget", (rule.match_target == ProcessMatchTarget::kPid) ? L"Pid" : L"ProcessName"});

    std::wstring match_str = L"Contains";
    if (rule.match_type == ProcessMatchType::kExact) match_str = L"Exact";
    else if (rule.match_type == ProcessMatchType::kStartsWith) match_str = L"StartsWith";
    else if (rule.match_type == ProcessMatchType::kEndsWith) match_str = L"EndsWith";
    r_obj.push_back({L"MatchType", match_str});

    r_obj.push_back({L"Pattern", rule.target_pattern});
    r_obj.push_back({L"Level", (rule.level == EventLevel::kCritical) ? L"Critical" : L"Warning"});
    r_obj.push_back({L"Logic", (rule.logical_op == LogicalOperator::kOr) ? L"Or" : L"And"});
    r_obj.push_back({L"CooldownSeconds", rule.cooldown_seconds});
    r_obj.push_back({L"NotifyIfNotFound", rule.notify_if_not_found});

    JsonArray conds_arr;
    for (const auto& cond : rule.conditions) {
      JsonObject c_obj;
      c_obj.push_back({L"ColumnId", static_cast<int>(cond.column_id)});
      c_obj.push_back({L"Operator", MonitorRule::OperatorToString(cond.op)});
      c_obj.push_back({L"NumericValue", cond.numeric_value});
      c_obj.push_back({L"StringValue", cond.string_value});
      conds_arr.push_back(JsonValue(c_obj));
    }
    r_obj.push_back({L"Conditions", JsonValue(conds_arr)});

    rules_arr.push_back(JsonValue(r_obj));
  }
  root_obj.push_back({L"MonitorRules", JsonValue(rules_arr)});

  JsonValue root(root_obj);
  std::wstring json_str = root.Serialize(2);
  WriteUtf8File(path, json_str);
}

void AppSettings::Save() const {
  SaveSettings();
  SaveMonitorRules();
}

bool AppSettings::IsAutoStartConfigured() {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                    0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t path[MAX_PATH * 2] = {0};
  DWORD size = sizeof(path);
  DWORD type = 0;
  LSTATUS status = RegQueryValueExW(hkey, L"LiteProcManager", nullptr, &type,
                                    reinterpret_cast<LPBYTE>(path), &size);
  RegCloseKey(hkey);
  return (status == ERROR_SUCCESS);
}

bool AppSettings::SetAutoStart(bool enable) {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                    0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS) {
    return false;
  }

  LSTATUS status = ERROR_SUCCESS;
  if (enable) {
    wchar_t exe_path[MAX_PATH * 2] = {0};
    if (GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(std::size(exe_path))) > 0) {
      std::wstring cmd = L"\"" + std::wstring(exe_path) + L"\"";
      status = RegSetValueExW(hkey, L"LiteProcManager", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(cmd.c_str()),
                              static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
    } else {
      status = ERROR_FILE_NOT_FOUND;
    }
  } else {
    status = RegDeleteValueW(hkey, L"LiteProcManager");
    if (status == ERROR_FILE_NOT_FOUND) {
      status = ERROR_SUCCESS;
    }
  }

  RegCloseKey(hkey);
  return (status == ERROR_SUCCESS);
}

}  // namespace lite_proc_manager
