// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "monitor_dialog.h"

#include <algorithm>
#include <cwchar>
#include <sstream>

#include "language_manager.h"
#include "monitor_service.h"
#include "resource.h"
#include "theme_manager.h"

namespace lite_proc_manager {

namespace {

struct EditDialogContext {
  MonitorRule rule;
  bool is_new{true};
  bool confirmed{false};
  AppTheme theme{AppTheme::kDark};
  HWND hwnd_name{nullptr};
  HWND hwnd_target_type{nullptr};
  HWND hwnd_match_type{nullptr};
  HWND hwnd_pattern{nullptr};
  HWND hwnd_chk_not_found{nullptr};
  HWND hwnd_level{nullptr};
  HWND hwnd_col{nullptr};
  HWND hwnd_op{nullptr};
  HWND hwnd_value{nullptr};
  HWND hwnd_slider_cooldown{nullptr};
  HWND hwnd_lbl_cooldown_val{nullptr};
};

LRESULT CALLBACK RuleEditDialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* ctx = reinterpret_cast<EditDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      ctx = reinterpret_cast<EditDialogContext*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
      SetWindowTextW(hwnd, ctx->is_new ? LanguageManager::GetString(StringId::kRuleEditTitleAdd)
                                       : LanguageManager::GetString(StringId::kRuleEditTitleEdit));
      ThemeManager::ApplyTheme(hwnd, ctx->theme);

