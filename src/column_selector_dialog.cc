// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "column_selector_dialog.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>

#include "language_manager.h"
#include "resource.h"
#include "theme_manager.h"

namespace lite_proc_manager {

ColumnSelectorDialog::ColumnSelectorDialog(
    HWND parent_hwnd, const std::vector<ProcessColumnInfo>& columns, AppTheme theme)
    : parent_hwnd_(parent_hwnd), columns_(columns), theme_(theme) {}

ColumnSelectorDialog::~ColumnSelectorDialog() = default;

bool ColumnSelectorDialog::Show() {
  INT_PTR res = DialogBoxParamW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_COLUMN_SELECTOR_DIALOG), parent_hwnd_,
      reinterpret_cast<DLGPROC>(DialogProc), reinterpret_cast<LPARAM>(this));

  return (res == IDOK && result_);
}

INT_PTR CALLBACK ColumnSelectorDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  ColumnSelectorDialog* self = nullptr;
  if (msg == WM_INITDIALOG) {
    self = reinterpret_cast<ColumnSelectorDialog*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->dialog_hwnd_ = hwnd;

    // Apply Title & Translations
    SetWindowTextW(hwnd, LanguageManager::GetString(StringId::kDlgColumnSelectorTitle));
    ThemeManager::ApplyTheme(hwnd, self->theme_);

    SetDlgItemTextW(hwnd, IDC_COL_INSTRUCTION, LanguageManager::GetString(StringId::kSelectColumnsInstruction));
    SetDlgItemTextW(hwnd, IDC_COL_BTN_MOVE_UP, LanguageManager::GetString(StringId::kBtnMoveUp));
    SetDlgItemTextW(hwnd, IDC_COL_BTN_MOVE_DOWN, LanguageManager::GetString(StringId::kBtnMoveDown));
    SetDlgItemTextW(hwnd, IDC_COL_BTN_SELECT_ALL, LanguageManager::GetString(StringId::kBtnSelectAll));
    SetDlgItemTextW(hwnd, IDC_COL_BTN_DEFAULT, LanguageManager::GetString(StringId::kBtnDefault));
    SetDlgItemTextW(hwnd, IDOK, LanguageManager::GetString(StringId::kBtnOk));
    SetDlgItemTextW(hwnd, IDCANCEL, LanguageManager::GetString(StringId::kBtnCancel));

    // Setup ListView
    self->listbox_hwnd_ = GetDlgItem(hwnd, IDC_COL_LIST);
    ListView_SetExtendedListViewStyle(self->listbox_hwnd_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    SetWindowTheme(self->listbox_hwnd_, L"Explorer", nullptr);

    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_WIDTH;
    lvc.cx = 280;
    ListView_InsertColumn(self->listbox_hwnd_, 0, &lvc);

    bool is_dark = (self->theme_ == AppTheme::kDark);
    auto apply_btn_theme = [&](int id) {
      HWND btn = GetDlgItem(hwnd, id);
      if (btn) SetWindowTheme(btn, is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    };
    apply_btn_theme(IDC_COL_BTN_MOVE_UP);
    apply_btn_theme(IDC_COL_BTN_MOVE_DOWN);
    apply_btn_theme(IDC_COL_BTN_SELECT_ALL);
    apply_btn_theme(IDC_COL_BTN_DEFAULT);
    apply_btn_theme(IDOK);
    apply_btn_theme(IDCANCEL);

    self->PopulateList();
    return TRUE;
  } else {
    self = reinterpret_cast<ColumnSelectorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return FALSE;
}

INT_PTR ColumnSelectorDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_COMMAND: {
      int id = LOWORD(wparam);
      switch (id) {
        case IDC_COL_BTN_MOVE_UP:
          MoveItem(-1);
          return TRUE;
        case IDC_COL_BTN_MOVE_DOWN:
          MoveItem(1);
          return TRUE;
        case IDC_COL_BTN_SELECT_ALL:
          SelectAll();
          return TRUE;
        case IDC_COL_BTN_DEFAULT:
          ResetDefault();
          return TRUE;
        case IDOK:
          OnOk();
          result_ = true;
          EndDialog(hwnd, IDOK);
          return TRUE;
        case IDCANCEL:
          result_ = false;
          EndDialog(hwnd, IDCANCEL);
          return TRUE;
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
      SetBkMode(hdc, TRANSPARENT);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }
  }

  return FALSE;
}

void ColumnSelectorDialog::PopulateList() {
  ListView_DeleteAllItems(listbox_hwnd_);

  for (size_t i = 0; i < columns_.size(); ++i) {
    LVITEMW item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(i);
    std::wstring header = LanguageManager::GetColumnHeaderText(columns_[i].id);
    item.pszText = const_cast<wchar_t*>(header.c_str());
    ListView_InsertItem(listbox_hwnd_, &item);

    ListView_SetCheckState(listbox_hwnd_, i, columns_[i].visible ? TRUE : FALSE);
  }
}

void ColumnSelectorDialog::MoveItem(int direction) {
  int sel = ListView_GetNextItem(listbox_hwnd_, -1, LVNI_SELECTED);
  if (sel < 0) return;

  int target = sel + direction;
  if (target < 0 || target >= static_cast<int>(columns_.size())) return;

  // Sync check states before move
  for (size_t i = 0; i < columns_.size(); ++i) {
    columns_[i].visible = (ListView_GetCheckState(listbox_hwnd_, i) != FALSE);
  }

  std::swap(columns_[sel], columns_[target]);
  PopulateList();

  ListView_SetItemState(listbox_hwnd_, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void ColumnSelectorDialog::SelectAll() {
  for (size_t i = 0; i < columns_.size(); ++i) {
    ListView_SetCheckState(listbox_hwnd_, i, TRUE);
  }
}

void ColumnSelectorDialog::ResetDefault() {
  columns_ = ProcessColumnInfo::GetDefaultColumns();
  PopulateList();
}

void ColumnSelectorDialog::OnOk() {
  for (size_t i = 0; i < columns_.size(); ++i) {
    columns_[i].visible = (ListView_GetCheckState(listbox_hwnd_, i) != FALSE);
    columns_[i].order = static_cast<int>(i);
  }
}

}  // namespace lite_proc_manager
