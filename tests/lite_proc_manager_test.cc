// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "CppUnitTest.h"

#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../include/app_settings.h"
#include "../include/json_helper.h"
#include "../include/language_manager.h"
#include "../include/monitor_rule.h"
#include "../include/monitor_service.h"
#include "../include/native_types.h"
#include "../include/process_item.h"
#include "../include/process_snapshot_service.h"
#include "../include/service_item.h"
#include "../include/service_manager_service.h"
#include "../include/version.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace lite_proc_manager;

namespace Microsoft {
namespace VisualStudio {
namespace CppUnitTestFramework {
template <>
inline std::wstring ToString<EventLevel>(const EventLevel& q) {
  return (q == EventLevel::kCritical) ? L"Critical" : L"Warning";
}
template <>
inline std::wstring ToString<AppTheme>(const AppTheme& q) {
  return (q == AppTheme::kDark) ? L"Dark" : L"Light";
}
}  // namespace CppUnitTestFramework
}  // namespace VisualStudio
}  // namespace Microsoft

namespace LiteProcManagerTests {

TEST_CLASS(ProcessSnapshotTests) {
 public:
  TEST_METHOD(ShouldRetrieveProcessList) {
    ProcessSnapshotService service;
    auto result = service.GetSnapshot();

    Assert::IsFalse(result.items.empty());
    Assert::IsTrue(result.totals.total_processes > 0);
    Assert::IsTrue(result.totals.total_threads > 0);
    Assert::IsTrue(result.totals.total_handles > 0);
    Assert::IsTrue(result.totals.total_physical_memory > 0);

    bool found_system_or_idle = false;
    for (const auto& item : result.items) {
      if (item->process_id == 0 || item->name.find(L"System") != std::wstring::npos) {
        found_system_or_idle = true;
        break;
      }
    }
    Assert::IsTrue(found_system_or_idle);
  }

  TEST_METHOD(MultipleSnapshots_ShouldCalculateCpuUsage) {
    ProcessSnapshotService service;

    // First snapshot (baseline)
    auto result1 = service.GetSnapshot();
    Assert::IsFalse(result1.items.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Second snapshot (Accurate CPU delta calculation)
    auto result2 = service.GetSnapshot();
    Assert::IsFalse(result2.items.empty());
    Assert::IsTrue(result2.totals.total_cpu_usage >= 0.0 && result2.totals.total_cpu_usage <= 100.0);

    // Verify individual process CPU usage range
    for (const auto& item : result2.items) {
      Assert::IsTrue(item->cpu_percent >= 0.0 && item->cpu_percent <= 100.0);
    }
  }
};

TEST_CLASS(ProcessItemTests) {
 public:
  TEST_METHOD(Formatting_ShouldReturnValidStrings) {
    ProcessItem item;
    item.process_id = 1234;
    item.name = L"test.exe";
    item.cpu_percent = 12.34;
    item.working_set = 1024ULL * 1024 * 50;         // 50MB
    item.private_working_set = 1024ULL * 1024 * 40; // 40MB
    item.peak_working_set = 1024ULL * 1024 * 60;    // 60MB
    item.working_set_delta = 1024 * 1024 * 2;       // +2MB
    item.commit_size = 1024ULL * 1024 * 60;         // 60MB
    item.paged_pool = 1024ULL * 512;                // 512KB
    item.non_paged_pool = 1024ULL * 256;            // 256KB
    item.thread_count = 8;
    item.handle_count = 250;
    item.user_objects = 15;
    item.gdi_objects = 25;
    item.io_read_count = 1200;
    item.io_read_bytes = 1024ULL * 1024 * 10;       // 10MB
    item.priority = ProcessPriorityClass::kHigh;
    item.architecture = L"x64";
    item.elevated = L"はい";
    item.dep_status = L"有効 (永続的)";

    Assert::AreEqual(std::wstring(L"12.3 %"), item.GetFormattedCpu());
    Assert::AreEqual(std::wstring(L"51,200 K"), item.GetFormattedWorkingSet());
    Assert::AreEqual(std::wstring(L"40,960 K"), item.GetFormattedPrivateWorkingSet());
    Assert::AreEqual(std::wstring(L"61,440 K"), item.GetFormattedPeakWorkingSet());
    Assert::AreEqual(std::wstring(L"+2,048 K"), item.GetFormattedWorkingSetDelta());
    Assert::AreEqual(std::wstring(L"61,440 K"), item.GetFormattedCommitSize());
    Assert::AreEqual(std::wstring(L"512 K"), item.GetFormattedPagedPool());
    Assert::AreEqual(std::wstring(L"256 K"), item.GetFormattedNonPagedPool());
    Assert::AreEqual(std::wstring(L"高"), item.GetFormattedPriority());
    Assert::AreEqual(std::wstring(L"1234"), item.GetColumnValue(ProcessColumnId::kPid));
    Assert::AreEqual(std::wstring(L"test.exe"), item.GetColumnValue(ProcessColumnId::kName));
    Assert::AreEqual(std::wstring(L"1,200"), item.GetColumnValue(ProcessColumnId::kIoReadCount));
    Assert::AreEqual(std::wstring(L"10,240 K"), item.GetColumnValue(ProcessColumnId::kIoReadBytes));
    Assert::AreEqual(std::wstring(L"はい"), item.GetColumnValue(ProcessColumnId::kElevated));
    Assert::AreEqual(std::wstring(L"有効 (永続的)"), item.GetColumnValue(ProcessColumnId::kDepStatus));
  }

  TEST_METHOD(Build_ShouldEstablishHierarchy) {
    std::vector<std::shared_ptr<ProcessItem>> flat_list;

    auto p1 = std::make_shared<ProcessItem>();
    p1->process_id = 1;
    p1->parent_process_id = 0;
    p1->name = L"Root";
    flat_list.push_back(p1);

    auto p2 = std::make_shared<ProcessItem>();
    p2->process_id = 2;
    p2->parent_process_id = 1;
    p2->name = L"Child1";
    flat_list.push_back(p2);

    auto p3 = std::make_shared<ProcessItem>();
    p3->process_id = 3;
    p3->parent_process_id = 1;
    p3->name = L"Child2";
    flat_list.push_back(p3);

    auto p4 = std::make_shared<ProcessItem>();
    p4->process_id = 4;
    p4->parent_process_id = 2;
    p4->name = L"GrandChild";
    flat_list.push_back(p4);

    auto roots = ProcessSnapshotService::BuildProcessTree(flat_list);

    Assert::AreEqual(1ULL, roots.size());
    Assert::AreEqual(1U, roots[0]->process_id);
    Assert::AreEqual(2ULL, roots[0]->children.size());
    Assert::AreEqual(1ULL, roots[0]->children[0]->children.size());
    Assert::AreEqual(4U, roots[0]->children[0]->children[0]->process_id);
  }

  TEST_METHOD(FormatBytes_ShouldFormatProperly) {
    Assert::AreEqual(std::wstring(L"0 K"), ProcessItem::FormatBytes(0));
    Assert::AreEqual(std::wstring(L"512 K"), ProcessItem::FormatBytes(512 * 1024));
    Assert::AreEqual(std::wstring(L"1,536 K"), ProcessItem::FormatBytes(static_cast<uint64_t>(1.5 * 1024 * 1024)));
    Assert::AreEqual(std::wstring(L"2,621,440 K"), ProcessItem::FormatBytes(static_cast<uint64_t>(2.5 * 1024 * 1024 * 1024)));

    Assert::AreEqual(std::wstring(L"+512 K"), ProcessItem::FormatDeltaBytes(512 * 1024));
    Assert::AreEqual(std::wstring(L"-1,536 K"), ProcessItem::FormatDeltaBytes(static_cast<int64_t>(-1.5 * 1024 * 1024)));
  }
};

TEST_CLASS(SettingsAndMonitorTests) {
 public:
  TEST_METHOD(Columns_ShouldHaveAll41Columns) {
    auto columns = ProcessColumnInfo::GetDefaultColumns();
    Assert::AreEqual(41ULL, columns.size());

    bool found_name = false, found_pid = false, found_cpu = false, found_gpu = false;
    bool found_priv_ws = false, found_peak_ws = false, found_io_read = false, found_elevated = false;

    for (const auto& c : columns) {
      if (c.id == ProcessColumnId::kName) found_name = true;
      if (c.id == ProcessColumnId::kPid) found_pid = true;
      if (c.id == ProcessColumnId::kCpu) found_cpu = true;
      if (c.id == ProcessColumnId::kGpuUsage) found_gpu = true;
      if (c.id == ProcessColumnId::kPrivateWorkingSet) found_priv_ws = true;
      if (c.id == ProcessColumnId::kPeakWorkingSet) found_peak_ws = true;
      if (c.id == ProcessColumnId::kIoReadCount) found_io_read = true;
      if (c.id == ProcessColumnId::kElevated) found_elevated = true;
    }

    Assert::IsTrue(found_name);
    Assert::IsTrue(found_pid);
    Assert::IsTrue(found_cpu);
    Assert::IsTrue(found_gpu);
    Assert::IsTrue(found_priv_ws);
    Assert::IsTrue(found_peak_ws);
    Assert::IsTrue(found_io_read);
    Assert::IsTrue(found_elevated);
  }

  TEST_METHOD(MonitorRule_ProcessMatching) {
    ProcessItem item;
    item.name = L"chrome.exe";
    item.process_id = 12345;

    // 1. Process Name Exact
    MonitorRule r1;
    r1.match_target = ProcessMatchTarget::kProcessName;
    r1.match_type = ProcessMatchType::kExact;
    r1.target_pattern = L"CHROME.EXE"; // Case-insensitive
    Assert::IsTrue(r1.MatchesProcess(item));

    r1.target_pattern = L"notepad.exe";
    Assert::IsFalse(r1.MatchesProcess(item));

    // 2. Process Name Contains
    MonitorRule r2;
    r2.match_target = ProcessMatchTarget::kProcessName;
    r2.match_type = ProcessMatchType::kContains;
    r2.target_pattern = L"rom";
    Assert::IsTrue(r2.MatchesProcess(item));

    // 3. Process Name StartsWith
    MonitorRule r3;
    r3.match_target = ProcessMatchTarget::kProcessName;
    r3.match_type = ProcessMatchType::kStartsWith;
    r3.target_pattern = L"chr";
    Assert::IsTrue(r3.MatchesProcess(item));
    r3.target_pattern = L"ome";
    Assert::IsFalse(r3.MatchesProcess(item));

    // 4. Process Name EndsWith
    MonitorRule r4;
    r4.match_target = ProcessMatchTarget::kProcessName;
    r4.match_type = ProcessMatchType::kEndsWith;
    r4.target_pattern = L".EXE";
    Assert::IsTrue(r4.MatchesProcess(item));

    // 5. PID Match
    MonitorRule r5;
    r5.match_target = ProcessMatchTarget::kPid;
    r5.match_type = ProcessMatchType::kExact;
    r5.target_pattern = L"12345";
    Assert::IsTrue(r5.MatchesProcess(item));
    r5.target_pattern = L"99999";
    Assert::IsFalse(r5.MatchesProcess(item));
  }

  TEST_METHOD(MonitorRule_ConditionEvaluation) {
    ProcessItem item;
    item.name = L"svchost.exe";
    item.process_id = 2000;
    item.cpu_percent = 85.5;
    item.working_set = 1024ULL * 1024 * 600; // 600MB
    item.thread_count = 120;

    // Rule 1: AND condition (CPU >= 80% && WorkingSet >= 500MB)
    MonitorRule r1;
    r1.name = L"高負荷プロセス";
    r1.match_target = ProcessMatchTarget::kProcessName;
    r1.match_type = ProcessMatchType::kContains;
    r1.target_pattern = L"svchost";
    r1.logical_op = LogicalOperator::kAnd;
    r1.level = EventLevel::kCritical;

    MonitorCondition c1;
    c1.column_id = ProcessColumnId::kCpu;
    c1.op = ComparisonOperator::kGreaterThanOrEqual;
    c1.numeric_value = 80.0;
    r1.conditions.push_back(c1);

    MonitorCondition c2;
    c2.column_id = ProcessColumnId::kWorkingSet;
    c2.op = ComparisonOperator::kGreaterThanOrEqual;
    c2.numeric_value = 1024.0 * 1024.0 * 500.0;
    r1.conditions.push_back(c2);

    std::wstring reason;
    Assert::IsTrue(r1.Evaluate(item, &reason));
    Assert::IsFalse(reason.empty());

    // Fail case for AND: CPU drops below threshold
    item.cpu_percent = 50.0;
    Assert::IsFalse(r1.Evaluate(item, &reason));

    // Rule 2: OR condition (CPU >= 80% || Threads > 100)
    MonitorRule r2;
    r2.logical_op = LogicalOperator::kOr;
    r2.conditions = {c1};
    MonitorCondition c3;
    c3.column_id = ProcessColumnId::kThreads;
    c3.op = ComparisonOperator::kGreaterThan;
    c3.numeric_value = 100;
    r2.conditions.push_back(c3);

    // Even with CPU=50%, Threads=120 > 100 should pass
    Assert::IsTrue(r2.Evaluate(item, &reason));
  }

  TEST_METHOD(MonitorRule_NumericColumns_AndKiBBytesCondition) {
    const auto& numeric_cols = MonitorRule::GetNumericRuleColumnIds();
    Assert::IsTrue(numeric_cols.size() >= 20);

    // Verify IsBytesColumn
    Assert::IsTrue(MonitorRule::IsBytesColumn(ProcessColumnId::kWorkingSet));
    Assert::IsTrue(MonitorRule::IsBytesColumn(ProcessColumnId::kCommitSize));
    Assert::IsTrue(MonitorRule::IsBytesColumn(ProcessColumnId::kPrivateWorkingSet));
    Assert::IsTrue(MonitorRule::IsBytesColumn(ProcessColumnId::kDedicatedGpuMemory));
    Assert::IsFalse(MonitorRule::IsBytesColumn(ProcessColumnId::kCpu));
    Assert::IsFalse(MonitorRule::IsBytesColumn(ProcessColumnId::kThreads));
    Assert::IsFalse(MonitorRule::IsBytesColumn(ProcessColumnId::kHandles));

    // Verify KiB (K) unit evaluation:
    // 50,000 KiB = 51,200,000 Bytes
    MonitorCondition cond;
    cond.column_id = ProcessColumnId::kWorkingSet;
    cond.op = ComparisonOperator::kGreaterThan;
    cond.numeric_value = 50000.0 * 1024.0; // 50,000 KiB in Bytes

    ProcessItem item;
    item.working_set = 60000ULL * 1024; // 60,000 KiB -> should pass
    Assert::IsTrue(cond.Evaluate(item));

    item.working_set = 40000ULL * 1024; // 40,000 KiB -> should fail
    Assert::IsFalse(cond.Evaluate(item));
  }

  TEST_METHOD(MonitorService_MonitoringAndCooldown) {
    MonitorService service;

    MonitorRule rule;
    rule.name = L"メモリ警告";
    rule.match_target = ProcessMatchTarget::kProcessName;
    rule.match_type = ProcessMatchType::kContains;
    rule.target_pattern = L"test";
    rule.level = EventLevel::kWarning;
    rule.cooldown_seconds = 10;

    MonitorCondition cond;
    cond.column_id = ProcessColumnId::kWorkingSet;
    cond.op = ComparisonOperator::kGreaterThanOrEqual;
    cond.numeric_value = 1024.0 * 1024.0 * 100.0; // 100MB
    rule.conditions.push_back(cond);

    service.SetRules({rule});

    auto proc1 = std::make_shared<ProcessItem>();
    proc1->name = L"test_app.exe";
    proc1->process_id = 4567;
    proc1->working_set = 1024ULL * 1024 * 150; // 150MB

    std::vector<std::shared_ptr<ProcessItem>> procs = {proc1};

    // 1st Check: Should trigger 1 event
    auto events1 = service.CheckProcesses(procs);
    Assert::AreEqual(1ULL, events1.size());
    Assert::AreEqual(std::wstring(L"test_app.exe"), events1[0].process_name);
    Assert::AreEqual(EventLevel::kWarning, events1[0].level);

    // 2nd Check immediate: Should be suppressed by cooldown (0 events)
    auto events2 = service.CheckProcesses(procs);
    Assert::AreEqual(0ULL, events2.size());
  }

  TEST_METHOD(MonitorService_NotifyIfNotFound) {
    MonitorService service;

    MonitorRule rule;
    rule.id = L"rule_deadman";
    rule.name = L"死活監視テスト";
    rule.match_target = ProcessMatchTarget::kProcessName;
    rule.match_type = ProcessMatchType::kExact;
    rule.target_pattern = L"important_service.exe";
    rule.level = EventLevel::kCritical;
    rule.notify_if_not_found = true;
    rule.cooldown_seconds = 10;

    service.SetRules({rule});

    // Case 1: Process list does NOT contain important_service.exe
    auto proc_other = std::make_shared<ProcessItem>();
    proc_other->name = L"explorer.exe";
    proc_other->process_id = 1234;

    std::vector<std::shared_ptr<ProcessItem>> procs_without = {proc_other};

    auto events1 = service.CheckProcesses(procs_without);
    Assert::AreEqual(1ULL, events1.size());
    Assert::AreEqual(std::wstring(L"important_service.exe"), events1[0].process_name);
    Assert::AreEqual(EventLevel::kCritical, events1[0].level);

    // Immediate re-check: cooldown should suppress duplicate alert
    auto events2 = service.CheckProcesses(procs_without);
    Assert::AreEqual(0ULL, events2.size());

    // Case 2: Process list DOES contain important_service.exe
    auto proc_target = std::make_shared<ProcessItem>();
    proc_target->name = L"important_service.exe";
    proc_target->process_id = 9999;

    std::vector<std::shared_ptr<ProcessItem>> procs_with = {proc_other, proc_target};
    auto events3 = service.CheckProcesses(procs_with);
    // Process exists, and no resource conditions violated -> 0 events
    Assert::AreEqual(0ULL, events3.size());
  }

  TEST_METHOD(JsonHelper_ParsingAndSerialization) {
    std::wstring sample_json = LR"({
  "Name": "ProcessManager",
  "Version": 2,
  "Active": true,
  "Threshold": 85.5,
  "Items": ["apple", "banana", "日本語テスト"],
  "Nested": {
    "Key": "Value"
  }
})";

    JsonValue val = JsonValue::Parse(sample_json);
    Assert::IsTrue(val.is_object());
    Assert::AreEqual(std::wstring(L"ProcessManager"), val[L"Name"].as_string());
    Assert::AreEqual(2, val[L"Version"].as_int());
    Assert::IsTrue(val[L"Active"].as_bool());
    Assert::AreEqual(85.5, val[L"Threshold"].as_double());
    Assert::IsTrue(val[L"Items"].is_array());
    Assert::AreEqual(3ULL, val[L"Items"].as_array().size());
    Assert::AreEqual(std::wstring(L"日本語テスト"), val[L"Items"].as_array()[2].as_string());
    Assert::AreEqual(std::wstring(L"Value"), val[L"Nested"][L"Key"].as_string());

    // Serialize and re-parse
    std::wstring serialized = val.Serialize(2);
    JsonValue val2 = JsonValue::Parse(serialized);
    Assert::IsTrue(val2.is_object());
    Assert::AreEqual(std::wstring(L"ProcessManager"), val2[L"Name"].as_string());
    Assert::AreEqual(std::wstring(L"日本語テスト"), val2[L"Items"].as_array()[2].as_string());
  }