      bool is_dark = (ctx->theme == AppTheme::kDark);
      auto apply_ctrl_theme = [&](HWND ctrl) {
        if (ctrl) SetWindowTheme(ctrl, is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      };

      // Set Labels & Buttons
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_NAME, LanguageManager::GetString(StringId::kLabelRuleName));
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_TARGET, LanguageManager::GetString(StringId::kLabelTarget));
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_PATTERN, LanguageManager::GetString(StringId::kPatternLabel));
      SetDlgItemTextW(hwnd, IDC_RULE_CHK_NOT_FOUND, LanguageManager::GetString(StringId::kLabelNotifyIfNotFound));
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_LEVEL, LanguageManager::GetString(StringId::kLabelLevel));
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_CONDITION, LanguageManager::GetString(StringId::kConditionLabel));
      SetDlgItemTextW(hwnd, IDC_RULE_LBL_COOLDOWN, LanguageManager::GetString(StringId::kCooldownLabel));
      SetDlgItemTextW(hwnd, IDOK, LanguageManager::GetString(StringId::kBtnSave));
      SetDlgItemTextW(hwnd, IDCANCEL, LanguageManager::GetString(StringId::kBtnCancel));

      // Get Controls
      ctx->hwnd_name = GetDlgItem(hwnd, IDC_RULE_EDIT_NAME);
      ctx->hwnd_target_type = GetDlgItem(hwnd, IDC_RULE_COMBO_TARGET);
      ctx->hwnd_match_type = GetDlgItem(hwnd, IDC_RULE_COMBO_MATCH);
      ctx->hwnd_pattern = GetDlgItem(hwnd, IDC_RULE_EDIT_PATTERN);
      ctx->hwnd_chk_not_found = GetDlgItem(hwnd, IDC_RULE_CHK_NOT_FOUND);
      ctx->hwnd_level = GetDlgItem(hwnd, IDC_RULE_COMBO_LEVEL);
      ctx->hwnd_col = GetDlgItem(hwnd, IDC_RULE_COMBO_COL);
      ctx->hwnd_op = GetDlgItem(hwnd, IDC_RULE_COMBO_OP);
      ctx->hwnd_value = GetDlgItem(hwnd, IDC_RULE_EDIT_VALUE);
      ctx->hwnd_slider_cooldown = GetDlgItem(hwnd, IDC_RULE_SLIDER_COOLDOWN);
      ctx->hwnd_lbl_cooldown_val = GetDlgItem(hwnd, IDC_RULE_LBL_COOLDOWN_VAL);

      apply_ctrl_theme(ctx->hwnd_target_type);
      apply_ctrl_theme(ctx->hwnd_match_type);
      apply_ctrl_theme(ctx->hwnd_chk_not_found);
      apply_ctrl_theme(ctx->hwnd_level);
      apply_ctrl_theme(ctx->hwnd_col);
      apply_ctrl_theme(ctx->hwnd_op);
      apply_ctrl_theme(ctx->hwnd_slider_cooldown);
      apply_ctrl_theme(GetDlgItem(hwnd, IDOK));
      apply_ctrl_theme(GetDlgItem(hwnd, IDCANCEL));

      // Populate Name & Pattern & Checkbox
      SetWindowTextW(ctx->hwnd_name, ctx->rule.name.c_str());
      SetWindowTextW(ctx->hwnd_pattern, ctx->rule.target_pattern.c_str());
      SendMessageW(ctx->hwnd_chk_not_found, BM_SETCHECK,
                   ctx->rule.notify_if_not_found ? BST_CHECKED : BST_UNCHECKED, 0);

      // Target Type
      SendMessageW(ctx->hwnd_target_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchTargetProcessName)));
      SendMessageW(ctx->hwnd_target_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchTargetPid)));
      SendMessageW(ctx->hwnd_target_type, CB_SETCURSEL, ctx->rule.match_target == ProcessMatchTarget::kPid ? 1 : 0, 0);

      // Match Type
      SendMessageW(ctx->hwnd_match_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchExact)));
      SendMessageW(ctx->hwnd_match_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchContains)));
      SendMessageW(ctx->hwnd_match_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchStartsWith)));
      SendMessageW(ctx->hwnd_match_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kMatchEndsWith)));
      int match_idx = 1;
      if (ctx->rule.match_type == ProcessMatchType::kExact) match_idx = 0;
      else if (ctx->rule.match_type == ProcessMatchType::kContains) match_idx = 1;
      else if (ctx->rule.match_type == ProcessMatchType::kStartsWith) match_idx = 2;
      else if (ctx->rule.match_type == ProcessMatchType::kEndsWith) match_idx = 3;
      SendMessageW(ctx->hwnd_match_type, CB_SETCURSEL, match_idx, 0);

      // Level
      SendMessageW(ctx->hwnd_level, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kLevelWarning)));
      SendMessageW(ctx->hwnd_level, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kLevelCritical)));
      SendMessageW(ctx->hwnd_level, CB_SETCURSEL, ctx->rule.level == EventLevel::kCritical ? 1 : 0, 0);

      // Column Select - Populate all numeric process columns
      SendMessageW(ctx->hwnd_col, CB_RESETCONTENT, 0, 0);
      const auto& numeric_cols = MonitorRule::GetNumericRuleColumnIds();
      ProcessColumnId current_col = ctx->rule.conditions.empty() ? ProcessColumnId::kCpu : ctx->rule.conditions[0].column_id;
      int col_idx = 0;

      for (size_t i = 0; i < numeric_cols.size(); ++i) {
        ProcessColumnId cid = numeric_cols[i];
        std::wstring col_name = LanguageManager::GetColumnHeaderText(cid);
        if (MonitorRule::IsBytesColumn(cid)) {
          col_name += L" (K)";
        } else if (cid == ProcessColumnId::kCpu || cid == ProcessColumnId::kGpuUsage) {
          col_name += L" (%)";
        }

        int idx = static_cast<int>(SendMessageW(ctx->hwnd_col, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(col_name.c_str())));
        SendMessageW(ctx->hwnd_col, CB_SETITEMDATA, idx, static_cast<LPARAM>(cid));

        if (cid == current_col) {
          col_idx = idx;
        }
      }
      SendMessageW(ctx->hwnd_col, CB_SETCURSEL, col_idx, 0);
      SendMessageW(ctx->hwnd_col, CB_SETDROPPEDWIDTH, 192, 0);

      double init_val = 80.0;
      ComparisonOperator init_op = ComparisonOperator::kGreaterThanOrEqual;

      if (!ctx->rule.conditions.empty()) {
        const auto& cond = ctx->rule.conditions[0];
        init_op = cond.op;
        if (MonitorRule::IsBytesColumn(cond.column_id)) {
          init_val = cond.numeric_value / 1024.0; // Bytes to K (KiB)
        } else {
          init_val = cond.numeric_value;
        }
      }

      // Operator Select
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L">="));
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L">"));
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"<="));
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"<"));
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"=="));
      SendMessageW(ctx->hwnd_op, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"!="));

      int op_idx = 0;
      if (init_op == ComparisonOperator::kGreaterThanOrEqual) op_idx = 0;
      else if (init_op == ComparisonOperator::kGreaterThan) op_idx = 1;
      else if (init_op == ComparisonOperator::kLessThanOrEqual) op_idx = 2;
      else if (init_op == ComparisonOperator::kLessThan) op_idx = 3;
      else if (init_op == ComparisonOperator::kEqual) op_idx = 4;
      else if (init_op == ComparisonOperator::kNotEqual) op_idx = 5;
      SendMessageW(ctx->hwnd_op, CB_SETCURSEL, op_idx, 0);

      // Threshold Value
      std::wostringstream woss;
      woss << init_val;
      SetWindowTextW(ctx->hwnd_value, woss.str().c_str());

      // Cooldown Slider (1-60 seconds)
      SendMessageW(ctx->hwnd_slider_cooldown, TBM_SETRANGE, TRUE, MAKELPARAM(1, 60));
      SendMessageW(ctx->hwnd_slider_cooldown, TBM_SETTICFREQ, 5, 0);
      int cd_val = std::clamp(ctx->rule.cooldown_seconds, 1, 60);
      SendMessageW(ctx->hwnd_slider_cooldown, TBM_SETPOS, TRUE, cd_val);
      std::wstring cd_text = std::to_wstring(cd_val) + L" " + LanguageManager::GetString(StringId::kSecondsUnit);
      SetWindowTextW(ctx->hwnd_lbl_cooldown_val, cd_text.c_str());

      return TRUE;
    }

    case WM_HSCROLL: {
      if (ctx && reinterpret_cast<HWND>(lparam) == ctx->hwnd_slider_cooldown) {
        int pos = static_cast<int>(SendMessageW(ctx->hwnd_slider_cooldown, TBM_GETPOS, 0, 0));
        std::wstring scroll_text = std::to_wstring(pos) + L" " + LanguageManager::GetString(StringId::kSecondsUnit);
        SetWindowTextW(ctx->hwnd_lbl_cooldown_val, scroll_text.c_str());
        return TRUE;
      }
      break;
    }

    case WM_COMMAND: {
      int id = LOWORD(wparam);
      if (id == IDOK) {
        wchar_t buf[256] = {0};
        GetWindowTextW(ctx->hwnd_name, buf, 256);
        ctx->rule.name = buf;
        if (ctx->rule.name.empty()) {
          MessageBoxW(hwnd, LanguageManager::GetString(StringId::kMsgRuleNameEmpty),
                      LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONWARNING);
          return TRUE;
        }

        int target_sel = static_cast<int>(SendMessageW(ctx->hwnd_target_type, CB_GETCURSEL, 0, 0));
        ctx->rule.match_target = (target_sel == 1) ? ProcessMatchTarget::kPid : ProcessMatchTarget::kProcessName;

        int match_sel = static_cast<int>(SendMessageW(ctx->hwnd_match_type, CB_GETCURSEL, 0, 0));
        if (match_sel == 0) ctx->rule.match_type = ProcessMatchType::kExact;
        else if (match_sel == 1) ctx->rule.match_type = ProcessMatchType::kContains;
        else if (match_sel == 2) ctx->rule.match_type = ProcessMatchType::kStartsWith;
        else if (match_sel == 3) ctx->rule.match_type = ProcessMatchType::kEndsWith;

        GetWindowTextW(ctx->hwnd_pattern, buf, 256);
        ctx->rule.target_pattern = buf;

        int level_sel = static_cast<int>(SendMessageW(ctx->hwnd_level, CB_GETCURSEL, 0, 0));
        ctx->rule.level = (level_sel == 1) ? EventLevel::kCritical : EventLevel::kWarning;

        // Condition
        MonitorCondition cond;
        int col_sel = static_cast<int>(SendMessageW(ctx->hwnd_col, CB_GETCURSEL, 0, 0));
        ProcessColumnId selected_col = ProcessColumnId::kCpu;
        if (col_sel >= 0) {
          LRESULT data = SendMessageW(ctx->hwnd_col, CB_GETITEMDATA, col_sel, 0);
          if (data != CB_ERR) {
            selected_col = static_cast<ProcessColumnId>(data);
          }
        }
        cond.column_id = selected_col;

        int op_sel = static_cast<int>(SendMessageW(ctx->hwnd_op, CB_GETCURSEL, 0, 0));
        if (op_sel == 0) cond.op = ComparisonOperator::kGreaterThanOrEqual;
        else if (op_sel == 1) cond.op = ComparisonOperator::kGreaterThan;
        else if (op_sel == 2) cond.op = ComparisonOperator::kLessThanOrEqual;
        else if (op_sel == 3) cond.op = ComparisonOperator::kLessThan;
        else if (op_sel == 4) cond.op = ComparisonOperator::kEqual;
        else if (op_sel == 5) cond.op = ComparisonOperator::kNotEqual;

        GetWindowTextW(ctx->hwnd_value, buf, 256);
        double val = _wtof(buf);
        if (MonitorRule::IsBytesColumn(selected_col)) {
          val = val * 1024.0; // K (KiB) to Bytes
        }
        cond.numeric_value = val;

        ctx->rule.conditions.clear();
        ctx->rule.conditions.push_back(cond);
        ctx->rule.logical_op = LogicalOperator::kAnd;

        int cd = static_cast<int>(SendMessageW(ctx->hwnd_slider_cooldown, TBM_GETPOS, 0, 0));
        ctx->rule.cooldown_seconds = std::clamp(cd, 1, 60);

        ctx->rule.notify_if_not_found =
            (SendMessageW(ctx->hwnd_chk_not_found, BM_GETCHECK, 0, 0) == BST_CHECKED);

        ctx->confirmed = true;
        EndDialog(hwnd, IDOK);
        return TRUE;
      } else if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      }
      break;
    }

    case WM_CTLCOLORDLG: {
      const auto& palette = ThemeManager::GetPalette(ctx->theme);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(ctx->theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkMode(hdc, TRANSPARENT);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(ctx->theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.control_background);
      return reinterpret_cast<INT_PTR>(palette.control_brush);
    }
  }
  return FALSE;
}

