// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "options_dialog.h"

#include <algorithm>
#include <commdlg.h>
#include <cwchar>
#include <sstream>

#include "language_manager.h"
#include "resource.h"
#include "theme_manager.h"

#pragma comment(lib, "comdlg32.lib")

namespace lite_proc_manager {

OptionsDialog::OptionsDialog(HWND parent_hwnd, const AppSettings& settings)
    : parent_hwnd_(parent_hwnd), settings_(settings) {}

bool OptionsDialog::Show() {
  INT_PTR res = DialogBoxParamW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_OPTIONS_DIALOG), parent_hwnd_,
      reinterpret_cast<DLGPROC>(DialogProc), reinterpret_cast<LPARAM>(this));

  return (res == IDOK && confirmed_);
}

LRESULT CALLBACK OptionsDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  OptionsDialog* self = nullptr;
  if (msg == WM_INITDIALOG) {
    self = reinterpret_cast<OptionsDialog*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->dlg_hwnd_ = hwnd;
    self->InitializeDialog(hwnd);
    return TRUE;
  } else {
    self = reinterpret_cast<OptionsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return FALSE;
}

namespace {
std::wstring TrimString(const std::wstring& s) {
  auto start = s.find_first_not_of(L" \t\r\n");
  if (start == std::wstring::npos) return L"";
  auto end = s.find_last_not_of(L" \t\r\n");
  return s.substr(start, end - start + 1);
}

struct AddExcludedDialogContext {
  std::wstring result_name;
  AppTheme theme{AppTheme::kLight};
};

INT_PTR CALLBACK AddExcludedProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* ctx = reinterpret_cast<AddExcludedDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_INITDIALOG: {
      ctx = reinterpret_cast<AddExcludedDialogContext*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
      SetWindowTextW(hwnd, LanguageManager::GetString(StringId::kDlgAddExcludedTitle));
      SetDlgItemTextW(hwnd, IDC_ADD_EXCL_PROMPT, LanguageManager::GetString(StringId::kLabelAddExcludedPrompt));
      SetDlgItemTextW(hwnd, IDOK, LanguageManager::GetString(StringId::kBtnOk));
      SetDlgItemTextW(hwnd, IDCANCEL, LanguageManager::GetString(StringId::kBtnCancel));
      ThemeManager::ApplyTheme(hwnd, ctx->theme);
      bool is_dark = (ctx->theme == AppTheme::kDark);
      SetWindowTheme(GetDlgItem(hwnd, IDC_ADD_EXCL_EDIT), is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      SetWindowTheme(GetDlgItem(hwnd, IDOK), is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      SetWindowTheme(GetDlgItem(hwnd, IDCANCEL), is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      SetFocus(GetDlgItem(hwnd, IDC_ADD_EXCL_EDIT));
      return FALSE;
    }
    case WM_COMMAND: {
      int id = LOWORD(wparam);
      if (id == IDOK) {
        wchar_t buf[260] = {0};
        GetDlgItemTextW(hwnd, IDC_ADD_EXCL_EDIT, buf, static_cast<int>(std::size(buf)));
        if (ctx) ctx->result_name = TrimString(buf);
        EndDialog(hwnd, IDOK);
        return TRUE;
      } else if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      }
      break;
    }
    case WM_CTLCOLORDLG: {
      if (ctx) {
        const auto& palette = ThemeManager::GetPalette(ctx->theme);
        return reinterpret_cast<INT_PTR>(palette.window_brush);
      }
      break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      if (ctx) {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        const auto& palette = ThemeManager::GetPalette(ctx->theme);
        SetTextColor(hdc, palette.text_primary);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(palette.window_brush);
      }
      break;
    }
    case WM_CTLCOLOREDIT: {
      if (ctx) {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        const auto& palette = ThemeManager::GetPalette(ctx->theme);
        SetTextColor(hdc, palette.text_primary);
        SetBkColor(hdc, palette.control_background);
        return reinterpret_cast<INT_PTR>(palette.control_brush);
      }
      break;
    }
  }
  return FALSE;
}

bool PromptAddExcludedProcess(HWND parent, AppTheme theme, std::wstring* out_name) {
  AddExcludedDialogContext ctx;
  ctx.theme = theme;
  INT_PTR res = DialogBoxParamW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD_EXCLUDED_DIALOG), parent,
      AddExcludedProc, reinterpret_cast<LPARAM>(&ctx));
  if (res == IDOK && !ctx.result_name.empty()) {
    if (out_name) *out_name = std::move(ctx.result_name);
    return true;
  }
  return false;
}
}  // namespace