  TEST_METHOD(AppSettings_JsonSaveAndLoad) {
    std::wstring test_settings_path = L"Test_LPMSettings.json";
    std::wstring test_rules_path = L"Test_LPMMonitorRules.json";

    DeleteFileW(test_settings_path.c_str());
    DeleteFileW(test_rules_path.c_str());

    AppSettings settings;
    settings.theme = AppTheme::kLight;
    settings.list_font_name = L"Segoe UI";
    settings.list_font_size = 10;
    settings.ui_font_name = L"Meiryo";
    settings.ui_font_size = 11;
    settings.refresh_interval_seconds = 5;
    settings.always_on_top = true;

    MonitorRule rule;
    rule.id = L"test_rule_1";
    rule.name = L"テスト高負荷監視";
    rule.level = EventLevel::kCritical;
    rule.cooldown_seconds = 45;

    MonitorCondition cond;
    cond.column_id = ProcessColumnId::kCpu;
    cond.op = ComparisonOperator::kGreaterThanOrEqual;
    cond.numeric_value = 90.0;
    rule.conditions.push_back(cond);

    settings.monitor_rules.push_back(rule);
    settings.excluded_processes = {L"Memory Compression", L"Secure System", L"CustomProc.exe"};

    settings.SaveSettingsTo(test_settings_path);
    settings.SaveMonitorRulesTo(test_rules_path);

    AppSettings loaded = AppSettings::LoadFrom(test_settings_path, test_rules_path);
    Assert::AreEqual(AppTheme::kLight, loaded.theme);
    Assert::AreEqual(std::wstring(L"Segoe UI"), loaded.list_font_name);
    Assert::AreEqual(10, loaded.list_font_size);
    Assert::AreEqual(std::wstring(L"Meiryo"), loaded.ui_font_name);
    Assert::AreEqual(11, loaded.ui_font_size);
    Assert::AreEqual(5, loaded.refresh_interval_seconds);
    Assert::IsTrue(loaded.always_on_top);

    // Verify excluded processes
    Assert::AreEqual(3ULL, loaded.excluded_processes.size());
    Assert::AreEqual(std::wstring(L"Memory Compression"), loaded.excluded_processes[0]);
    Assert::AreEqual(std::wstring(L"Secure System"), loaded.excluded_processes[1]);
    Assert::AreEqual(std::wstring(L"CustomProc.exe"), loaded.excluded_processes[2]);

    bool found_rule = false;
    for (const auto& r : loaded.monitor_rules) {
      if (r.id == L"test_rule_1") {
        found_rule = true;
        Assert::AreEqual(std::wstring(L"テスト高負荷監視"), r.name);
        Assert::AreEqual(EventLevel::kCritical, r.level);
        Assert::AreEqual(45, r.cooldown_seconds);
        Assert::AreEqual(1ULL, r.conditions.size());
        Assert::AreEqual(90.0, r.conditions[0].numeric_value);
        break;
      }
    }
    Assert::IsTrue(found_rule);

    DeleteFileW(test_settings_path.c_str());
    DeleteFileW(test_rules_path.c_str());
  }
};