bool ShowRuleEditModal(HWND parent, MonitorRule* in_out_rule, bool is_new, AppTheme theme) {
  EditDialogContext ctx;
  ctx.rule = *in_out_rule;
  ctx.is_new = is_new;
  ctx.theme = theme;

  INT_PTR res = DialogBoxParamW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULE_EDIT_DIALOG), parent,
      reinterpret_cast<DLGPROC>(RuleEditDialogProc), reinterpret_cast<LPARAM>(&ctx));

  if (res == IDOK && ctx.confirmed) {
    *in_out_rule = ctx.rule;
    return true;
  }
  return false;
}
}  // namespace

MonitorDialog::MonitorDialog(HWND parent_hwnd, const std::vector<MonitorRule>& rules, AppTheme theme)
    : parent_hwnd_(parent_hwnd), rules_(rules), theme_(theme) {}

bool MonitorDialog::Show() {
  INT_PTR res = DialogBoxParamW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_MONITOR_DIALOG), parent_hwnd_,
      reinterpret_cast<DLGPROC>(DialogProc), reinterpret_cast<LPARAM>(this));

  return (res == IDOK && confirmed_);
}

bool MonitorDialog::ShowAddRuleForProcess(
    HWND parent_hwnd, const std::wstring& process_name, uint32_t pid,
    std::vector<MonitorRule>* in_out_rules, AppTheme theme) {
  MonitorRule new_rule;
  new_rule.name = process_name;
  new_rule.match_target = ProcessMatchTarget::kProcessName;
  new_rule.match_type = ProcessMatchType::kExact;
  new_rule.target_pattern = process_name;

  MonitorCondition cond;
  cond.column_id = ProcessColumnId::kCpu;
  cond.op = ComparisonOperator::kGreaterThanOrEqual;
  cond.numeric_value = 80.0;
  new_rule.conditions.push_back(cond);

  if (ShowRuleEditModal(parent_hwnd, &new_rule, true, theme)) {
    in_out_rules->push_back(new_rule);
    return true;
  }
  return false;
}

