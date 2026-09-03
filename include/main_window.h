// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_MAIN_WINDOW_H_
#define LITE_PROC_MANAGER_MAIN_WINDOW_H_

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app_settings.h"
#include "icon_helper.h"
#include "monitor_service.h"
#include "process_item.h"
#include "process_snapshot_service.h"
#include "service_item.h"
#include "service_manager_service.h"

namespace lite_proc_manager {

enum class MainTab {
  kProcesses = 0,
  kServices = 1,
};

class MainWindow {
 public:
  MainWindow();
  ~MainWindow();

  bool Create(HINSTANCE instance, int cmd_show);
  int RunMessageLoop();

  bool IsDarkMode() const { return false; }

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  void InitializeComponents();
  void RebuildListViewColumns();
  void PopulateIntervalComboBox();
  void UpdateTimerInterval();
  void SetControlTooltip(HWND control_hwnd, const std::wstring& text);
  void RefreshData();
  void UpdateStatusLabels();
  void ApplyConditionFilter();
  void OnFilterConditionChanged();
  void ApplyFilterAndDisplay();
  void UpdateListView();
  void UpdateTreeView();
  void AddTreeNode(HTREEITEM parent_node, const std::shared_ptr<ProcessItem>& process);
  void SortItems();

  std::shared_ptr<ProcessItem> GetSelectedProcess();
  std::vector<std::shared_ptr<ProcessItem>> GetSelectedProcesses();
  void EndSelectedProcess();
  void EndSelectedProcessTree();
  void SetSelectedProcessPriority(ProcessPriorityClass priority);
  void OpenSelectedFileLocation();
  void SearchSelectedProcessOnline();
  void ShowSelectedProcessProperties();
  void OpenMonitorSettings();
  void OpenOptionsDialog();
  void ShowAboutDialog();
  void ApplyTheme();
  void RestartApplication();
  void RestartAsAdministrator();
  static bool IsCurrentProcessElevated();
  void UpdateLanguageAndUI();
  void AddSelectedProcessToMonitor();
  void CopySelectedInfo(ProcessColumnId col_id);
  void CopySelectedAsJson();
  void CopySelectedAsTsv();
  void SetClipboardText(const std::wstring& text);

  void ShowContextMenu(int x, int y);
  void ShowTrayContextMenu();
  void MinimizeToTray();
  void RestoreFromTray() const;
  void ResizeChildren(int width, int height);

  // Tab and Services Management
  void OnTabChanged();
  void RebuildServiceListViewColumns();
  void RefreshServicesData();
  void ApplyServiceFilterAndDisplay();
  void UpdateServiceListView();
  void SortServices();
  std::shared_ptr<ServiceItem> GetSelectedService();
  void StartSelectedService();
  void StopSelectedService();
  void RestartSelectedService();
  void ChangeSelectedServiceStartupType(DWORD start_type);
  void ShowServiceContextMenu(int x, int y);
  void GoToSelectedServiceProcess();
  void GoToProcessRelatedServices();

  HWND hwnd_{nullptr};
  HINSTANCE instance_{nullptr};

  // Controls
  HWND search_edit_{nullptr};
  HWND interval_combo_{nullptr};
  HWND btn_tree_{nullptr};
  HWND btn_columns_{nullptr};
  HWND btn_restart_admin_{nullptr};
  HWND btn_topmost_{nullptr};
  HWND btn_options_{nullptr};
  HWND btn_monitor_{nullptr};
  HWND btn_refresh_{nullptr};
  HWND btn_endtask_{nullptr};

  // Condition Filter Controls
  HWND filter_col_combo_{nullptr};
  HWND filter_op_combo_{nullptr};
  HWND filter_val_edit_{nullptr};
  HWND filter_clear_btn_{nullptr};

  // Tab & Views
  HWND tab_control_{nullptr};
  HWND listview_hwnd_{nullptr};
  HWND treeview_hwnd_{nullptr};
  HWND service_list_view_{nullptr};
  HWND statusbar_hwnd_{nullptr};
  HWND tooltip_hwnd_{nullptr};

  NOTIFYICONDATAW notify_icon_data_{0};
  HMENU context_menu_{nullptr};
  HMENU tray_menu_{nullptr};
  HMENU service_context_menu_{nullptr};

  // Services & State
  AppSettings settings_;
  std::optional<AppTheme> pending_theme_change_;
  ProcessSnapshotService snapshot_service_;
  MonitorService monitor_service_;
  ServiceManagerService service_manager_service_;
  IconHelper icon_helper_;
  bool is_elevated_{false};
  MainTab current_tab_{MainTab::kProcesses};

  std::vector<std::shared_ptr<ProcessItem>> all_processes_;
  std::vector<std::shared_ptr<ProcessItem>> display_processes_;
  std::vector<std::shared_ptr<ProcessItem>> filtered_processes_;
  SystemTotals totals_;

  int sort_column_index_{0};
  bool sort_ascending_{true};

  std::vector<std::shared_ptr<ServiceItem>> all_services_;
  std::vector<std::shared_ptr<ServiceItem>> filtered_services_;
  int service_sort_column_{0};
  bool service_sort_ascending_{true};

  HBRUSH search_active_brush_{nullptr};
  HFONT list_font_{nullptr};
  HFONT ui_font_{nullptr};

  // Colorful Toolbar Icons
  HICON hicon_tree_{nullptr};
  HICON hicon_list_{nullptr};
  HICON hicon_columns_{nullptr};
  HICON hicon_shield_{nullptr};
  HICON hicon_topmost_{nullptr};
  HICON hicon_topmost_off_{nullptr};
  HICON hicon_options_{nullptr};
  HICON hicon_monitor_{nullptr};
  HICON hicon_refresh_{nullptr};
  HICON hicon_endtask_{nullptr};
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_MAIN_WINDOW_H_