void OptionsDialog::InitializeDialog(HWND hwnd) {
  SetWindowTextW(hwnd, LanguageManager::GetString(StringId::kDlgOptionsTitle));
  ThemeManager::ApplyTheme(hwnd, settings_.theme);

  bool is_dark = (settings_.theme == AppTheme::kDark);
  auto apply_ctrl_theme = [&](HWND ctrl) {
    if (ctrl) {
      SetWindowTheme(ctrl, is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
  };

  // Localize Static Labels and Buttons
  SetDlgItemTextW(hwnd, IDC_OPT_LBL_LANG, LanguageManager::GetString(StringId::kLabelLanguage));
  SetDlgItemTextW(hwnd, IDC_OPT_LBL_LIST_FONT, LanguageManager::GetString(StringId::kLabelListFont));
  SetDlgItemTextW(hwnd, IDC_OPT_BTN_LIST_FONT, LanguageManager::GetString(StringId::kBtnChangeFont));
  SetDlgItemTextW(hwnd, IDC_OPT_LBL_UI_FONT, LanguageManager::GetString(StringId::kLabelUiFont));
  SetDlgItemTextW(hwnd, IDC_OPT_BTN_UI_FONT, LanguageManager::GetString(StringId::kBtnChangeFont));
  SetDlgItemTextW(hwnd, IDC_OPT_LBL_INTERVAL, LanguageManager::GetString(StringId::kLabelRefreshInterval));
  SetDlgItemTextW(hwnd, IDC_OPT_CHK_ALWAYS_TOP, LanguageManager::GetString(StringId::kLabelAlwaysOnTop));
  SetDlgItemTextW(hwnd, IDC_OPT_CHK_TRAY, LanguageManager::GetString(StringId::kLabelMinimizeToTray));
  SetDlgItemTextW(hwnd, IDC_OPT_CHK_AUTO_START, LanguageManager::GetString(StringId::kLabelAutoStart));
  SetDlgItemTextW(hwnd, IDC_OPT_LBL_EXCLUDED, LanguageManager::GetString(StringId::kLabelExcludedProcesses));
  SetDlgItemTextW(hwnd, IDC_OPT_BTN_ADD_EXCLUDED, LanguageManager::GetString(StringId::kBtnAdd));
  SetDlgItemTextW(hwnd, IDC_OPT_BTN_DEL_EXCLUDED, LanguageManager::GetString(StringId::kBtnRemove));
  SetDlgItemTextW(hwnd, IDOK, LanguageManager::GetString(StringId::kBtnOk));
  SetDlgItemTextW(hwnd, IDCANCEL, LanguageManager::GetString(StringId::kBtnCancel));

  // Get Control Handles
  combo_lang_ = GetDlgItem(hwnd, IDC_OPT_COMBO_LANG);
  lbl_list_font_val_ = GetDlgItem(hwnd, IDC_OPT_LBL_LIST_FONT_VAL);
  btn_list_font_ = GetDlgItem(hwnd, IDC_OPT_BTN_LIST_FONT);
  lbl_ui_font_val_ = GetDlgItem(hwnd, IDC_OPT_LBL_UI_FONT_VAL);
  btn_ui_font_ = GetDlgItem(hwnd, IDC_OPT_BTN_UI_FONT);
  slider_interval_ = GetDlgItem(hwnd, IDC_OPT_SLIDER_INTERVAL);
  lbl_interval_val_ = GetDlgItem(hwnd, IDC_OPT_LBL_INTERVAL_VAL);
  chk_always_top_ = GetDlgItem(hwnd, IDC_OPT_CHK_ALWAYS_TOP);
  chk_tray_ = GetDlgItem(hwnd, IDC_OPT_CHK_TRAY);
  chk_auto_start_ = GetDlgItem(hwnd, IDC_OPT_CHK_AUTO_START);
  list_excluded_ = GetDlgItem(hwnd, IDC_OPT_LIST_EXCLUDED);
  btn_add_excluded_ = GetDlgItem(hwnd, IDC_OPT_BTN_ADD_EXCLUDED);
  btn_del_excluded_ = GetDlgItem(hwnd, IDC_OPT_BTN_DEL_EXCLUDED);

  // Apply Themes
  apply_ctrl_theme(combo_lang_);
  apply_ctrl_theme(btn_list_font_);
  apply_ctrl_theme(btn_ui_font_);
  apply_ctrl_theme(slider_interval_);
  apply_ctrl_theme(chk_always_top_);
  apply_ctrl_theme(chk_tray_);
  apply_ctrl_theme(chk_auto_start_);
  apply_ctrl_theme(list_excluded_);
  apply_ctrl_theme(btn_add_excluded_);
  apply_ctrl_theme(btn_del_excluded_);
  apply_ctrl_theme(GetDlgItem(hwnd, IDOK));
  apply_ctrl_theme(GetDlgItem(hwnd, IDCANCEL));

  // 1. Language Combobox
  SendMessageW(combo_lang_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kLangAuto)));
  SendMessageW(combo_lang_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kLangJapanese)));
  SendMessageW(combo_lang_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LanguageManager::GetString(StringId::kLangEnglish)));
  int lang_sel = 0;
  if (settings_.language == AppLanguage::kJapanese) lang_sel = 1;
  else if (settings_.language == AppLanguage::kEnglish) lang_sel = 2;
  SendMessageW(combo_lang_, CB_SETCURSEL, lang_sel, 0);

  // 2. Fonts
  std::wstring list_font_disp = settings_.list_font_name + L", " + std::to_wstring(settings_.list_font_size) + L"pt";
  SetWindowTextW(lbl_list_font_val_, list_font_disp.c_str());

  std::wstring ui_font_disp = settings_.ui_font_name + L", " + std::to_wstring(settings_.ui_font_size) + L"pt";
  SetWindowTextW(lbl_ui_font_val_, ui_font_disp.c_str());

  // 3. Refresh Interval Trackbar (0-30 seconds)
  SendMessageW(slider_interval_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 30));
  SendMessageW(slider_interval_, TBM_SETTICFREQ, 5, 0);
  int cur_interval = std::clamp(settings_.refresh_interval_seconds, 0, 30);
  SendMessageW(slider_interval_, TBM_SETPOS, TRUE, cur_interval);
  UpdateIntervalLabel(cur_interval);

  // 5. Checkboxes
  SendMessageW(chk_always_top_, BM_SETCHECK, settings_.always_on_top ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(chk_tray_, BM_SETCHECK, settings_.minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(chk_auto_start_, BM_SETCHECK, settings_.auto_start ? BST_CHECKED : BST_UNCHECKED, 0);

  // 6. Excluded Processes ListView
  ListView_SetExtendedListViewStyle(list_excluded_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  LVCOLUMNW lvc = {0};
  lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
  lvc.iSubItem = 0;
  lvc.cx = 260;
  std::wstring col_hdr = LanguageManager::GetString(StringId::kMatchTargetProcessName);
  lvc.pszText = const_cast<LPWSTR>(col_hdr.c_str());
  ListView_InsertColumn(list_excluded_, 0, &lvc);

  for (int i = 0; i < static_cast<int>(settings_.excluded_processes.size()); ++i) {
    LVITEMW lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = i;
    lvi.pszText = const_cast<LPWSTR>(settings_.excluded_processes[i].c_str());
    ListView_InsertItem(list_excluded_, &lvi);
  }
  if (!settings_.excluded_processes.empty()) {
    ListView_SetItemState(list_excluded_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  }
}

void OptionsDialog::UpdateIntervalLabel(int seconds) {
  std::wstring text;
  std::wstring sec_unit = LanguageManager::GetString(StringId::kSecondsUnit);
  if (seconds == 0) {
    std::wstring pause_str = LanguageManager::GetString(StringId::kIntervalPause);
    text = L"0 " + sec_unit + L" (" + pause_str + L")";
  } else {
    text = std::to_wstring(seconds) + L" " + sec_unit;
  }
  SetWindowTextW(lbl_interval_val_, text.c_str());
}

void OptionsDialog::OnChangeListFont() {
  CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};
  LOGFONTW lf = {0};

  HDC hdc = GetDC(dlg_hwnd_);
  int log_y = GetDeviceCaps(hdc, LOGPIXELSY);
  ReleaseDC(dlg_hwnd_, hdc);

  lf.lfHeight = -MulDiv(settings_.list_font_size > 0 ? settings_.list_font_size : 9, log_y, 72);
  lf.lfWeight = FW_NORMAL;
  wcscpy_s(lf.lfFaceName, settings_.list_font_name.c_str());

  cf.hwndOwner = dlg_hwnd_;
  cf.lpLogFont = &lf;
  cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

  if (ChooseFontW(&cf)) {
    settings_.list_font_name = lf.lfFaceName;
    int pts = MulDiv(-lf.lfHeight, 72, log_y);
    if (pts <= 0) pts = 9;
    settings_.list_font_size = pts;

    std::wstring font_disp = settings_.list_font_name + L", " + std::to_wstring(settings_.list_font_size) + L"pt";
    SetWindowTextW(lbl_list_font_val_, font_disp.c_str());
  }
}

void OptionsDialog::OnChangeUiFont() {
  CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};
  LOGFONTW lf = {0};

  HDC hdc = GetDC(dlg_hwnd_);
  int log_y = GetDeviceCaps(hdc, LOGPIXELSY);
  ReleaseDC(dlg_hwnd_, hdc);

  lf.lfHeight = -MulDiv(settings_.ui_font_size > 0 ? settings_.ui_font_size : 9, log_y, 72);
  lf.lfWeight = FW_NORMAL;
  wcscpy_s(lf.lfFaceName, settings_.ui_font_name.c_str());

  cf.hwndOwner = dlg_hwnd_;
  cf.lpLogFont = &lf;
  cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

  if (ChooseFontW(&cf)) {
    settings_.ui_font_name = lf.lfFaceName;
    int pts = MulDiv(-lf.lfHeight, 72, log_y);
    if (pts <= 0) pts = 9;
    settings_.ui_font_size = pts;

    std::wstring font_disp = settings_.ui_font_name + L", " + std::to_wstring(settings_.ui_font_size) + L"pt";
    SetWindowTextW(lbl_ui_font_val_, font_disp.c_str());
  }
}

void OptionsDialog::OnAddExcludedProcess() {
  std::wstring name;
  if (PromptAddExcludedProcess(dlg_hwnd_, settings_.theme, &name)) {
    // Check if already in list
    int count = ListView_GetItemCount(list_excluded_);
    for (int i = 0; i < count; ++i) {
      wchar_t buf[260] = {0};
      ListView_GetItemText(list_excluded_, i, 0, buf, static_cast<int>(std::size(buf)));
      if (_wcsicmp(buf, name.c_str()) == 0) {
        ListView_SetItemState(list_excluded_, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_excluded_, i, FALSE);
        return;
      }
    }

    LVITEMW lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = count;
    lvi.pszText = const_cast<LPWSTR>(name.c_str());
    int new_idx = ListView_InsertItem(list_excluded_, &lvi);
    ListView_SetItemState(list_excluded_, new_idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_excluded_, new_idx, FALSE);
  }
}

void OptionsDialog::OnDeleteExcludedProcess() {
  int sel = ListView_GetNextItem(list_excluded_, -1, LVNI_SELECTED);
  if (sel < 0) return;

  ListView_DeleteItem(list_excluded_, sel);
  int count = ListView_GetItemCount(list_excluded_);
  if (count > 0) {
    int next_sel = (sel < count) ? sel : (count - 1);
    ListView_SetItemState(list_excluded_, next_sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  }
}

void OptionsDialog::OnSave() {
  int lang_sel = static_cast<int>(SendMessageW(combo_lang_, CB_GETCURSEL, 0, 0));
  if (lang_sel == 1) settings_.language = AppLanguage::kJapanese;
  else if (lang_sel == 2) settings_.language = AppLanguage::kEnglish;
  else settings_.language = AppLanguage::kAuto;

  settings_.theme = AppTheme::kLight;

  int interval = static_cast<int>(SendMessageW(slider_interval_, TBM_GETPOS, 0, 0));
  settings_.refresh_interval_seconds = std::clamp(interval, 0, 30);

  settings_.always_on_top = (SendMessageW(chk_always_top_, BM_GETCHECK, 0, 0) == BST_CHECKED);
  settings_.minimize_to_tray = (SendMessageW(chk_tray_, BM_GETCHECK, 0, 0) == BST_CHECKED);
  settings_.auto_start = (SendMessageW(chk_auto_start_, BM_GETCHECK, 0, 0) == BST_CHECKED);
  AppSettings::SetAutoStart(settings_.auto_start);

  // Excluded Processes from listview
  std::vector<std::wstring> new_excluded;
  int count = ListView_GetItemCount(list_excluded_);
  for (int i = 0; i < count; ++i) {
    wchar_t item_buf[260] = {0};
    ListView_GetItemText(list_excluded_, i, 0, item_buf, static_cast<int>(std::size(item_buf)));
    std::wstring trimmed = TrimString(item_buf);
    if (!trimmed.empty()) {
      new_excluded.push_back(trimmed);
    }
  }
  settings_.excluded_processes = std::move(new_excluded);

  confirmed_ = true;
}

LRESULT OptionsDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_HSCROLL: {
      if (reinterpret_cast<HWND>(lparam) == slider_interval_) {
        int pos = static_cast<int>(SendMessageW(slider_interval_, TBM_GETPOS, 0, 0));
        UpdateIntervalLabel(pos);
        return TRUE;
      }
      break;
    }

    case WM_COMMAND: {
      int id = LOWORD(wparam);
      if (id == IDC_OPT_BTN_LIST_FONT) {
        OnChangeListFont();
        return TRUE;
      } else if (id == IDC_OPT_BTN_UI_FONT) {
        OnChangeUiFont();
        return TRUE;
      } else if (id == IDC_OPT_BTN_ADD_EXCLUDED) {
        OnAddExcludedProcess();
        return TRUE;
      } else if (id == IDC_OPT_BTN_DEL_EXCLUDED) {
        OnDeleteExcludedProcess();
        return TRUE;
      } else if (id == IDOK || id == IDC_OPT_BTN_OK) {
        OnSave();
        EndDialog(hwnd, IDOK);
        return TRUE;
      } else if (id == IDCANCEL || id == IDC_OPT_BTN_CANCEL) {
        confirmed_ = false;
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
      }
      break;
    }

    case WM_NOTIFY: {
      auto* pnmh = reinterpret_cast<NMHDR*>(lparam);
      if (pnmh && pnmh->idFrom == IDC_OPT_LIST_EXCLUDED && pnmh->code == LVN_KEYDOWN) {
        auto* plv = reinterpret_cast<NMLVKEYDOWN*>(lparam);
        if (plv->wVKey == VK_DELETE) {
          OnDeleteExcludedProcess();
          return TRUE;
        }
      }
      break;
    }

    case WM_CTLCOLORDLG: {
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkMode(hdc, TRANSPARENT);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.control_background);
      return reinterpret_cast<INT_PTR>(palette.control_brush);
    }
  }
  return FALSE;
}

}  // namespace lite_proc_manager