LRESULT CALLBACK MonitorDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  MonitorDialog* self = nullptr;
  if (msg == WM_INITDIALOG) {
    self = reinterpret_cast<MonitorDialog*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->dlg_hwnd_ = hwnd;
    self->InitializeDialog(hwnd);
    return TRUE;
  } else {
    self = reinterpret_cast<MonitorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return FALSE;
}

void MonitorDialog::InitializeDialog(HWND hwnd) {
  SetWindowTextW(hwnd, LanguageManager::GetString(StringId::kDlgMonitorTitle));
  ThemeManager::ApplyTheme(hwnd, theme_);

  bool is_dark = (theme_ == AppTheme::kDark);
  const auto& palette = ThemeManager::GetPalette(theme_);

  // Set Button Labels
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_ADD, LanguageManager::GetString(StringId::kBtnAdd));
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_EDIT, LanguageManager::GetString(StringId::kBtnEdit));
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_DELETE, LanguageManager::GetString(StringId::kBtnRemove));
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_TOGGLE, LanguageManager::GetString(StringId::kBtnToggle));
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_TEST_WARN, LanguageManager::GetString(StringId::kBtnTestWarn));
  SetDlgItemTextW(hwnd, IDC_MONITOR_BTN_TEST_ERR, LanguageManager::GetString(StringId::kBtnTestErr));
  SetDlgItemTextW(hwnd, IDOK, LanguageManager::GetString(StringId::kBtnOk));

  // Get Control Handles
  listview_hwnd_ = GetDlgItem(hwnd, IDC_MONITOR_LIST);
  btn_add_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_ADD);
  btn_edit_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_EDIT);
  btn_delete_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_DELETE);
  btn_toggle_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_TOGGLE);
  btn_test_warn_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_TEST_WARN);
  btn_test_err_ = GetDlgItem(hwnd, IDC_MONITOR_BTN_TEST_ERR);
  btn_ok_ = GetDlgItem(hwnd, IDOK);

  auto apply_btn_theme = [&](HWND btn) {
    if (btn) SetWindowTheme(btn, is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  };
  apply_btn_theme(btn_add_);
  apply_btn_theme(btn_edit_);
  apply_btn_theme(btn_delete_);
  apply_btn_theme(btn_toggle_);
  apply_btn_theme(btn_test_warn_);
  apply_btn_theme(btn_test_err_);
  apply_btn_theme(btn_ok_);

  // Setup ListView
  ListView_SetExtendedListViewStyle(listview_hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  SetWindowTheme(listview_hwnd_, is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  ListView_SetBkColor(listview_hwnd_, palette.control_background);
  ListView_SetTextBkColor(listview_hwnd_, palette.control_background);
  ListView_SetTextColor(listview_hwnd_, palette.text_primary);

  HWND header = ListView_GetHeader(listview_hwnd_);
  if (header) {
    SetWindowTheme(header, is_dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
  }

  LVCOLUMNW lvc = {0};
  lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  lvc.cx = 60; lvc.pszText = const_cast<wchar_t*>(LanguageManager::GetString(StringId::kColStatus)); ListView_InsertColumn(listview_hwnd_, 0, &lvc);
  lvc.cx = 120; lvc.pszText = const_cast<wchar_t*>(LanguageManager::GetString(StringId::kColRuleName)); ListView_InsertColumn(listview_hwnd_, 1, &lvc);
  lvc.cx = 60; lvc.pszText = const_cast<wchar_t*>(LanguageManager::GetString(StringId::kColLevel)); ListView_InsertColumn(listview_hwnd_, 2, &lvc);
  lvc.cx = 110; lvc.pszText = const_cast<wchar_t*>(LanguageManager::GetString(StringId::kColTarget)); ListView_InsertColumn(listview_hwnd_, 3, &lvc);
  lvc.cx = 140; lvc.pszText = const_cast<wchar_t*>(LanguageManager::GetString(StringId::kColCondition)); ListView_InsertColumn(listview_hwnd_, 4, &lvc);

  ThemeManager::ApplyListViewHeaderTheme(listview_hwnd_, theme_);

  RefreshRuleList();
}

std::vector<int> MonitorDialog::GetSelectedRuleIndices() const {
  std::vector<int> indices;
  int idx = -1;
  while ((idx = ListView_GetNextItem(listview_hwnd_, idx, LVNI_SELECTED)) != -1) {
    if (idx >= 0 && idx < static_cast<int>(rules_.size())) {
      indices.push_back(idx);
    }
  }
  return indices;
}

void MonitorDialog::UpdateButtonsState() {
  auto sel = GetSelectedRuleIndices();
  bool has_sel = !sel.empty();
  bool single_sel = (sel.size() == 1);

  if (btn_edit_) EnableWindow(btn_edit_, single_sel ? TRUE : FALSE);
  if (btn_toggle_) EnableWindow(btn_toggle_, has_sel ? TRUE : FALSE);
  if (btn_delete_) EnableWindow(btn_delete_, has_sel ? TRUE : FALSE);
  if (btn_test_warn_) EnableWindow(btn_test_warn_, has_sel ? TRUE : FALSE);
  if (btn_test_err_) EnableWindow(btn_test_err_, has_sel ? TRUE : FALSE);
}

void MonitorDialog::RefreshRuleList() {
  ListView_DeleteAllItems(listview_hwnd_);

  for (size_t i = 0; i < rules_.size(); ++i) {
    const auto& rule = rules_[i];

    LVITEMW lvi = {0};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    lvi.iItem = static_cast<int>(i);
    lvi.iSubItem = 0;
    std::wstring status_str = rule.enabled ? LanguageManager::GetString(StringId::kStatusEnabled)
                                           : LanguageManager::GetString(StringId::kStatusDisabled);
    lvi.pszText = const_cast<wchar_t*>(status_str.c_str());
    lvi.lParam = static_cast<LPARAM>(i);

    ListView_InsertItem(listview_hwnd_, &lvi);

    ListView_SetItemText(listview_hwnd_, i, 1, const_cast<wchar_t*>(rule.name.c_str()));
    std::wstring level_str = MonitorRule::EventLevelToString(rule.level);
    ListView_SetItemText(listview_hwnd_, i, 2, const_cast<wchar_t*>(level_str.c_str()));

    std::wstring target_label = (rule.match_target == ProcessMatchTarget::kPid)
                                    ? LanguageManager::GetString(StringId::kMatchTargetPid)
                                    : LanguageManager::GetString(StringId::kMatchTargetProcessName);
    std::wstring target_str = target_label + L": " + rule.target_pattern + L" (" +
                              MonitorRule::MatchTypeToString(rule.match_type) + L")";
    ListView_SetItemText(listview_hwnd_, i, 3, const_cast<wchar_t*>(target_str.c_str()));

    std::wstring cond_str;
    for (size_t c = 0; c < rule.conditions.size(); ++c) {
      if (c > 0) cond_str += (rule.logical_op == LogicalOperator::kOr) ? L" || " : L" && ";
      cond_str += rule.conditions[c].ToString();
    }
    ListView_SetItemText(listview_hwnd_, i, 4, const_cast<wchar_t*>(cond_str.c_str()));
  }

  UpdateButtonsState();
}

void MonitorDialog::OnAddRule() {
  MonitorRule new_rule;
  new_rule.name = L"";
  new_rule.target_pattern = L"";
  new_rule.cooldown_seconds = 30;

  if (ShowRuleEditModal(dlg_hwnd_, &new_rule, true, theme_)) {
    rules_.push_back(new_rule);
    RefreshRuleList();
  }
}

void MonitorDialog::OnEditRule() {
  auto sel = GetSelectedRuleIndices();
  if (sel.size() != 1) return;

  if (ShowRuleEditModal(dlg_hwnd_, &rules_[sel[0]], false, theme_)) {
    RefreshRuleList();
  }
}

void MonitorDialog::OnDeleteRule() {
  auto sel = GetSelectedRuleIndices();
  if (sel.empty()) return;

  wchar_t prompt_buf[512];
  if (sel.size() == 1) {
    swprintf_s(prompt_buf, LanguageManager::GetString(StringId::kConfirmDelete), rules_[sel[0]].name.c_str());
  } else {
    swprintf_s(prompt_buf, LanguageManager::GetString(StringId::kConfirmDeleteMultiple), static_cast<int>(sel.size()));
  }

  if (MessageBoxW(dlg_hwnd_, prompt_buf, LanguageManager::GetString(StringId::kTitleNotice),
                  MB_YESNO | MB_ICONQUESTION) == IDYES) {
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    for (int idx : sel) {
      if (idx >= 0 && idx < static_cast<int>(rules_.size())) {
        rules_.erase(rules_.begin() + idx);
      }
    }
    RefreshRuleList();
  }
}

void MonitorDialog::OnToggleRuleEnabled() {
  auto sel = GetSelectedRuleIndices();
  if (sel.empty()) return;

  for (int idx : sel) {
    if (idx >= 0 && idx < static_cast<int>(rules_.size())) {
      rules_[idx].enabled = !rules_[idx].enabled;
    }
  }
  RefreshRuleList();
}

void MonitorDialog::OnTestRules(EventLevel level) {
  auto sel = GetSelectedRuleIndices();
  if (sel.empty()) return;

  for (int idx : sel) {
    if (idx < 0 || idx >= static_cast<int>(rules_.size())) continue;
    const auto& rule = rules_[idx];

    std::wstring level_text = (level == EventLevel::kCritical)
                                  ? LanguageManager::GetString(StringId::kLevelCritical)
                                  : LanguageManager::GetString(StringId::kLevelWarning);
    std::wstring title = L"[" + level_text + L"] " +
                         (rule.name.empty() ? LanguageManager::GetString(StringId::kDlgMonitorTitle) : rule.name);

    std::wstring target_label = (rule.match_target == ProcessMatchTarget::kPid)
                                    ? LanguageManager::GetString(StringId::kMatchTargetPid)
                                    : LanguageManager::GetString(StringId::kMatchTargetProcessName);
    std::wstring msg = target_label + L": " +
                       (rule.target_pattern.empty() ? L"-" : rule.target_pattern);

    if (!rule.conditions.empty()) {
      msg += L" | " + rule.conditions[0].ToString();
    }

    MonitorService::ShowBalloonNotification(parent_hwnd_, 1, level, title, msg);
  }
}

LRESULT MonitorDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_COMMAND: {
      int id = LOWORD(wparam);
      switch (id) {
        case IDC_MONITOR_BTN_ADD:
          OnAddRule();
          break;
        case IDC_MONITOR_BTN_EDIT:
          OnEditRule();
          break;
        case IDC_MONITOR_BTN_DELETE:
          OnDeleteRule();
          break;
        case IDC_MONITOR_BTN_TOGGLE:
          OnToggleRuleEnabled();
          break;
        case IDC_MONITOR_BTN_TEST_WARN:
          OnTestRules(EventLevel::kWarning);
          break;
        case IDC_MONITOR_BTN_TEST_ERR:
          OnTestRules(EventLevel::kCritical);
          break;
        case IDOK:
          confirmed_ = true;
          EndDialog(hwnd, IDOK);
          break;
        case IDCANCEL:
          confirmed_ = false;
          EndDialog(hwnd, IDCANCEL);
          break;
      }
      return TRUE;
    }

    case WM_NOTIFY: {
      auto* nmhdr = reinterpret_cast<NMHDR*>(lparam);
      if (nmhdr->hwndFrom == listview_hwnd_) {
        if (nmhdr->code == LVN_ITEMCHANGED) {
          UpdateButtonsState();
        } else if (nmhdr->code == NM_DBLCLK) {
          OnEditRule();
          return TRUE;
        }
      }

      HWND header = ListView_GetHeader(listview_hwnd_);
      if (header && nmhdr->hwndFrom == header && nmhdr->code == NM_CUSTOMDRAW) {
        auto* nmcd = reinterpret_cast<NMCUSTOMDRAW*>(lparam);
        if (theme_ == AppTheme::kDark) {
          switch (nmcd->dwDrawStage) {
            case CDDS_PREPAINT:
              SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
              return TRUE;

            case CDDS_ITEMPREPAINT: {
              const auto& palette = ThemeManager::GetPalette(theme_);
              SetTextColor(nmcd->hdc, palette.text_primary);
              SetBkColor(nmcd->hdc, palette.header_background);
              SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_NOTIFYPOSTPAINT);
              return TRUE;
            }

            case CDDS_ITEMPOSTPAINT: {
              int item_idx = static_cast<int>(nmcd->dwItemSpec);
              wchar_t text[128] = {0};
              HDITEMW hdi = {0};
              hdi.mask = HDI_TEXT;
              hdi.pszText = text;
              hdi.cchTextMax = 128;
              Header_GetItem(header, item_idx, &hdi);

              if (text[0] != L'\0') {
                RECT rc = nmcd->rc;
                rc.left += 6;
                SetBkMode(nmcd->hdc, TRANSPARENT);
                SetTextColor(nmcd->hdc, RGB(245, 245, 245));
                DrawTextW(nmcd->hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
              }
              SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, CDRF_DODEFAULT);
              return TRUE;
            }
          }
        }
      }
      break;
    }

    case WM_CTLCOLORDLG: {
      const auto& palette = ThemeManager::GetPalette(theme_);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(theme_);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.window_background);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(theme_);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.control_background);
      return reinterpret_cast<INT_PTR>(palette.control_brush);
    }
  }
  return FALSE;
}

}  // namespace lite_proc_manager