TEST_CLASS(LanguageManagerTests) {
 public:
  TEST_METHOD(LanguageManager_JapaneseAndEnglishStrings) {
    LanguageManager::SetLanguage(AppLanguage::kJapanese);
    Assert::IsTrue(LanguageManager::IsJapanese());
    Assert::AreEqual(std::wstring(L"LiteProcManager"), std::wstring(LanguageManager::GetString(StringId::kAppTitle)));
    Assert::AreEqual(std::wstring(L"名前"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kName));
    Assert::AreEqual(std::wstring(L"CPU"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kCpu));
    Assert::AreEqual(std::wstring(L"完全一致"), MonitorRule::MatchTypeToString(ProcessMatchType::kExact));
    Assert::AreEqual(std::wstring(L"部分一致"), MonitorRule::MatchTypeToString(ProcessMatchType::kContains));
    Assert::AreEqual(std::wstring(L"前方一致"), MonitorRule::MatchTypeToString(ProcessMatchType::kStartsWith));
    Assert::AreEqual(std::wstring(L"後方一致"), MonitorRule::MatchTypeToString(ProcessMatchType::kEndsWith));
    Assert::AreEqual(std::wstring(L"異常"), MonitorRule::EventLevelToString(EventLevel::kCritical));
    Assert::AreEqual(std::wstring(L"警告"), MonitorRule::EventLevelToString(EventLevel::kWarning));
    Assert::AreEqual(std::wstring(L"含む"), MonitorRule::OperatorToString(ComparisonOperator::kContains));

    ProcessItem proc_ja;
    proc_ja.priority = ProcessPriorityClass::kHigh;
    Assert::AreEqual(std::wstring(L"高"), proc_ja.GetFormattedPriority());
    Assert::AreEqual(std::wstring(L"上へ"), std::wstring(LanguageManager::GetString(StringId::kBtnMoveUp)));
    Assert::AreEqual(std::wstring(L"下へ"), std::wstring(LanguageManager::GetString(StringId::kBtnMoveDown)));
    Assert::AreEqual(std::wstring(L"デフォルト"), std::wstring(LanguageManager::GetString(StringId::kBtnDefault)));
    Assert::AreEqual(std::wstring(L"プロセスの終了 (Delete)"), std::wstring(LanguageManager::GetString(StringId::kTooltipEndProcess)));
    Assert::AreEqual(std::wstring(L"ファイル(&F)"), std::wstring(LanguageManager::GetString(StringId::kMenuFile)));
    Assert::AreEqual(std::wstring(L"🔍 検索:"), std::wstring(LanguageManager::GetString(StringId::kLabelSearch)));
    Assert::AreEqual(std::wstring(L"更新頻度:"), std::wstring(LanguageManager::GetString(StringId::kLabelInterval)));
    Assert::AreEqual(std::wstring(L"一時停止"), std::wstring(LanguageManager::GetString(StringId::kIntervalPause)));
    Assert::AreEqual(std::wstring(L"メモリ (ワーキングセット)"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kWorkingSet));
    Assert::AreEqual(std::wstring(L"含む"), std::wstring(LanguageManager::GetString(StringId::kOpContains)));
    Assert::AreEqual(std::wstring(L"LiteProcManagerを終了しますか？"), std::wstring(LanguageManager::GetString(StringId::kMsgConfirmExit)));
    Assert::AreEqual(std::wstring(L"追加"), std::wstring(LanguageManager::GetString(StringId::kBtnAdd)));
    Assert::AreEqual(std::wstring(L"削除"), std::wstring(LanguageManager::GetString(StringId::kBtnRemove)));
    Assert::AreEqual(std::wstring(L"削除"), std::wstring(LanguageManager::GetString(StringId::kBtnDelete)));

    LanguageManager::SetLanguage(AppLanguage::kEnglish);
    Assert::IsFalse(LanguageManager::IsJapanese());
    Assert::AreEqual(std::wstring(L"LiteProcManager"), std::wstring(LanguageManager::GetString(StringId::kAppTitle)));
    Assert::AreEqual(std::wstring(L"Name"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kName));
    Assert::AreEqual(std::wstring(L"CPU"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kCpu));
    Assert::AreEqual(std::wstring(L"Memory (Working Set)"), LanguageManager::GetColumnHeaderText(ProcessColumnId::kWorkingSet));
    Assert::AreEqual(std::wstring(L"Contains"), std::wstring(LanguageManager::GetString(StringId::kOpContains)));
    Assert::AreEqual(std::wstring(L"Do you want to exit LiteProcManager?"), std::wstring(LanguageManager::GetString(StringId::kMsgConfirmExit)));
    Assert::AreEqual(std::wstring(L"Exact Match"), MonitorRule::MatchTypeToString(ProcessMatchType::kExact));
    Assert::AreEqual(std::wstring(L"Contains"), MonitorRule::MatchTypeToString(ProcessMatchType::kContains));
    Assert::AreEqual(std::wstring(L"Starts With"), MonitorRule::MatchTypeToString(ProcessMatchType::kStartsWith));
    Assert::AreEqual(std::wstring(L"Ends With"), MonitorRule::MatchTypeToString(ProcessMatchType::kEndsWith));
    Assert::AreEqual(std::wstring(L"Critical"), MonitorRule::EventLevelToString(EventLevel::kCritical));
    Assert::AreEqual(std::wstring(L"Warning"), MonitorRule::EventLevelToString(EventLevel::kWarning));
    Assert::AreEqual(std::wstring(L"Contains"), MonitorRule::OperatorToString(ComparisonOperator::kContains));

    ProcessItem proc_en;
    proc_en.priority = ProcessPriorityClass::kHigh;
    Assert::AreEqual(std::wstring(L"High"), proc_en.GetFormattedPriority());
    Assert::AreEqual(std::wstring(L"Move Up"), std::wstring(LanguageManager::GetString(StringId::kBtnMoveUp)));
    Assert::AreEqual(std::wstring(L"Move Down"), std::wstring(LanguageManager::GetString(StringId::kBtnMoveDown)));
    Assert::AreEqual(std::wstring(L"Default"), std::wstring(LanguageManager::GetString(StringId::kBtnDefault)));
    Assert::AreEqual(std::wstring(L"Add"), std::wstring(LanguageManager::GetString(StringId::kBtnAdd)));
    Assert::AreEqual(std::wstring(L"Remove"), std::wstring(LanguageManager::GetString(StringId::kBtnRemove)));
    Assert::AreEqual(std::wstring(L"Remove"), std::wstring(LanguageManager::GetString(StringId::kBtnDelete)));
    Assert::AreEqual(std::wstring(L"Start automatically with Windows(&A)"), std::wstring(LanguageManager::GetString(StringId::kLabelAutoStart)));
    Assert::AreEqual(std::wstring(L"End Process (Delete)"), std::wstring(LanguageManager::GetString(StringId::kTooltipEndProcess)));
    Assert::AreEqual(std::wstring(L"&File"), std::wstring(LanguageManager::GetString(StringId::kMenuFile)));
    Assert::AreEqual(std::wstring(L"🔍 Search:"), std::wstring(LanguageManager::GetString(StringId::kLabelSearch)));
    Assert::AreEqual(std::wstring(L"Interval:"), std::wstring(LanguageManager::GetString(StringId::kLabelInterval)));
    Assert::AreEqual(std::wstring(L"Pause"), std::wstring(LanguageManager::GetString(StringId::kIntervalPause)));
    Assert::AreEqual(std::wstring(L"sec"), std::wstring(LanguageManager::GetString(StringId::kSecondsUnit)));
  }

  TEST_METHOD(LanguageManager_AutoDetect) {
    LanguageManager::SetLanguage(AppLanguage::kAuto);
    Assert::AreEqual(static_cast<int>(AppLanguage::kAuto), static_cast<int>(LanguageManager::GetConfiguredLanguage()));
    // In Japanese Windows OS (CodePage 932), IsJapanese() returns true
    if (GetACP() == 932) {
      Assert::IsTrue(LanguageManager::IsJapanese());
    }
  }

  TEST_METHOD(Version_ShouldNotBeEmptyAndFormatCorrectly) {
    std::wstring ver_str = APP_VERSION_STR;
    Assert::IsFalse(ver_str.empty());
    Assert::IsTrue(ver_str.find(L'.') != std::wstring::npos);
  }

  TEST_METHOD(AutoStart_RegistryOperations) {
    bool initial_state = AppSettings::IsAutoStartConfigured();

    // Test enabling auto start
    bool enable_res = AppSettings::SetAutoStart(true);
    Assert::IsTrue(enable_res);
    Assert::IsTrue(AppSettings::IsAutoStartConfigured());

    // Test disabling auto start
    bool disable_res = AppSettings::SetAutoStart(false);
    Assert::IsTrue(disable_res);
    Assert::IsFalse(AppSettings::IsAutoStartConfigured());

    // Restore initial state
    AppSettings::SetAutoStart(initial_state);
    Assert::AreEqual(initial_state, AppSettings::IsAutoStartConfigured());
  }

  TEST_METHOD(ServiceManagerService_EnumServices_ReturnsItems) {
    ServiceManagerService service;
    auto services = service.GetServicesSnapshot();

    // In a typical Windows system, there are at least dozens of services
    Assert::IsTrue(services.size() > 10);

    // Look for well-known services (RpcSs, EventLog, etc.)
    bool found_rpcss = false;
    for (const auto& s : services) {
      Assert::IsFalse(s->service_name.empty());
      if (_wcsicmp(s->service_name.c_str(), L"RpcSs") == 0) {
        found_rpcss = true;
        Assert::IsFalse(s->display_name.empty());
        Assert::AreEqual(static_cast<DWORD>(SERVICE_RUNNING), s->state);
        Assert::AreEqual(static_cast<DWORD>(SERVICE_AUTO_START), s->start_type);
        break;
      }
    }
    Assert::IsTrue(found_rpcss);
  }

  TEST_METHOD(ServiceItem_StateAndStartTypeStrings_ReturnNonEmpty) {
    ServiceItem item;
    item.service_name = L"TestService";
    item.state = SERVICE_RUNNING;
    item.start_type = SERVICE_AUTO_START;

    std::wstring state_str = item.GetStateString();
    std::wstring start_str = item.GetStartTypeString();

    Assert::IsFalse(state_str.empty());
    Assert::IsFalse(start_str.empty());

    item.state = SERVICE_STOPPED;
    item.start_type = SERVICE_DISABLED;
    Assert::IsFalse(item.GetStateString().empty());
    Assert::IsFalse(item.GetStartTypeString().empty());
  }

  TEST_METHOD(AppSettings_RefreshIntervalSeconds_Range0to30_Persists) {
    std::wstring test_settings_path = L"test_interval_settings.json";
    std::wstring test_rules_path = L"test_interval_rules.json";

    // Test with 0 (pause)
    {
      AppSettings settings;
      settings.refresh_interval_seconds = 0;
      settings.SaveSettingsTo(test_settings_path);

      AppSettings loaded = AppSettings::LoadFrom(test_settings_path, test_rules_path);
      Assert::AreEqual(0, loaded.refresh_interval_seconds);
    }

    // Test with arbitrary value (7 sec)
    {
      AppSettings settings;
      settings.refresh_interval_seconds = 7;
      settings.SaveSettingsTo(test_settings_path);

      AppSettings loaded = AppSettings::LoadFrom(test_settings_path, test_rules_path);
      Assert::AreEqual(7, loaded.refresh_interval_seconds);
    }

    // Test with max gauge value (30 sec)
    {
      AppSettings settings;
      settings.refresh_interval_seconds = 30;
      settings.SaveSettingsTo(test_settings_path);

      AppSettings loaded = AppSettings::LoadFrom(test_settings_path, test_rules_path);
      Assert::AreEqual(30, loaded.refresh_interval_seconds);
    }

    DeleteFileW(test_settings_path.c_str());
    DeleteFileW(test_rules_path.c_str());
  }

  TEST_METHOD(MonitorRule_CooldownSeconds_Range1to60) {
    std::wstring test_rules_path = L"test_cooldown_rules.json";

    std::vector<MonitorRule> rules;
    MonitorRule r1;
    r1.id = L"r1";
    r1.name = L"Min Cooldown";
    r1.cooldown_seconds = 1;
    r1.notify_if_not_found = true;

    MonitorRule r2;
    r2.id = L"r2";
    r2.name = L"Max Cooldown";
    r2.cooldown_seconds = 60;
    r2.notify_if_not_found = false;

    rules.push_back(r1);
    rules.push_back(r2);

    AppSettings s;
    s.monitor_rules = rules;
    s.SaveMonitorRulesTo(test_rules_path);

    std::wstring dummy_settings = L"test_dummy_settings.json";
    AppSettings loaded = AppSettings::LoadFrom(dummy_settings, test_rules_path);

    Assert::AreEqual(2ULL, loaded.monitor_rules.size());
    Assert::AreEqual(1, loaded.monitor_rules[0].cooldown_seconds);
    Assert::IsTrue(loaded.monitor_rules[0].notify_if_not_found);
    Assert::AreEqual(60, loaded.monitor_rules[1].cooldown_seconds);
    Assert::IsFalse(loaded.monitor_rules[1].notify_if_not_found);

    DeleteFileW(test_rules_path.c_str());
  }
};

}  // namespace LiteProcManagerTests
