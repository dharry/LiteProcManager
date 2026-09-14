// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "main_window.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>

#include "column_selector_dialog.h"
#include "language_manager.h"
#include "monitor_dialog.h"
#include "options_dialog.h"
#include "process_export.h"
#include "resource.h"
#include "theme_manager.h"
#include "url_helper.h"
#include "version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

// Defined in main.cc; releases the single-instance mutex so a relaunched
// copy of this process does not mistake this (soon-to-exit) instance for
// an already-running one.
void ReleaseSingleInstanceLock();
void RestoreSingleInstanceLock(HANDLE mutex);

namespace lite_proc_manager {

namespace {
constexpr UINT kSearchDebounceMilliseconds = 150;
constexpr UINT kFilterSaveDebounceMilliseconds = 500;
std::atomic_uint32_t g_export_temporary_file_sequence{0};

enum class ExportSaveResult {
  kSaved,
  kCancelled,
  kError,
};

struct ServiceSnapshotMessage {
  uint64_t generation{0};
  std::vector<std::shared_ptr<ServiceItem>> services;
};

struct AboutDialogContext {
  AppTheme theme{AppTheme::kLight};
  HFONT ui_font{nullptr};
  std::wstring title;
  std::wstring app_name;
  std::wstring details;
};

INT_PTR CALLBACK AboutDialogProc(HWND hwnd, UINT msg, WPARAM wparam,
                                 LPARAM lparam) {
  auto* context = reinterpret_cast<AboutDialogContext*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      context = reinterpret_cast<AboutDialogContext*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(context));

      SetWindowTextW(hwnd, context->title.c_str());
      SetDlgItemTextW(hwnd, IDC_ABOUT_APP_NAME, context->app_name.c_str());
      SetDlgItemTextW(hwnd, IDC_ABOUT_DETAILS, context->details.c_str());
      SetDlgItemTextW(hwnd, IDOK,
                      LanguageManager::GetString(StringId::kBtnOk));

      HICON app_icon = LoadIconW(GetModuleHandleW(nullptr),
                                 MAKEINTRESOURCEW(IDI_APP_ICON));
      SendDlgItemMessageW(hwnd, IDC_ABOUT_ICON, STM_SETICON,
                          reinterpret_cast<WPARAM>(app_icon), 0);
      SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(app_icon));

      ThemeManager::ApplyTheme(hwnd, context->theme);
      ThemeManager::ApplyFontToWindowTree(hwnd, context->ui_font);

      const bool is_dark = context->theme == AppTheme::kDark;
      SetWindowTheme(GetDlgItem(hwnd, IDOK),
                     is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      return TRUE;
    }

    case WM_COMMAND:
      if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
        EndDialog(hwnd, LOWORD(wparam));
        return TRUE;
      }
      break;

    case WM_CLOSE:
      EndDialog(hwnd, IDCANCEL);
      return TRUE;

    case WM_CTLCOLORDLG:
      if (context) {
        return reinterpret_cast<INT_PTR>(
            ThemeManager::GetPalette(context->theme).window_brush);
      }
      break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      if (context) {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        const auto& palette = ThemeManager::GetPalette(context->theme);
        SetTextColor(hdc, palette.text_primary);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(palette.window_brush);
      }
      break;
  }
  return FALSE;
}

HRESULT OpenFolderAndSelectFile(const std::wstring& file_path) {
  PIDLIST_ABSOLUTE item_pidl = nullptr;
  HRESULT result = SHParseDisplayName(
      file_path.c_str(), nullptr, &item_pidl, 0, nullptr);
  if (FAILED(result)) {
    return result;
  }

  PIDLIST_ABSOLUTE folder_pidl = ILClone(item_pidl);
  if (!folder_pidl) {
    CoTaskMemFree(item_pidl);
    return E_OUTOFMEMORY;
  }

  PCUITEMID_CHILD child_pidl = ILFindLastID(item_pidl);
  if (!ILRemoveLastID(folder_pidl)) {
    CoTaskMemFree(folder_pidl);
    CoTaskMemFree(item_pidl);
    return E_INVALIDARG;
  }

  result = SHOpenFolderAndSelectItems(folder_pidl, 1, &child_pidl, 0);
  CoTaskMemFree(folder_pidl);
  CoTaskMemFree(item_pidl);
  return result;
}

bool WriteUtf8ExportFileAtomically(const std::wstring& path,
                                   const std::wstring& content,
                                   bool include_bom) {
  if (content.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return false;
  }

  int utf8_length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, content.data(),
      static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0) return false;

  std::string utf8;
  if (include_bom) {
    utf8.append("\xEF\xBB\xBF", 3);
  }
  size_t content_offset = utf8.size();
  utf8.resize(content_offset + static_cast<size_t>(utf8_length));
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, content.data(),
          static_cast<int>(content.size()), utf8.data() + content_offset,
          utf8_length, nullptr, nullptr) != utf8_length) {
    return false;
  }

  std::wstring temporary_path;
  HANDLE temporary_file = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 16; ++attempt) {
    uint32_t sequence = g_export_temporary_file_sequence.fetch_add(
        1, std::memory_order_relaxed);
    temporary_path = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                     L"." + std::to_wstring(sequence);
    temporary_file = CreateFileW(
        temporary_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (temporary_file != INVALID_HANDLE_VALUE) break;
    DWORD error = GetLastError();
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
      return false;
    }
  }
  if (temporary_file == INVALID_HANDLE_VALUE) return false;

  bool succeeded = true;
  size_t written_total = 0;
  while (written_total < utf8.size()) {
    DWORD write_size = static_cast<DWORD>(std::min<size_t>(
        utf8.size() - written_total, std::numeric_limits<DWORD>::max()));
    DWORD written = 0;
    if (!WriteFile(temporary_file, utf8.data() + written_total, write_size,
                   &written, nullptr) ||
        written == 0) {
      succeeded = false;
      break;
    }
    written_total += written;
  }
  if (succeeded && !FlushFileBuffers(temporary_file)) succeeded = false;
  if (!CloseHandle(temporary_file)) succeeded = false;

  if (!succeeded) {
    DeleteFileW(temporary_path.c_str());
    return false;
  }
  if (ReplaceFileW(path.c_str(), temporary_path.c_str(), nullptr, 0, nullptr,
                   nullptr)) {
    return true;
  }

  DWORD replace_error = GetLastError();
  if ((replace_error == ERROR_FILE_NOT_FOUND ||
       replace_error == ERROR_PATH_NOT_FOUND) &&
      MoveFileExW(temporary_path.c_str(), path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  DeleteFileW(temporary_path.c_str());
  return false;
}

ExportSaveResult PromptAndSaveExportFile(
    HWND owner, const std::wstring& content, const wchar_t* default_filename,
    const wchar_t* default_extension, StringId filter_string_id,
    StringId title_string_id, bool include_bom) {
  std::vector<wchar_t> filename(32768, L'\0');
  wcsncpy_s(filename.data(), filename.size(), default_filename, _TRUNCATE);

  std::wstring filter_label = LanguageManager::GetString(filter_string_id);
  std::wstring filter_pattern = L"*." + std::wstring(default_extension);
  std::vector<wchar_t> filter;
  filter.insert(filter.end(), filter_label.begin(), filter_label.end());
  filter.push_back(L'\0');
  filter.insert(filter.end(), filter_pattern.begin(), filter_pattern.end());
  filter.push_back(L'\0');
  filter.push_back(L'\0');

  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = filter.data();
  dialog.lpstrFile = filename.data();
  dialog.nMaxFile = static_cast<DWORD>(filename.size());
  dialog.lpstrDefExt = default_extension;
  std::wstring title = LanguageManager::GetString(title_string_id);
  dialog.lpstrTitle = title.c_str();
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                 OFN_NOCHANGEDIR;

  if (!GetSaveFileNameW(&dialog)) {
    return CommDlgExtendedError() == 0 ? ExportSaveResult::kCancelled
                                       : ExportSaveResult::kError;
  }
  return WriteUtf8ExportFileAtomically(filename.data(), content, include_bom)
             ? ExportSaveResult::kSaved
             : ExportSaveResult::kError;
}

bool CaseInsensitiveContains(const std::wstring& text, const std::wstring& pattern) {
  if (pattern.empty()) return true;
  if (text.empty()) return false;

  auto it = std::search(
      text.begin(), text.end(), pattern.begin(), pattern.end(),
      [](wchar_t ch1, wchar_t ch2) { return std::towlower(ch1) == std::towlower(ch2); });
  return it != text.end();
}

bool IsSearchPlaceholderOrEmpty(const std::wstring& text) {
  if (text.empty()) return true;

  size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return true;
  size_t last = text.find_last_not_of(L" \t\r\n");
  std::wstring trimmed = text.substr(first, (last - first + 1));

  std::wstring cur_ph = LanguageManager::GetString(StringId::kSearchPlaceholder);
  size_t placeholder_first = cur_ph.find_first_not_of(L" \t\r\n");
  size_t placeholder_last = cur_ph.find_last_not_of(L" \t\r\n");
  std::wstring trimmed_placeholder =
      placeholder_first == std::wstring::npos
          ? std::wstring()
          : cur_ph.substr(placeholder_first,
                          placeholder_last - placeholder_first + 1);
  return text == cur_ph || trimmed == trimmed_placeholder;
}

constexpr UINT_PTR kStatusBarSubclassId = 0x9002;

LRESULT CALLBACK StatusBarSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto* self = reinterpret_cast<MainWindow*>(dwRefData);

  if (self && self->IsDarkMode()) {
    switch (msg) {
      case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
        FillRect(hdc, &rc, palette.window_brush);
        return TRUE;
      }
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
        FillRect(hdc, &rc, palette.window_brush);

        HFONT hfont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT old_font = static_cast<HFONT>(SelectObject(hdc, hfont));

        SetTextColor(hdc, palette.text_primary);
        SetBkMode(hdc, TRANSPARENT);

        int num_parts = static_cast<int>(SendMessageW(hwnd, SB_GETPARTS, 0, 0));
        if (num_parts > 0) {
          std::vector<int> right_edges(num_parts);
          SendMessageW(hwnd, SB_GETPARTS, num_parts, reinterpret_cast<LPARAM>(right_edges.data()));

          int left = 6;
          for (int i = 0; i < num_parts; ++i) {
            wchar_t text[256] = {0};
            SendMessageW(hwnd, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(text));

            int right = (right_edges[i] == -1) ? rc.right : right_edges[i];
            RECT part_rc = {left, rc.top + 2, right - 4, rc.bottom - 2};

            DrawTextW(hdc, text, -1, &part_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (i < num_parts - 1 && right < rc.right) {
              HPEN pen = CreatePen(PS_SOLID, 1, palette.border_color);
              HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
              MoveToEx(hdc, right, rc.top + 3, nullptr);
              LineTo(hdc, right, rc.bottom - 3);
              SelectObject(hdc, old_pen);
              DeleteObject(pen);
            }
            left = right + 6;
          }
        }

        SelectObject(hdc, old_font);
        EndPaint(hwnd, &ps);
        return 0;
      }
    }
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

constexpr UINT_PTR kTabControlSubclassId = 0x9004;

LRESULT CALLBACK TabControlSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto* self = reinterpret_cast<MainWindow*>(dwRefData);

  if (self && self->IsDarkMode()) {
    switch (msg) {
      case WM_ERASEBKGND:
        return TRUE;

      case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rect{};
        GetClientRect(hwnd, &client_rect);
        int width = client_rect.right - client_rect.left;
        int height = client_rect.bottom - client_rect.top;
        if (width <= 0 || height <= 0) {
          EndPaint(hwnd, &ps);
          return 0;
        }

        const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
        HDC memory_dc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP old_bitmap =
            static_cast<HBITMAP>(SelectObject(memory_dc, bitmap));
        FillRect(memory_dc, &client_rect, palette.window_brush);

        HFONT font = reinterpret_cast<HFONT>(
            SendMessageW(hwnd, WM_GETFONT, 0, 0));
        if (!font) {
          font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        HFONT old_font = static_cast<HFONT>(SelectObject(memory_dc, font));
        SetBkMode(memory_dc, TRANSPARENT);

        int selected_index = TabCtrl_GetCurSel(hwnd);
        int item_count = TabCtrl_GetItemCount(hwnd);
        for (int index = 0; index < item_count; ++index) {
          RECT item_rect{};
          if (!TabCtrl_GetItemRect(hwnd, index, &item_rect)) continue;

          bool selected = index == selected_index;
          FillRect(memory_dc, &item_rect,
                   selected ? palette.control_brush : palette.window_brush);

          wchar_t item_text[128] = {0};
          TCITEMW item{};
          item.mask = TCIF_TEXT;
          item.pszText = item_text;
          item.cchTextMax = static_cast<int>(std::size(item_text));
          if (TabCtrl_GetItem(hwnd, index, &item)) {
            RECT text_rect = item_rect;
            text_rect.left += 4;
            text_rect.right -= 4;
            SetTextColor(memory_dc,
                         selected ? palette.text_primary : palette.header_text);
            DrawTextW(memory_dc, item_text, -1, &text_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                          DT_NOPREFIX);
          }

          HBRUSH border_brush = CreateSolidBrush(
              selected ? palette.focus_border : palette.border_color);
          FrameRect(memory_dc, &item_rect, border_brush);
          DeleteObject(border_brush);
        }

        SelectObject(memory_dc, old_font);
        BitBlt(hdc, 0, 0, width, height, memory_dc, 0, 0, SRCCOPY);
        SelectObject(memory_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        EndPaint(hwnd, &ps);
        return 0;
      }
    }
  }

  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, TabControlSubclassProc, uIdSubclass);
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

constexpr UINT_PTR kSearchEditSubclassId = 0x9005;

LRESULT CALLBACK SearchEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto* self = reinterpret_cast<MainWindow*>(dwRefData);

  if (msg == WM_PAINT) {
    LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
    if (self && GetWindowTextLengthW(hwnd) == 0) {
      HDC hdc = GetDC(hwnd);
      if (hdc) {
        RECT text_rect{};
        GetClientRect(hwnd, &text_rect);
        text_rect.left += 5;
        text_rect.right -= 5;

        AppTheme theme = self->IsDarkMode() ? AppTheme::kDark
                                            : AppTheme::kLight;
        const auto& palette = ThemeManager::GetPalette(theme);
        HFONT font = reinterpret_cast<HFONT>(
            SendMessageW(hwnd, WM_GETFONT, 0, 0));
        if (!font) {
          font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, palette.placeholder_text);
        DrawTextW(hdc,
                  LanguageManager::GetString(StringId::kSearchPlaceholder),
                  -1, &text_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
        SelectObject(hdc, old_font);
        ReleaseDC(hwnd, hdc);
      }
    }
    return result;
  }

  if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_SETTEXT ||
      msg == WM_THEMECHANGED) {
    LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
    InvalidateRect(hwnd, nullptr, TRUE);
    return result;
  }

  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, SearchEditSubclassProc, uIdSubclass);
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

constexpr UINT_PTR kListViewSubclassId = 0x9003;

LRESULT CALLBACK ListViewSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  switch (msg) {
    case WM_KEYDOWN: {
      if ((wparam == 'A' || wparam == 'a') && (GetKeyState(VK_CONTROL) & 0x8000)) {
        ListView_SetItemState(hwnd, -1, LVIS_SELECTED, LVIS_SELECTED);
        return 0;
      }
      break;
    }
    case WM_CHAR: {
      // Suppress standard error beep when Ctrl+A (character code 1) is pressed
      if (wparam == 1) {
        return 0;
      }
      break;
    }
    case WM_NCDESTROY:
      RemoveWindowSubclass(hwnd, ListViewSubclassProc, uIdSubclass);
      break;
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

// -----------------------------------------------------------------------
// Condition Filter Column Table
// The order here is the combo box item order. Must stay in sync.
// -----------------------------------------------------------------------
constexpr ProcessColumnId kFilterColumnIds[] = {
  ProcessColumnId::kCpu,
  ProcessColumnId::kWorkingSet,
  ProcessColumnId::kPrivateWorkingSet,
  ProcessColumnId::kPeakWorkingSet,
  ProcessColumnId::kCommitSize,
  ProcessColumnId::kPagedPool,
  ProcessColumnId::kNonPagedPool,
  ProcessColumnId::kHandles,
  ProcessColumnId::kThreads,
  ProcessColumnId::kUserObjects,
  ProcessColumnId::kGdiObjects,
  ProcessColumnId::kIoReadBytes,
  ProcessColumnId::kIoWriteBytes,
  ProcessColumnId::kIoOtherBytes,
  ProcessColumnId::kIoReadCount,
  ProcessColumnId::kIoWriteCount,
  ProcessColumnId::kIoOtherCount,
  ProcessColumnId::kGpuUsage,
  ProcessColumnId::kDedicatedGpuMemory,
  ProcessColumnId::kSharedGpuMemory,
  ProcessColumnId::kPid,
  ProcessColumnId::kBasePriority,
  ProcessColumnId::kSessionId,
  ProcessColumnId::kName,
  ProcessColumnId::kStatus,
  ProcessColumnId::kUserName,
  ProcessColumnId::kDescription,
  ProcessColumnId::kFilePath,
  ProcessColumnId::kCommandLine,
  ProcessColumnId::kElevated,
  ProcessColumnId::kPlatform,
  ProcessColumnId::kArchitecture,
};
constexpr int kFilterColumnCount = static_cast<int>(std::size(kFilterColumnIds));

// Returns true for byte-unit columns where user input is in KiB (K)
bool IsMemoryBytesColumn(ProcessColumnId id) {
  switch (id) {
    case ProcessColumnId::kWorkingSet:
    case ProcessColumnId::kPrivateWorkingSet:
    case ProcessColumnId::kPeakWorkingSet:
    case ProcessColumnId::kCommitSize:
    case ProcessColumnId::kPagedPool:
    case ProcessColumnId::kNonPagedPool:
    case ProcessColumnId::kIoReadBytes:
    case ProcessColumnId::kIoWriteBytes:
    case ProcessColumnId::kIoOtherBytes:
    case ProcessColumnId::kDedicatedGpuMemory:
    case ProcessColumnId::kSharedGpuMemory:
      return true;
    default:
      return false;
  }
}

ProcessColumnId GetFilterColumnId(int combo_index) {
  if (combo_index < 0 || combo_index >= kFilterColumnCount) {
    return ProcessColumnId::kCpu;
  }
  return kFilterColumnIds[combo_index];
}

int FindFilterColumnIndex(ProcessColumnId id) {
  for (int i = 0; i < kFilterColumnCount; ++i) {
    if (kFilterColumnIds[i] == id) return i;
  }
  return 0;
}

bool IsProcessExcluded(const std::wstring& proc_name, const std::vector<std::wstring>& excluded_list) {
  for (const auto& excl : excluded_list) {
    if (excl.empty()) continue;
    if (_wcsicmp(proc_name.c_str(), excl.c_str()) == 0) return true;

    // Also match with/without .exe extension
    std::wstring name_no_ext = proc_name;
    if (name_no_ext.size() > 4 && _wcsicmp(name_no_ext.c_str() + name_no_ext.size() - 4, L".exe") == 0) {
      name_no_ext.resize(name_no_ext.size() - 4);
    }
    std::wstring excl_no_ext = excl;
    if (excl_no_ext.size() > 4 && _wcsicmp(excl_no_ext.c_str() + excl_no_ext.size() - 4, L".exe") == 0) {
      excl_no_ext.resize(excl_no_ext.size() - 4);
    }
    if (_wcsicmp(name_no_ext.c_str(), excl_no_ext.c_str()) == 0) return true;
  }
  return false;
}

ProcessSnapshotOptions BuildProcessSnapshotOptions(const AppSettings& settings) {
  ProcessSnapshotOptions options = ProcessSnapshotOptions::None();

  for (const auto& column : settings.columns) {
    if (column.visible) {
      options.IncludeColumn(column.id);
    }
  }

  if (settings.display_filter_enabled) {
    options.IncludeColumn(settings.display_filter_condition.column_id);
  }

  for (const auto& rule : settings.monitor_rules) {
    if (!rule.enabled) continue;
    for (const auto& condition : rule.conditions) {
      options.IncludeColumn(condition.column_id);
    }
  }

  return options;
}

}  // namespace

MainWindow::MainWindow() : settings_(AppSettings::Load()) {
  LanguageManager::Initialize(settings_.language);
  for (auto& rule : settings_.monitor_rules) {
    if (rule.name.empty()) {
      rule.name = LanguageManager::GetString(StringId::kDefaultMonitorRuleName);
    }
  }
  is_elevated_ = IsCurrentProcessElevated();
  search_active_brush_ = CreateSolidBrush(RGB(253, 253, 223));  // #FDFDDF

  // Create colorful toolbar icons
  hicon_tree_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kTree, 16);
  hicon_list_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kList, 16);
  hicon_columns_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kColumns, 16);
  hicon_shield_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kShield, 16);
  hicon_topmost_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kTopmost, 16);
  hicon_topmost_off_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kTopmostOff, 16);
  hicon_options_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kSettings, 16);
  hicon_monitor_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kMonitor, 16);
  hicon_refresh_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kRefresh, 16);
  hicon_endtask_ = IconHelper::CreateToolbarIcon(ToolbarIconType::kEndTask, 16);

  monitor_service_.SetRules(settings_.monitor_rules);
}

MainWindow::~MainWindow() {
  StopServiceRefreshWorker();

  if (notify_icon_data_.cbSize > 0) {
    Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_);
  }
  if (context_menu_) DestroyMenu(context_menu_);
  if (service_context_menu_) DestroyMenu(service_context_menu_);
  if (tray_menu_) DestroyMenu(tray_menu_);
  if (search_active_brush_) DeleteObject(search_active_brush_);
  if (list_font_) DeleteObject(list_font_);
  if (ui_font_) DeleteObject(ui_font_);

  if (hicon_tree_) DestroyIcon(hicon_tree_);
  if (hicon_list_) DestroyIcon(hicon_list_);
  if (hicon_columns_) DestroyIcon(hicon_columns_);
  if (hicon_shield_) DestroyIcon(hicon_shield_);
  if (hicon_topmost_) DestroyIcon(hicon_topmost_);
  if (hicon_topmost_off_) DestroyIcon(hicon_topmost_off_);
  if (hicon_options_) DestroyIcon(hicon_options_);
  if (hicon_monitor_) DestroyIcon(hicon_monitor_);
  if (hicon_refresh_) DestroyIcon(hicon_refresh_);
  if (hicon_endtask_) DestroyIcon(hicon_endtask_);

  ThemeManager::Shutdown();
}

bool MainWindow::Create(HINSTANCE instance, int cmd_show) {
  instance_ = instance;

  INITCOMMONCONTROLSEX iccex = {0};
  iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  iccex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_COOL_CLASSES | ICC_TAB_CLASSES;
  InitCommonControlsEx(&iccex);

  ThemeManager::Initialize();
  // The preferred app mode must be selected before creating native controls;
  // otherwise menus and scroll bars can retain their light appearance.
  ThemeManager::SetPreferredAppTheme(settings_.theme);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (settings_.theme == AppTheme::kDark)
                         ? ThemeManager::GetPalette(AppTheme::kDark).window_brush
                         : ThemeManager::GetPalette(AppTheme::kLight).window_brush;
  wc.lpszClassName = L"LiteProcManagerMainWindow";

  if (!RegisterClassExW(&wc)) {
    return false;
  }

  icon_helper_.Initialize(16);

  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  int w = settings_.window_width;
  int h = settings_.window_height;

  hwnd_ = CreateWindowExW(
      settings_.always_on_top ? WS_EX_TOPMOST : 0,
      wc.lpszClassName,
      LanguageManager::GetString(StringId::kAppTitle),
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      x, y, w, h,
      nullptr, nullptr, instance_, this);

  if (!hwnd_) return false;

  HICON big_icon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
  HICON sm_icon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
  if (big_icon) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
  if (sm_icon) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(sm_icon));

  InitializeComponents();
  ApplyTheme();
  StartServiceRefreshWorker();

  ShowWindow(hwnd_, settings_.is_maximized ? SW_MAXIMIZE : cmd_show);
  UpdateWindow(hwnd_);

  UpdateTimerInterval();
  RefreshData();

  return true;
}

int MainWindow::RunMessageLoop() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (msg.message == WM_KEYDOWN) {
      if (msg.wParam == VK_F5) {
        RefreshData();
        continue;
      } else if (msg.wParam == VK_DELETE) {
        HWND focus = GetFocus();
        wchar_t class_name[64] = {0};
        if (focus) {
          GetClassNameW(focus, class_name, static_cast<int>(std::size(class_name)));
        }

        // If focus is inside an Edit control (Quick Filter, Condition Value, etc.), let it process Del
        if (_wcsicmp(class_name, L"EDIT") == 0) {
          // Fall through to TranslateMessage / DispatchMessage
        } else if (current_tab_ == MainTab::kProcesses) {
          bool has_selection = false;
          if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
            has_selection = (focus == treeview_hwnd_ && TreeView_GetSelection(treeview_hwnd_) != nullptr);
          } else {
            has_selection = (focus == listview_hwnd_ && ListView_GetSelectedCount(listview_hwnd_) > 0);
          }

          if (has_selection) {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
              EndSelectedProcessTree();
            } else {
              EndSelectedProcess();
            }
          }
          continue;
        }
      } else if ((msg.wParam == 'A' || msg.wParam == 'a') && (GetKeyState(VK_CONTROL) & 0x8000)) {
        HWND focus = GetFocus();
        if (current_tab_ == MainTab::kServices) {
          if (focus == service_list_view_) {
            ListView_SetItemState(service_list_view_, -1, LVIS_SELECTED, LVIS_SELECTED);
            continue;
          }
        } else {
          if (focus == listview_hwnd_ || (focus != search_edit_ && settings_.display_mode == ViewDisplayMode::kFlatList)) {
            ListView_SetItemState(listview_hwnd_, -1, LVIS_SELECTED, LVIS_SELECTED);
            continue;
          }
        }
      } else if ((msg.wParam == 'F' || msg.wParam == 'f') && (GetKeyState(VK_CONTROL) & 0x8000)) {
        // Ctrl+F: Focus the quick filter search box
        if (search_edit_ && IsWindowVisible(search_edit_)) {
          SetFocus(search_edit_);
          SendMessageW(search_edit_, EM_SETSEL, 0, -1);
          continue;
        }
      } else if (msg.wParam == VK_TAB && ((GetKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000))) {
        // Ctrl+Tab / Ctrl+Shift+Tab: Switch between Processes and Services tabs
        if (tab_control_) {
          int cur_sel = TabCtrl_GetCurSel(tab_control_);
          int tab_count = TabCtrl_GetItemCount(tab_control_);
          if (tab_count > 0) {
            bool is_shift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            int next_sel = is_shift ? (cur_sel - 1 + tab_count) % tab_count
                                    : (cur_sel + 1) % tab_count;
            TabCtrl_SetCurSel(tab_control_, next_sel);
            OnTabChanged();
            if (current_tab_ == MainTab::kProcesses) {
              SetFocus(settings_.display_mode == ViewDisplayMode::kProcessTree ? treeview_hwnd_ : listview_hwnd_);
            } else if (current_tab_ == MainTab::kServices && service_list_view_) {
              SetFocus(service_list_view_);
            }
          }
          continue;
        }
      }
    }

    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  MainWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<MainWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void MainWindow::SetControlTooltip(HWND control_hwnd, const std::wstring& text) {
  if (!tooltip_hwnd_ || !control_hwnd) return;

  TOOLINFO ti = {0};
  ti.cbSize = sizeof(TOOLINFO);
  ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
  ti.hwnd = hwnd_;
  ti.uId = reinterpret_cast<UINT_PTR>(control_hwnd);
  ti.lpszText = const_cast<wchar_t*>(text.c_str());

  if (SendMessageW(tooltip_hwnd_, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&ti)) == 0) {
    SendMessageW(tooltip_hwnd_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
  }
}

void MainWindow::InitializeComponents() {
  HFONT hfont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

  // Tooltip control with 0 delay
  tooltip_hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      hwnd_, nullptr, instance_, nullptr);
  SetWindowPos(tooltip_hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SendMessageW(tooltip_hwnd_, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
  SendMessageW(tooltip_hwnd_, TTM_SETDELAYTIME, TTDT_RESHOW, 0);
  SendMessageW(tooltip_hwnd_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);

  // 1. Controls on top toolbar (Parent is hwnd_ so WM_COMMAND is directly handled)
  search_edit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      10, 9, 280, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCH_EDIT)), instance_, nullptr);
  SendMessageW(search_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
  SendMessageW(search_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
  SetWindowSubclass(search_edit_, SearchEditSubclassProc,
                    kSearchEditSubclassId,
                    reinterpret_cast<DWORD_PTR>(this));
  SetControlTooltip(search_edit_, LanguageManager::GetString(StringId::kTooltipQuickFilter));

  interval_combo_ = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
      298, 9, 80, 200, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INTERVAL_COMBO)), instance_, nullptr);
  SendMessageW(interval_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
  SetControlTooltip(interval_combo_, LanguageManager::GetString(StringId::kTooltipInterval));
  PopulateIntervalComboBox();

  auto create_icon_btn = [&](HICON hicon, int x, int w, int id, const wchar_t* tooltip_text) {
    HWND btn = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON,
        x, 8, w, 26, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (hicon) {
      SendMessageW(btn, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(hicon));
    }
    SetControlTooltip(btn, tooltip_text);
    return btn;
  };

  btn_tree_ = create_icon_btn(
      settings_.display_mode == ViewDisplayMode::kProcessTree ? hicon_list_ : hicon_tree_,
      386, 32, IDC_BTN_TREE,
      settings_.display_mode == ViewDisplayMode::kProcessTree ? LanguageManager::GetString(StringId::kTooltipViewList)
                                                             : LanguageManager::GetString(StringId::kTooltipViewTree));
  btn_columns_ = create_icon_btn(hicon_columns_, 422, 32, IDC_BTN_COLUMNS, LanguageManager::GetString(StringId::kTooltipColumns));
  btn_restart_admin_ = create_icon_btn(
      hicon_shield_, 458, 32, IDC_BTN_RESTART_ADMIN,
      is_elevated_ ? LanguageManager::GetString(StringId::kTooltipRunningAsAdmin)
                   : LanguageManager::GetString(StringId::kTooltipRestartAdmin));
  if (is_elevated_) {
    EnableWindow(btn_restart_admin_, FALSE);
  }
  btn_topmost_ = create_icon_btn(
      settings_.always_on_top ? hicon_topmost_ : hicon_topmost_off_,
      520, 32, IDC_BTN_TOPMOST,
      LanguageManager::GetString(StringId::kTooltipAlwaysOnTop));
  btn_options_ = create_icon_btn(hicon_options_, 556, 32, IDC_BTN_OPTIONS, LanguageManager::GetString(StringId::kTooltipOptions));
  btn_monitor_ = create_icon_btn(hicon_monitor_, 592, 32, IDC_BTN_MONITOR, LanguageManager::GetString(StringId::kTooltipMonitor));
  btn_refresh_ = create_icon_btn(hicon_refresh_, 628, 32, IDC_BTN_REFRESH, LanguageManager::GetString(StringId::kTooltipRefresh));
  btn_endtask_ = create_icon_btn(hicon_endtask_, 664, 32, IDC_BTN_ENDTASK, LanguageManager::GetString(StringId::kTooltipEndProcess));

  // 2. ListView
  listview_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_CLIPSIBLINGS,
      0, 42, settings_.window_width, settings_.window_height - 42 - 24,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LISTVIEW)), instance_, nullptr);

  ListView_SetExtendedListViewStyle(
      listview_hwnd_,
      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP | LVS_EX_DOUBLEBUFFER);
  ListView_SetImageList(listview_hwnd_, icon_helper_.GetImageList(), LVSIL_SMALL);
  RebuildListViewColumns();

  HWND header = ListView_GetHeader(listview_hwnd_);
  if (header) {
    SetWindowSubclass(header, HeaderSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
  }

  // 3. TreeView
  treeview_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
      WS_CHILD | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | WS_CLIPSIBLINGS,
      0, 42, settings_.window_width, settings_.window_height - 42 - 24,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TREEVIEW)), instance_, nullptr);

  TreeView_SetImageList(treeview_hwnd_, icon_helper_.GetImageList(), TVSIL_NORMAL);

  if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
    ShowWindow(listview_hwnd_, SW_HIDE);
    ShowWindow(treeview_hwnd_, SW_SHOW);
  } else {
    ShowWindow(listview_hwnd_, SW_SHOW);
    ShowWindow(treeview_hwnd_, SW_HIDE);
  }

  // 3b. Tab Control
  tab_control_ = CreateWindowExW(
      0, WC_TABCONTROLW, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_FOCUSNEVER,
      0, 38, settings_.window_width, 26,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAIN_TAB)), instance_, nullptr);
  SendMessageW(tab_control_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
  SendMessageW(tab_control_, TCM_SETMINTABWIDTH, 0, 88);

  TCITEMW tie = {0};
  tie.mask = TCIF_TEXT;
  std::wstring tab_proc = LanguageManager::GetString(StringId::kTabProcesses);
  tie.pszText = const_cast<LPWSTR>(tab_proc.c_str());
  TabCtrl_InsertItem(tab_control_, 0, &tie);

  std::wstring tab_svc = LanguageManager::GetString(StringId::kTabServices);
  tie.pszText = const_cast<LPWSTR>(tab_svc.c_str());
  TabCtrl_InsertItem(tab_control_, 1, &tie);
  SetWindowSubclass(tab_control_, TabControlSubclassProc,
                    kTabControlSubclassId,
                    reinterpret_cast<DWORD_PTR>(this));

  // 3c. Service ListView
  service_list_view_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | WS_CLIPSIBLINGS,
      0, 64, settings_.window_width, settings_.window_height - 64 - 24,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SERVICE_LIST_VIEW)), instance_, nullptr);
  ListView_SetExtendedListViewStyle(
      service_list_view_,
      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP | LVS_EX_DOUBLEBUFFER);
  SetWindowSubclass(service_list_view_, ListViewSubclassProc, kListViewSubclassId, reinterpret_cast<DWORD_PTR>(this));
  RebuildServiceListViewColumns();

  // 4. StatusBar
  statusbar_hwnd_ = CreateWindowExW(
      0, STATUSCLASSNAMEW, nullptr,
      WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
      0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUSBAR)), instance_, nullptr);

  int parts[] = {120, 240, 360, 480, 720, -1};
  SendMessageW(statusbar_hwnd_, SB_SETPARTS, std::size(parts), reinterpret_cast<LPARAM>(parts));
  SetWindowSubclass(statusbar_hwnd_, StatusBarSubclassProc, kStatusBarSubclassId, reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(listview_hwnd_, ListViewSubclassProc, kListViewSubclassId, reinterpret_cast<DWORD_PTR>(this));

  // 5. Notify Icon (Task Tray)
  notify_icon_data_.cbSize = sizeof(NOTIFYICONDATAW);
  notify_icon_data_.hWnd = hwnd_;
  notify_icon_data_.uID = 1;
  notify_icon_data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  notify_icon_data_.uCallbackMessage = WM_APP_TRAYMSG;
  HICON tray_icon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
  notify_icon_data_.hIcon = tray_icon ? tray_icon : icon_helper_.GetDefaultIcon();
  wcscpy_s(notify_icon_data_.szTip, L"LiteProcManager");

  Shell_NotifyIconW(NIM_ADD, &notify_icon_data_);

  notify_icon_data_.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &notify_icon_data_);
  UpdateTrayTooltip();

  // 6. Condition Filter Controls (2nd toolbar row, y=44)
  // Column combo - uses shared kFilterColumnIds table
  filter_col_combo_ = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
      10, 44, 220, 300,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FILTER_COL_COMBO)), instance_, nullptr);
  SendMessageW(filter_col_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);

  int init_col_sel = FindFilterColumnIndex(settings_.display_filter_condition.column_id);
  for (int ci = 0; ci < kFilterColumnCount; ++ci) {
    std::wstring col_name = LanguageManager::GetColumnHeaderText(kFilterColumnIds[ci]);
    SendMessageW(filter_col_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(col_name.c_str()));
  }
  SendMessageW(filter_col_combo_, CB_SETCURSEL, init_col_sel, 0);

  // Operator combo
  filter_op_combo_ = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
      238, 44, 90, 200,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FILTER_OP_COMBO)), instance_, nullptr);
  SendMessageW(filter_op_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);

  const wchar_t* op_labels[] = {L">", L">=", L"<", L"<=", L"==", L"!="};
  for (const auto* lbl : op_labels) {
    SendMessageW(filter_op_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lbl));
  }
  std::wstring contains_str = LanguageManager::GetString(StringId::kOpContains);
  SendMessageW(filter_op_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(contains_str.c_str()));

  {
    ComparisonOperator saved_ops[] = {
      ComparisonOperator::kGreaterThan, ComparisonOperator::kGreaterThanOrEqual,
      ComparisonOperator::kLessThan, ComparisonOperator::kLessThanOrEqual,
      ComparisonOperator::kEqual, ComparisonOperator::kNotEqual,
      ComparisonOperator::kContains,
    };
    int init_op_sel = 0;
    for (int oi = 0; oi < static_cast<int>(std::size(saved_ops)); ++oi) {
      if (saved_ops[oi] == settings_.display_filter_condition.op) {
        init_op_sel = oi;
        break;
      }
    }
    SendMessageW(filter_op_combo_, CB_SETCURSEL, init_op_sel, 0);
  }

  // Value edit (200px)
  filter_val_edit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      336, 44, 200, 24,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FILTER_VAL_EDIT)), instance_, nullptr);
  SendMessageW(filter_val_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
  SendMessageW(filter_val_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

  // Restore saved value - show in KiB (K) if memory column
  if (settings_.display_filter_enabled && settings_.display_filter_condition.numeric_value != 0.0) {
    bool is_bytes = IsMemoryBytesColumn(settings_.display_filter_condition.column_id);
    double display_val = is_bytes
        ? settings_.display_filter_condition.numeric_value / 1024.0
        : settings_.display_filter_condition.numeric_value;
    wchar_t num_buf[64];
    swprintf_s(num_buf, L"%.6g", display_val);
    SetWindowTextW(filter_val_edit_, num_buf);
  } else if (settings_.display_filter_enabled &&
             !settings_.display_filter_condition.string_value.empty() &&
             !IsMemoryBytesColumn(settings_.display_filter_condition.column_id)) {
    SetWindowTextW(filter_val_edit_, settings_.display_filter_condition.string_value.c_str());
  }

  // Clear button (×)
  filter_clear_btn_ = CreateWindowExW(
      0, L"BUTTON", L"\u00D7",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      544, 44, 28, 24,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FILTER_CLEAR_BTN)), instance_, nullptr);
  SendMessageW(filter_clear_btn_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);

  UpdateLanguageAndUI();
}

void MainWindow::RebuildListViewColumns() {
  HWND header = ListView_GetHeader(listview_hwnd_);
  int col_count = Header_GetItemCount(header);
  for (int i = col_count - 1; i >= 0; --i) {
    ListView_DeleteColumn(listview_hwnd_, i);
  }

  visible_process_columns_.clear();
  int col_index = 0;
  for (const auto& col : settings_.columns) {
    if (!col.visible) continue;

    visible_process_columns_.push_back(col.id);

    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
    lvc.cx = col.default_width;
    lvc.pszText = const_cast<wchar_t*>(col.header_text.c_str());
    lvc.fmt = (col.alignment == ColumnAlignment::kRight) ? LVCFMT_RIGHT : LVCFMT_LEFT;
    lvc.iSubItem = col_index;

    ListView_InsertColumn(listview_hwnd_, col_index, &lvc);
    col_index++;
  }

  ThemeManager::ApplyListViewHeaderTheme(listview_hwnd_, settings_.theme, sort_column_index_, sort_ascending_);
}

void MainWindow::PopulateIntervalComboBox() {
  if (!interval_combo_) return;

  SendMessageW(interval_combo_, CB_RESETCONTENT, 0, 0);

  std::vector<int> candidate_seconds = {1, 2, 5, 10, 15, 30, 60, 120, 300};
  int cur_sec = settings_.refresh_interval_seconds;
  if (cur_sec > 0 && std::find(candidate_seconds.begin(), candidate_seconds.end(), cur_sec) == candidate_seconds.end()) {
    candidate_seconds.push_back(cur_sec);
    std::sort(candidate_seconds.begin(), candidate_seconds.end());
  }
  // Pause (0 sec) at end
  candidate_seconds.push_back(0);

  std::wstring sec_unit = LanguageManager::GetString(StringId::kSecondsUnit);
  std::wstring pause_str = LanguageManager::GetString(StringId::kIntervalPause);

  int sel_idx = 0;
  for (size_t i = 0; i < candidate_seconds.size(); ++i) {
    int sec = candidate_seconds[i];
    std::wstring label;
    if (sec == 0) {
      label = pause_str;
    } else {
      label = std::to_wstring(sec) + L" " + sec_unit;
    }

    int idx = static_cast<int>(SendMessageW(interval_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
    SendMessageW(interval_combo_, CB_SETITEMDATA, idx, static_cast<LPARAM>(sec));

    if (sec == cur_sec) {
      sel_idx = idx;
    }
  }

  SendMessageW(interval_combo_, CB_SETCURSEL, sel_idx, 0);
}

void MainWindow::UpdateTimerInterval() {
  KillTimer(hwnd_, IDT_REFRESH_TIMER);
  if (settings_.refresh_interval_seconds > 0) {
    SetTimer(hwnd_, IDT_REFRESH_TIMER, settings_.refresh_interval_seconds * 1000, nullptr);
  }
}

void MainWindow::RefreshData() {
  if (current_tab_ == MainTab::kServices) {
    RefreshServicesData();
    return;
  }

  auto result = snapshot_service_.GetSnapshot(BuildProcessSnapshotOptions(settings_));
  all_processes_ = std::move(result.items);
  totals_ = result.totals;

  // Run Process Monitoring against the full process list (always uses all_processes_)
  monitor_service_.CheckProcesses(all_processes_, hwnd_, 1);

  UpdateStatusLabels();

  // Apply display condition filter, then text filter + display
  ApplyConditionFilter();
  ApplyFilterAndDisplay();
}

void MainWindow::ApplyConditionFilter() {
  display_processes_.clear();
  display_processes_.reserve(all_processes_.size());

  for (const auto& proc : all_processes_) {
    // 1. Exclude processes specified in user settings (e.g. Memory Compression, Secure System)
    if (IsProcessExcluded(proc->name, settings_.excluded_processes)) {
      continue;
    }

    // 2. Evaluate condition filter if enabled
    if (settings_.display_filter_enabled) {
      if (!settings_.display_filter_condition.Evaluate(*proc)) {
        continue;
      }
    }

    display_processes_.push_back(proc);
  }
}

void MainWindow::OnFilterConditionChanged() {
  // Read column selection via shared table
  int col_sel = static_cast<int>(SendMessageW(filter_col_combo_, CB_GETCURSEL, 0, 0));
  settings_.display_filter_condition.column_id = GetFilterColumnId(col_sel);
  bool is_bytes = IsMemoryBytesColumn(settings_.display_filter_condition.column_id);

  // Read operator selection
  int op_sel = static_cast<int>(SendMessageW(filter_op_combo_, CB_GETCURSEL, 0, 0));
  ComparisonOperator ops[] = {
    ComparisonOperator::kGreaterThan, ComparisonOperator::kGreaterThanOrEqual,
    ComparisonOperator::kLessThan, ComparisonOperator::kLessThanOrEqual,
    ComparisonOperator::kEqual, ComparisonOperator::kNotEqual,
    ComparisonOperator::kContains,
  };
  if (op_sel >= 0 && op_sel < static_cast<int>(std::size(ops))) {
    settings_.display_filter_condition.op = ops[op_sel];
  }

  // Read value text
  wchar_t val_buf[256] = {0};
  GetWindowTextW(filter_val_edit_, val_buf, static_cast<int>(std::size(val_buf)));
  std::wstring val_str = val_buf;

  if (val_str.empty()) {
    settings_.display_filter_enabled = false;
    settings_.display_filter_condition.numeric_value = 0.0;
    settings_.display_filter_condition.string_value.clear();
  } else {
    settings_.display_filter_enabled = true;
    if (settings_.display_filter_condition.op == ComparisonOperator::kContains) {
      settings_.display_filter_condition.string_value = val_str;
      settings_.display_filter_condition.numeric_value = 0.0;
    } else {
      wchar_t* end_ptr = nullptr;
      double numeric = std::wcstod(val_str.c_str(), &end_ptr);
      if (end_ptr != val_str.c_str()) {
        // For memory columns, user input is in KiB (K)
        settings_.display_filter_condition.numeric_value = is_bytes ? numeric * 1024.0 : numeric;
        settings_.display_filter_condition.string_value = val_str;
      } else {
        settings_.display_filter_condition.string_value = val_str;
        settings_.display_filter_condition.numeric_value = 0.0;
      }
    }
  }

  ScheduleFilterSettingsSave();
  ApplyConditionFilter();
  ApplyFilterAndDisplay();
  if (filter_val_edit_) {
    InvalidateRect(filter_val_edit_, nullptr, TRUE);
  }
}

void MainWindow::UpdateStatusLabels() {
  wchar_t buf[128];

  if (current_tab_ == MainTab::kServices) {
    DWORD running_count = 0;
    DWORD stopped_count = 0;
    for (const auto& svc : all_services_) {
      if (svc->state == SERVICE_RUNNING) running_count++;
      else if (svc->state == SERVICE_STOPPED) stopped_count++;
    }

    swprintf_s(buf, LanguageManager::GetString(StringId::kStatusServiceCounts),
               running_count, static_cast<UINT>(all_services_.size()));
    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));

    std::wstring stopped_label = LanguageManager::GetString(StringId::kServiceStateStopped);
    swprintf_s(buf, L"%s: %u", stopped_label.c_str(), stopped_count);
    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(buf));

    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(L""));

    swprintf_s(buf, LanguageManager::GetString(StringId::kStatusCpu), totals_.total_cpu_usage);
    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(buf));

    if (totals_.total_physical_memory > 0) {
      double used_gb = static_cast<double>(totals_.used_physical_memory) / (1024.0 * 1024 * 1024);
      double total_gb = static_cast<double>(totals_.total_physical_memory) / (1024.0 * 1024 * 1024);
      int percent = static_cast<int>((totals_.used_physical_memory * 100) / totals_.total_physical_memory);
      swprintf_s(buf, LanguageManager::GetString(StringId::kStatusMemory), used_gb, total_gb, percent);
      SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(buf));
    }

    if (totals_.total_commit_limit > 0) {
      double commit_gb = static_cast<double>(totals_.total_committed) / (1024.0 * 1024 * 1024);
      double limit_gb = static_cast<double>(totals_.total_commit_limit) / (1024.0 * 1024 * 1024);
      swprintf_s(buf, LanguageManager::GetString(StringId::kStatusCommit), commit_gb, limit_gb);
      SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 5, reinterpret_cast<LPARAM>(buf));
    }
    UpdateTrayTooltip();
    return;
  }

  swprintf_s(buf, LanguageManager::GetString(StringId::kStatusProcesses), totals_.total_processes);
  SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));

  swprintf_s(buf, LanguageManager::GetString(StringId::kStatusThreads), totals_.total_threads);
  SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(buf));

  swprintf_s(buf, LanguageManager::GetString(StringId::kStatusHandles), totals_.total_handles);
  SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(buf));

  swprintf_s(buf, LanguageManager::GetString(StringId::kStatusCpu), totals_.total_cpu_usage);
  SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(buf));

  if (totals_.total_physical_memory > 0) {
    double used_gb = static_cast<double>(totals_.used_physical_memory) / (1024.0 * 1024 * 1024);
    double total_gb = static_cast<double>(totals_.total_physical_memory) / (1024.0 * 1024 * 1024);
    int percent = static_cast<int>((totals_.used_physical_memory * 100) / totals_.total_physical_memory);
    swprintf_s(buf, LanguageManager::GetString(StringId::kStatusMemory), used_gb, total_gb, percent);
    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(buf));
  }

  if (totals_.total_commit_limit > 0) {
    double commit_gb = static_cast<double>(totals_.total_committed) / (1024.0 * 1024 * 1024);
    double limit_gb = static_cast<double>(totals_.total_commit_limit) / (1024.0 * 1024 * 1024);
    swprintf_s(buf, LanguageManager::GetString(StringId::kStatusCommit), commit_gb, limit_gb);
    SendMessageW(statusbar_hwnd_, SB_SETTEXTW, 5, reinterpret_cast<LPARAM>(buf));
  }
  UpdateTrayTooltip();
}

void MainWindow::ApplyFilterAndDisplay() {
  if (list_view_data_current_) {
    saved_list_view_state_ = CaptureListViewState();
  }
  if (tree_view_data_current_) {
    saved_tree_view_state_ = CaptureTreeViewState();
  }

  wchar_t filter_buf[256] = {0};
  GetWindowTextW(search_edit_, filter_buf, static_cast<int>(std::size(filter_buf)));
  std::wstring query = filter_buf;
  if (IsSearchPlaceholderOrEmpty(query)) {
    query.clear();
  }

  // Source is display_processes_ (condition-filtered subset of all_processes_)
  if (query.empty()) {
    filtered_processes_ = display_processes_;
  } else {
    filtered_processes_.clear();
    for (const auto& proc : display_processes_) {
      if (CaseInsensitiveContains(proc->name, query) ||
          CaseInsensitiveContains(std::to_wstring(proc->process_id), query) ||
          CaseInsensitiveContains(proc->description, query) ||
          CaseInsensitiveContains(proc->user_name, query) ||
          CaseInsensitiveContains(proc->file_path, query)) {
        filtered_processes_.push_back(proc);
      }
    }
  }

  if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
    UpdateTreeView(saved_tree_view_state_ ? &saved_tree_view_state_.value() : nullptr);
    tree_view_data_current_ = true;
    list_view_data_current_ = false;
  } else {
    UpdateListView(saved_list_view_state_ ? &saved_list_view_state_.value() : nullptr);
    list_view_data_current_ = true;
    tree_view_data_current_ = false;
  }
}

void MainWindow::ScheduleFilterSettingsSave() {
  filter_settings_save_pending_ = true;
  KillTimer(hwnd_, IDT_FILTER_SAVE_TIMER);
  if (SetTimer(hwnd_, IDT_FILTER_SAVE_TIMER,
               kFilterSaveDebounceMilliseconds, nullptr) == 0) {
    FlushPendingFilterSettingsSave();
  }
}

void MainWindow::FlushPendingFilterSettingsSave() {
  KillTimer(hwnd_, IDT_FILTER_SAVE_TIMER);
  if (!filter_settings_save_pending_) {
    return;
  }

  SaveSettingsPreservingPendingTheme();
  filter_settings_save_pending_ = false;
}

void MainWindow::SaveSettingsPreservingPendingTheme() const {
  if (!pending_theme_change_.has_value()) {
    settings_.SaveSettings();
    return;
  }

  AppSettings persisted_settings = settings_;
  persisted_settings.theme = pending_theme_change_.value();
  persisted_settings.SaveSettings();
}

void MainWindow::SortItems() {
  std::vector<ProcessColumnInfo> visible_cols;
  for (const auto& col : settings_.columns) {
    if (col.visible) visible_cols.push_back(col);
  }

  if (sort_column_index_ < 0 || sort_column_index_ >= static_cast<int>(visible_cols.size())) {
    return;
  }

  auto col_id = visible_cols[sort_column_index_].id;

  auto comparator = [col_id](const std::shared_ptr<ProcessItem>& a,
                             const std::shared_ptr<ProcessItem>& b) -> bool {
    switch (col_id) {
      case ProcessColumnId::kName:
        return _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
      case ProcessColumnId::kPid:
        return a->process_id < b->process_id;
      case ProcessColumnId::kStatus:
        return a->status < b->status;
      case ProcessColumnId::kUserName:
        return _wcsicmp(a->user_name.c_str(), b->user_name.c_str()) < 0;
      case ProcessColumnId::kCpu:
        return a->cpu_percent < b->cpu_percent;
      case ProcessColumnId::kPrivateWorkingSet:
        return a->private_working_set < b->private_working_set;
      case ProcessColumnId::kWorkingSet:
        return a->working_set < b->working_set;
      case ProcessColumnId::kPeakWorkingSet:
        return a->peak_working_set < b->peak_working_set;
      case ProcessColumnId::kWorkingSetDelta:
        return a->working_set_delta < b->working_set_delta;
      case ProcessColumnId::kCommitSize:
        return a->commit_size < b->commit_size;
      case ProcessColumnId::kPagedPool:
        return a->paged_pool < b->paged_pool;
      case ProcessColumnId::kNonPagedPool:
        return a->non_paged_pool < b->non_paged_pool;
      case ProcessColumnId::kBasePriority:
        return static_cast<uint32_t>(a->priority) < static_cast<uint32_t>(b->priority);
      case ProcessColumnId::kHandles:
        return a->handle_count < b->handle_count;
      case ProcessColumnId::kThreads:
        return a->thread_count < b->thread_count;
      case ProcessColumnId::kUserObjects:
        return a->user_objects < b->user_objects;
      case ProcessColumnId::kGdiObjects:
        return a->gdi_objects < b->gdi_objects;
      case ProcessColumnId::kIoReadCount:
        return a->io_read_count < b->io_read_count;
      case ProcessColumnId::kIoWriteCount:
        return a->io_write_count < b->io_write_count;
      case ProcessColumnId::kIoOtherCount:
        return a->io_other_count < b->io_other_count;
      case ProcessColumnId::kIoReadBytes:
        return a->io_read_bytes < b->io_read_bytes;
      case ProcessColumnId::kIoWriteBytes:
        return a->io_write_bytes < b->io_write_bytes;
      case ProcessColumnId::kIoOtherBytes:
        return a->io_other_bytes < b->io_other_bytes;
      case ProcessColumnId::kFilePath:
        return _wcsicmp(a->file_path.c_str(), b->file_path.c_str()) < 0;
      case ProcessColumnId::kCommandLine:
        return _wcsicmp(a->command_line.c_str(), b->command_line.c_str()) < 0;
      case ProcessColumnId::kOsContext:
        return a->os_context < b->os_context;
      case ProcessColumnId::kPlatform:
        return a->platform < b->platform;
      case ProcessColumnId::kElevated:
        return a->elevated < b->elevated;
      case ProcessColumnId::kUacVirtualization:
        return a->uac_virtualization < b->uac_virtualization;
      case ProcessColumnId::kDescription:
        return _wcsicmp(a->description.c_str(), b->description.c_str()) < 0;
      case ProcessColumnId::kDepStatus:
        return a->dep_status < b->dep_status;
      case ProcessColumnId::kEnterpriseContext:
        return _wcsicmp(a->enterprise_context.c_str(), b->enterprise_context.c_str()) < 0;
      case ProcessColumnId::kDpiAwareness:
        return _wcsicmp(a->dpi_awareness.c_str(), b->dpi_awareness.c_str()) < 0;
      case ProcessColumnId::kPackageName:
        return _wcsicmp(a->package_name.c_str(), b->package_name.c_str()) < 0;
      case ProcessColumnId::kArchitecture:
        return _wcsicmp(a->architecture.c_str(), b->architecture.c_str()) < 0;
      case ProcessColumnId::kGpuUsage:
        return a->gpu_percent < b->gpu_percent;
      case ProcessColumnId::kGpuEngine:
        return _wcsicmp(a->gpu_engine.c_str(), b->gpu_engine.c_str()) < 0;
      case ProcessColumnId::kDedicatedGpuMemory:
        return a->dedicated_gpu_memory < b->dedicated_gpu_memory;
      case ProcessColumnId::kSharedGpuMemory:
        return a->shared_gpu_memory < b->shared_gpu_memory;
      case ProcessColumnId::kSessionId:
        return a->session_id < b->session_id;
      case ProcessColumnId::kCreateTime:
        if (a->start_time.has_value() && b->start_time.has_value()) {
          return CompareFileTime(&a->start_time.value(), &b->start_time.value()) < 0;
        }
        return a->start_time.has_value();
      default:
        return a->process_id < b->process_id;
    }
  };

  if (sort_ascending_) {
    std::sort(filtered_processes_.begin(), filtered_processes_.end(), comparator);
  } else {
    std::sort(filtered_processes_.begin(), filtered_processes_.end(),
              [&comparator](const auto& a, const auto& b) { return comparator(b, a); });
  }
}

std::optional<MainWindow::ProcessIdentity> MainWindow::GetProcessIdentity(
    const ProcessItem& process) {
  if (!process.start_time.has_value()) {
    return std::nullopt;
  }

  ULARGE_INTEGER creation_time{};
  creation_time.LowPart = process.start_time->dwLowDateTime;
  creation_time.HighPart = process.start_time->dwHighDateTime;
  return ProcessIdentity{process.process_id, creation_time.QuadPart};
}

bool MainWindow::IsSameProcessIdentity(const ProcessIdentity& lhs,
                                       const ProcessIdentity& rhs) {
  return lhs.process_id == rhs.process_id &&
         lhs.creation_time == rhs.creation_time;
}

MainWindow::ProcessListViewState MainWindow::CaptureListViewState() const {
  ProcessListViewState state;

  int selected_index = -1;
  while ((selected_index = ListView_GetNextItem(
              listview_hwnd_, selected_index, LVNI_SELECTED)) != -1) {
    if (selected_index >= static_cast<int>(filtered_processes_.size())) {
      continue;
    }
    auto identity = GetProcessIdentity(*filtered_processes_[selected_index]);
    if (identity.has_value()) {
      state.selected_items.push_back(identity.value());
    }
  }

  int focused_index = ListView_GetNextItem(listview_hwnd_, -1, LVNI_FOCUSED);
  if (focused_index >= 0 &&
      focused_index < static_cast<int>(filtered_processes_.size())) {
    state.focused_item = GetProcessIdentity(*filtered_processes_[focused_index]);
  }

  int top_index = ListView_GetTopIndex(listview_hwnd_);
  if (top_index >= 0 && top_index < static_cast<int>(filtered_processes_.size())) {
    state.top_item = GetProcessIdentity(*filtered_processes_[top_index]);
  }

  return state;
}

MainWindow::ProcessTreeViewState MainWindow::CaptureTreeViewState() const {
  ProcessTreeViewState state;
  std::vector<HTREEITEM> pending_items;
  for (HTREEITEM root = TreeView_GetRoot(treeview_hwnd_); root != nullptr;
       root = TreeView_GetNextSibling(treeview_hwnd_, root)) {
    pending_items.push_back(root);
  }
  state.had_items = !pending_items.empty();

  auto get_identity = [this](HTREEITEM item) -> std::optional<ProcessIdentity> {
    TVITEMW tree_item{};
    tree_item.mask = TVIF_PARAM;
    tree_item.hItem = item;
    if (!TreeView_GetItem(treeview_hwnd_, &tree_item) || tree_item.lParam == 0) {
      return std::nullopt;
    }
    return GetProcessIdentity(*reinterpret_cast<ProcessItem*>(tree_item.lParam));
  };

  while (!pending_items.empty()) {
    HTREEITEM item = pending_items.back();
    pending_items.pop_back();

    auto identity = get_identity(item);
    if (identity.has_value() &&
        (TreeView_GetItemState(treeview_hwnd_, item, TVIS_EXPANDED) & TVIS_EXPANDED) != 0) {
      state.expanded_items.push_back(identity.value());
    }

    for (HTREEITEM child = TreeView_GetChild(treeview_hwnd_, item); child != nullptr;
         child = TreeView_GetNextSibling(treeview_hwnd_, child)) {
      pending_items.push_back(child);
    }
  }

  HTREEITEM selected_item = TreeView_GetSelection(treeview_hwnd_);
  if (selected_item != nullptr) {
    state.selected_item = get_identity(selected_item);
  }

  HTREEITEM first_visible_item = TreeView_GetFirstVisible(treeview_hwnd_);
  if (first_visible_item != nullptr) {
    state.first_visible_item = get_identity(first_visible_item);
  }

  return state;
}

void MainWindow::UpdateListView(const ProcessListViewState* preserved_state) {
  ProcessListViewState state;
  if (preserved_state != nullptr) {
    state = *preserved_state;
  } else if (list_view_data_current_) {
    state = CaptureListViewState();
  } else if (saved_list_view_state_.has_value()) {
    state = saved_list_view_state_.value();
  }
  SortItems();

  SendMessageW(listview_hwnd_, WM_SETREDRAW, FALSE, 0);
  ListView_SetItemCountEx(listview_hwnd_, static_cast<int>(filtered_processes_.size()),
                          LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
  ListView_SetItemState(listview_hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);

  auto find_index = [this](const ProcessIdentity& identity) {
    for (size_t i = 0; i < filtered_processes_.size(); ++i) {
      auto candidate = GetProcessIdentity(*filtered_processes_[i]);
      if (candidate.has_value() &&
          IsSameProcessIdentity(candidate.value(), identity)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  for (const auto& identity : state.selected_items) {
    int index = find_index(identity);
    if (index >= 0) {
      ListView_SetItemState(listview_hwnd_, index, LVIS_SELECTED, LVIS_SELECTED);
    }
  }

  if (state.focused_item.has_value()) {
    int index = find_index(state.focused_item.value());
    if (index >= 0) {
      ListView_SetItemState(listview_hwnd_, index, LVIS_FOCUSED, LVIS_FOCUSED);
    }
  }

  if (state.top_item.has_value() && !filtered_processes_.empty()) {
    int target_index = find_index(state.top_item.value());
    RECT item_rect{};
    if (target_index >= 0 &&
        ListView_GetItemRect(listview_hwnd_, 0, &item_rect, LVIR_BOUNDS)) {
      int current_top = ListView_GetTopIndex(listview_hwnd_);
      int row_height = item_rect.bottom - item_rect.top;
      ListView_Scroll(listview_hwnd_, 0,
                      (target_index - current_top) * row_height);
    }
  }

  SendMessageW(listview_hwnd_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(listview_hwnd_, nullptr, TRUE);
  list_view_data_current_ = true;
}

void MainWindow::UpdateTreeView(const ProcessTreeViewState* preserved_state) {
  SendMessageW(treeview_hwnd_, WM_SETREDRAW, FALSE, 0);
  TreeView_DeleteAllItems(treeview_hwnd_);

  std::vector<TreeNodeHandle> node_handles;
  auto root_items = ProcessSnapshotService::BuildProcessTree(filtered_processes_);
  for (const auto& root : root_items) {
    AddTreeNode(TVI_ROOT, root, &node_handles);
  }

  auto find_item = [&node_handles](const ProcessIdentity& identity) {
    for (const auto& node : node_handles) {
      if (IsSameProcessIdentity(node.identity, identity)) {
        return node.item;
      }
    }
    return static_cast<HTREEITEM>(nullptr);
  };

  if (preserved_state == nullptr || !preserved_state->had_items) {
    std::vector<HTREEITEM> pending_items;
    for (HTREEITEM root = TreeView_GetRoot(treeview_hwnd_); root != nullptr;
         root = TreeView_GetNextSibling(treeview_hwnd_, root)) {
      pending_items.push_back(root);
    }
    while (!pending_items.empty()) {
      HTREEITEM item = pending_items.back();
      pending_items.pop_back();
      TreeView_Expand(treeview_hwnd_, item, TVE_EXPAND);
      for (HTREEITEM child = TreeView_GetChild(treeview_hwnd_, item); child != nullptr;
           child = TreeView_GetNextSibling(treeview_hwnd_, child)) {
        pending_items.push_back(child);
      }
    }
  } else {
    for (const auto& identity : preserved_state->expanded_items) {
      HTREEITEM item = find_item(identity);
      if (item != nullptr) {
        TreeView_Expand(treeview_hwnd_, item, TVE_EXPAND);
      }
    }

    if (preserved_state->selected_item.has_value()) {
      HTREEITEM item = find_item(preserved_state->selected_item.value());
      if (item != nullptr) {
        TreeView_SelectItem(treeview_hwnd_, item);
      }
    }

    if (preserved_state->first_visible_item.has_value()) {
      HTREEITEM item = find_item(preserved_state->first_visible_item.value());
      if (item != nullptr) {
        SendMessageW(treeview_hwnd_, TVM_SELECTITEM, TVGN_FIRSTVISIBLE,
                     reinterpret_cast<LPARAM>(item));
      }
    }
  }

  SendMessageW(treeview_hwnd_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(treeview_hwnd_, nullptr, TRUE);
  tree_view_data_current_ = true;
}

void MainWindow::AddTreeNode(HTREEITEM parent_node,
                             const std::shared_ptr<ProcessItem>& process,
                             std::vector<TreeNodeHandle>* node_handles) {
  std::wstring mem_label = LanguageManager::GetColumnHeaderText(ProcessColumnId::kWorkingSet);
  wchar_t text[512];
  swprintf_s(text, L"%s (PID: %u) - CPU: %s, %s: %s",
             process->name.c_str(),
             process->process_id,
             process->GetFormattedCpu().c_str(),
             mem_label.c_str(),
             process->GetFormattedWorkingSet().c_str());

  TVINSERTSTRUCTW tvis = {0};
  tvis.hParent = parent_node;
  tvis.hInsertAfter = TVI_LAST;
  tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
  tvis.item.pszText = text;
  tvis.item.iImage = I_IMAGECALLBACK;
  tvis.item.iSelectedImage = I_IMAGECALLBACK;
  tvis.item.lParam = reinterpret_cast<LPARAM>(process.get());

  HTREEITEM item = TreeView_InsertItem(treeview_hwnd_, &tvis);

  auto identity = GetProcessIdentity(*process);
  if (item != nullptr && identity.has_value()) {
    node_handles->push_back(TreeNodeHandle{identity.value(), item});
  }

  for (const auto& child : process->children) {
    AddTreeNode(item, child, node_handles);
  }
}

std::shared_ptr<ProcessItem> MainWindow::GetSelectedProcess() {
  if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
    HTREEITEM sel = TreeView_GetSelection(treeview_hwnd_);
    if (!sel) return nullptr;

    TVITEMW tvi = {0};
    tvi.mask = TVIF_PARAM;
    tvi.hItem = sel;
    if (TreeView_GetItem(treeview_hwnd_, &tvi) && tvi.lParam != 0) {
      auto* raw_ptr = reinterpret_cast<ProcessItem*>(tvi.lParam);
      for (const auto& proc : all_processes_) {
        if (proc.get() == raw_ptr) return proc;
      }
    }
  } else {
    int sel = ListView_GetNextItem(listview_hwnd_, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(filtered_processes_.size())) {
      return filtered_processes_[sel];
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<ProcessItem>> MainWindow::GetSelectedProcesses() {
  std::vector<std::shared_ptr<ProcessItem>> selected;
  if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
    auto single = GetSelectedProcess();
    if (single) selected.push_back(single);
  } else {
    int sel_idx = -1;
    while ((sel_idx = ListView_GetNextItem(listview_hwnd_, sel_idx, LVNI_SELECTED)) != -1) {
      if (sel_idx >= 0 && sel_idx < static_cast<int>(filtered_processes_.size())) {
        selected.push_back(filtered_processes_[sel_idx]);
      }
    }
  }
  return selected;
}

void MainWindow::EndSelectedProcess() {
  auto procs = GetSelectedProcesses();
  if (procs.empty()) return;

  std::vector<std::shared_ptr<ProcessItem>> valid_procs;
  for (const auto& p : procs) {
    if (p && p->process_id != 0) {
      valid_procs.push_back(p);
    }
  }
  if (valid_procs.empty()) return;

  wchar_t msg[512];
  if (valid_procs.size() == 1) {
    swprintf_s(msg, LanguageManager::GetString(StringId::kMsgConfirmEndProcess),
               valid_procs[0]->name.c_str(), valid_procs[0]->process_id);
  } else {
    swprintf_s(msg, LanguageManager::GetString(StringId::kMsgConfirmEndProcessMultiple),
               valid_procs.size());
  }

  if (MessageBoxW(hwnd_, msg, LanguageManager::GetString(StringId::kTitleConfirmEndProcess),
                  MB_YESNO | MB_ICONWARNING) == IDYES) {
    int fail_count = 0;
    for (const auto& p : valid_procs) {
      if (!ProcessSnapshotService::TerminateProcess(*p)) {
        fail_count++;
      }
    }
    RefreshData();
    if (fail_count > 0) {
      MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgEndProcessError),
                  LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
    }
  }
}

void MainWindow::EndSelectedProcessTree() {
  auto procs = GetSelectedProcesses();
  if (procs.empty()) return;

  std::vector<std::shared_ptr<ProcessItem>> valid_procs;
  for (const auto& p : procs) {
    if (p && p->process_id != 0) {
      valid_procs.push_back(p);
    }
  }
  if (valid_procs.empty()) return;

  wchar_t msg[512];
  if (valid_procs.size() == 1) {
    swprintf_s(msg, LanguageManager::GetString(StringId::kMsgConfirmEndTree),
               valid_procs[0]->name.c_str(), valid_procs[0]->process_id);
  } else {
    swprintf_s(msg, LanguageManager::GetString(StringId::kMsgConfirmEndProcessMultiple),
               valid_procs.size());
  }

  if (MessageBoxW(hwnd_, msg, LanguageManager::GetString(StringId::kTitleConfirmEndTree),
                  MB_YESNO | MB_ICONWARNING) == IDYES) {
    int fail_count = 0;
    for (const auto& p : valid_procs) {
      if (!snapshot_service_.TerminateProcessTree(*p, all_processes_)) {
        fail_count++;
      }
    }
    RefreshData();
    if (fail_count > 0) {
      MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgEndTreePartial),
                  LanguageManager::GetString(StringId::kTitleNotice), MB_OK | MB_ICONINFORMATION);
    }
  }
}

void MainWindow::SetSelectedProcessPriority(ProcessPriorityClass priority) {
  auto proc = GetSelectedProcess();
  if (!proc) return;

  if ((static_cast<uint32_t>(priority) & static_cast<uint32_t>(ProcessPriorityClass::kRealtime)) != 0) {
    if (MessageBoxW(hwnd_,
                    LanguageManager::GetString(StringId::kMsgWarnRealtime),
                    LanguageManager::GetString(StringId::kTitleChangePriority),
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return;
    }
  }

  if (ProcessSnapshotService::SetPriority(*proc, priority)) {
    proc->priority = priority;
    RefreshData();
  } else {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgChangePriorityError),
                LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
  }
}

void MainWindow::OpenSelectedFileLocation() {
  auto proc = GetSelectedProcess();
  if (!proc || proc->file_path.empty()) {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgFileNotFound),
                LanguageManager::GetString(StringId::kTitleInfo), MB_OK | MB_ICONINFORMATION);
    return;
  }

  if (FAILED(OpenFolderAndSelectFile(proc->file_path))) {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgFileNotFound),
                LanguageManager::GetString(StringId::kTitleInfo), MB_OK | MB_ICONINFORMATION);
  }
}

void MainWindow::SearchSelectedProcessOnline() {
  auto proc = GetSelectedProcess();
  if (!proc) return;

  auto encoded_query = PercentEncodeUrlComponent(proc->name + L" process");
  if (!encoded_query.has_value()) return;

  std::wstring url = L"https://www.google.com/search?q=" + *encoded_query;
  ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MainWindow::ShowSelectedProcessProperties() {
  auto proc = GetSelectedProcess();
  if (!proc) return;

  std::wostringstream oss;
  oss << LanguageManager::GetString(StringId::kPropertiesHeader)
      << LanguageManager::GetString(StringId::kPropImageName) << proc->name << L"\n"
      << LanguageManager::GetString(StringId::kPropPid) << proc->process_id
      << LanguageManager::GetString(StringId::kPropParentPid) << proc->parent_process_id << L")\n"
      << LanguageManager::GetString(StringId::kPropStatus)
      << proc->GetColumnValue(ProcessColumnId::kStatus) << L"\n"
      << LanguageManager::GetString(StringId::kPropUser) << proc->user_name << L"\n"
      << LanguageManager::GetString(StringId::kPropDescription) << proc->description << L"\n"
      << LanguageManager::GetString(StringId::kPropArchitecture) << proc->architecture << L"\n"
      << LanguageManager::GetString(StringId::kPropPriority) << proc->GetFormattedPriority() << L"\n"
      << LanguageManager::GetString(StringId::kPropThreads) << proc->thread_count << L"\n"
      << LanguageManager::GetString(StringId::kPropHandles) << proc->handle_count << L"\n"
      << LanguageManager::GetString(StringId::kPropCpu) << proc->GetFormattedCpu() << L"\n"
      << LanguageManager::GetString(StringId::kPropWorkingSet) << proc->GetFormattedWorkingSet() << L"\n"
      << LanguageManager::GetString(StringId::kPropCommit) << proc->GetFormattedCommitSize() << L"\n"
      << LanguageManager::GetString(StringId::kPropStartTime) << proc->GetFormattedStartTime() << L"\n"
      << LanguageManager::GetString(StringId::kPropFilePath) << proc->file_path;

  std::wstring title = LanguageManager::GetString(StringId::kPropertiesTitlePrefix) + proc->name;
  MessageBoxW(hwnd_, oss.str().c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::SetClipboardText(const std::wstring& text) {
  if (text.empty()) return;
  if (OpenClipboard(hwnd_)) {
    EmptyClipboard();
    size_t bytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hmem) {
      void* dest = GlobalLock(hmem);
      if (dest) {
        memcpy(dest, text.c_str(), bytes);
        GlobalUnlock(hmem);
        SetClipboardData(CF_UNICODETEXT, hmem);
      } else {
        GlobalFree(hmem);
      }
    }
    CloseClipboard();
  }
}

void MainWindow::CopySelectedInfo(ProcessColumnId col_id) {
  auto proc = GetSelectedProcess();
  if (!proc) return;
  SetClipboardText(proc->GetColumnValue(col_id));
}

void MainWindow::CopySelectedAsJson() {
  auto selected_procs = GetSelectedProcesses();
  if (selected_procs.empty()) return;
  SetClipboardText(BuildProcessJson(selected_procs, settings_.columns));
}

void MainWindow::CopySelectedAsTsv() {
  auto selected_procs = GetSelectedProcesses();
  if (selected_procs.empty()) return;
  SetClipboardText(BuildProcessTsv(selected_procs, settings_.columns));
}

void MainWindow::ExportSelectedAsJson() {
  auto selected_procs = GetSelectedProcesses();
  if (selected_procs.empty()) return;
  ExportSaveResult result = PromptAndSaveExportFile(
      hwnd_, BuildProcessJson(selected_procs, settings_.columns),
      L"processes.json", L"json", StringId::kFileFilterJson,
      StringId::kFileDialogTitleJson, false);
  if (result == ExportSaveResult::kError) {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgExportFailed),
                LanguageManager::GetString(StringId::kTitleError),
                MB_OK | MB_ICONERROR);
  }
}

void MainWindow::ExportSelectedAsTsv() {
  auto selected_procs = GetSelectedProcesses();
  if (selected_procs.empty()) return;
  ExportSaveResult result = PromptAndSaveExportFile(
      hwnd_, BuildProcessTsv(selected_procs, settings_.columns),
      L"processes.tsv", L"tsv", StringId::kFileFilterTsv,
      StringId::kFileDialogTitleTsv, true);
  if (result == ExportSaveResult::kError) {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgExportFailed),
                LanguageManager::GetString(StringId::kTitleError),
                MB_OK | MB_ICONERROR);
  }
}

void MainWindow::OpenMonitorSettings() {
  MonitorDialog dlg(hwnd_, settings_.monitor_rules, settings_.theme, ui_font_);
  if (dlg.Show()) {
    settings_.monitor_rules = dlg.GetRules();
    monitor_service_.SetRules(settings_.monitor_rules);
    settings_.SaveMonitorRules();
  }
}

void MainWindow::ApplyTheme() {
  ThemeManager::ApplyTheme(hwnd_, settings_.theme);

  const auto& palette = ThemeManager::GetPalette(settings_.theme);
  bool is_dark = (settings_.theme == AppTheme::kDark);

  auto apply_control_theme = [&](HWND control) {
    if (control) {
      SetWindowTheme(control,
                     is_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
  };
  auto apply_combo_theme = [&](HWND control) {
    if (control) {
      SetWindowTheme(control,
                     is_dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
    }
  };
  apply_control_theme(listview_hwnd_);
  apply_control_theme(treeview_hwnd_);
  apply_control_theme(statusbar_hwnd_);
  apply_control_theme(search_edit_);
  apply_combo_theme(interval_combo_);
  apply_combo_theme(filter_col_combo_);
  apply_combo_theme(filter_op_combo_);
  apply_control_theme(filter_val_edit_);
  apply_control_theme(filter_clear_btn_);
  apply_control_theme(tab_control_);
  apply_control_theme(service_list_view_);
  apply_control_theme(tooltip_hwnd_);
  apply_control_theme(btn_tree_);
  apply_control_theme(btn_columns_);
  apply_control_theme(btn_restart_admin_);
  apply_control_theme(btn_topmost_);
  apply_control_theme(btn_options_);
  apply_control_theme(btn_monitor_);
  apply_control_theme(btn_refresh_);
  apply_control_theme(btn_endtask_);

  // Set ListView theme colors
  ListView_SetBkColor(listview_hwnd_, palette.surface_background);
  ListView_SetTextBkColor(listview_hwnd_, palette.surface_background);
  ListView_SetTextColor(listview_hwnd_, palette.text_primary);

  if (service_list_view_) {
    ListView_SetBkColor(service_list_view_, palette.surface_background);
    ListView_SetTextBkColor(service_list_view_, palette.surface_background);
    ListView_SetTextColor(service_list_view_, palette.text_primary);
    ThemeManager::ApplyListViewHeaderTheme(service_list_view_, settings_.theme, service_sort_column_, service_sort_ascending_);
  }

  // Set TreeView theme colors
  TreeView_SetBkColor(treeview_hwnd_, palette.surface_background);
  TreeView_SetTextColor(treeview_hwnd_, palette.text_primary);
  TreeView_SetLineColor(treeview_hwnd_, palette.border_color);

  ThemeManager::ApplyListViewHeaderTheme(listview_hwnd_, settings_.theme, sort_column_index_, sort_ascending_);

  DrawMenuBar(hwnd_);
  RedrawWindow(hwnd_, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void MainWindow::RestartApplication() {
  FlushPendingFilterSettingsSave();

  wchar_t exe_path[MAX_PATH] = {0};
  DWORD path_length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (path_length == 0 || path_length >= MAX_PATH) {
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgRestartFailed),
                LanguageManager::GetString(StringId::kTitleError),
                MB_OK | MB_ICONERROR);
    return;
  }

  // Release the single-instance lock before spawning the new copy, otherwise
  // it can start and find the mutex still held by this (exiting) process and
  // quit immediately, making the restart appear to do nothing.
  ReleaseSingleInstanceLock();

  SHELLEXECUTEINFOW sei = {sizeof(SHELLEXECUTEINFOW)};
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.hwnd = hwnd_;
  sei.lpVerb = L"open";
  sei.lpFile = exe_path;
  sei.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&sei)) {
    HANDLE mutex = CreateMutexW(
        nullptr, TRUE, L"Local\\LiteProcManager_SingleInstance_Mutex");
    if (mutex != nullptr) {
      RestoreSingleInstanceLock(mutex);
    }
    MessageBoxW(hwnd_, LanguageManager::GetString(StringId::kMsgRestartFailed),
                LanguageManager::GetString(StringId::kTitleError),
                MB_OK | MB_ICONERROR);
    return;
  }
  if (sei.hProcess != nullptr) {
    CloseHandle(sei.hProcess);
  }

  // Use the regular shutdown path so timers, worker threads, and owned
  // resources are cleaned up before this process exits.
  DestroyWindow(hwnd_);
}

void MainWindow::RestartAsAdministrator() {
  if (is_elevated_) return;

  FlushPendingFilterSettingsSave();

  wchar_t exe_path[MAX_PATH] = {0};
  if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0) {
    SHELLEXECUTEINFOW sei = {sizeof(SHELLEXECUTEINFOW)};
    sei.lpVerb = L"runas";  // Prompt UAC elevation dialog
    sei.lpFile = exe_path;
    sei.hwnd = hwnd_;
    sei.nShow = SW_SHOWNORMAL;

    // Release the single-instance lock first. The elevated child can start
    // probing for it while this process is still tearing down; if the mutex
    // is still held at that moment, the child sees "already running" and
    // exits immediately without ever showing an elevated window.
    ReleaseSingleInstanceLock();

    if (ShellExecuteExW(&sei)) {
      if (notify_icon_data_.cbSize > 0) {
        Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_);
      }
      ExitProcess(0);
    }
    // Elevation was cancelled/denied (e.g. user clicked "No" on the UAC
    // prompt) - this process keeps running, so re-acquire the lock rather
    // than leaving the app unprotected against a duplicate launch.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LiteProcManager_SingleInstance_Mutex");
    if (mutex != nullptr) {
      RestoreSingleInstanceLock(mutex);
    }
  }
}

bool MainWindow::IsCurrentProcessElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation = {0};
  DWORD ret_len = 0;
  bool elevated = false;
  if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &ret_len)) {
    elevated = (elevation.TokenIsElevated != 0);
  }
  CloseHandle(token);
  return elevated;
}

void MainWindow::OpenOptionsDialog() {
  AppSettings dialog_settings = settings_;
  if (pending_theme_change_.has_value()) {
    dialog_settings.theme = pending_theme_change_.value();
  }

  const AppTheme active_theme = settings_.theme;
  OptionsDialog dlg(hwnd_, dialog_settings, active_theme, ui_font_);
  if (dlg.Show()) {
    settings_ = dlg.GetSettings();
    const AppTheme requested_theme = settings_.theme;
    const bool theme_changed = requested_theme != active_theme;
    if (theme_changed) {
      pending_theme_change_ = requested_theme;
      settings_.theme = active_theme;
    } else {
      pending_theme_change_.reset();
    }
    SaveSettingsPreservingPendingTheme();

    UpdateLanguageAndUI();
    UpdateTimerInterval();

    if (theme_changed &&
        MessageBoxW(hwnd_,
                    LanguageManager::GetString(StringId::kConfirmRestartTheme),
                    LanguageManager::GetString(StringId::kRestartNoticeTitle),
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
      RestartApplication();
    }
  }
}

void MainWindow::ShowAboutDialog() {
  AboutDialogContext context;
  context.theme = settings_.theme;
  context.ui_font = ui_font_;
  context.title = LanguageManager::GetString(StringId::kAboutTitle);
  context.app_name = LanguageManager::GetString(StringId::kAboutAppName);
  const wchar_t* privilege = LanguageManager::GetString(
      is_elevated_ ? StringId::kPrivilegeAdministrator
                   : StringId::kPrivilegeStandardUser);
  wchar_t details[1024] = {0};
  swprintf_s(details,
             LanguageManager::GetString(StringId::kAboutDetailsFormat),
             APP_VERSION_STR, privilege);
  context.details = details;

  DialogBoxParamW(GetModuleHandleW(nullptr),
                  MAKEINTRESOURCEW(IDD_ABOUT_DIALOG), hwnd_,
                  AboutDialogProc, reinterpret_cast<LPARAM>(&context));
}

void MainWindow::AddSelectedProcessToMonitor() {
  auto proc = GetSelectedProcess();
  if (!proc) return;

  if (MonitorDialog::ShowAddRuleForProcess(
          hwnd_, proc->name, proc->process_id, &settings_.monitor_rules,
          settings_.theme, ui_font_)) {
    monitor_service_.SetRules(settings_.monitor_rules);
    settings_.SaveMonitorRules();
  }
}

void MainWindow::ShowContextMenu(int x, int y) {
  if (x == -1 && y == -1) {
    RECT rc;
    GetWindowRect(listview_hwnd_, &rc);
    x = rc.left + 50;
    y = rc.top + 50;
  }

  auto proc = GetSelectedProcess();
  UINT proc_flags = proc ? MF_ENABLED : MF_GRAYED;
  EnableMenuItem(context_menu_, IDM_END_TASK, proc_flags);
  EnableMenuItem(context_menu_, IDM_END_TREE, proc_flags);
  EnableMenuItem(context_menu_, IDM_OPEN_FILE_LOCATION, proc_flags);
  EnableMenuItem(context_menu_, IDM_SEARCH_ONLINE, proc_flags);
  EnableMenuItem(context_menu_, IDM_PROPERTIES, proc_flags);
  EnableMenuItem(context_menu_, IDM_PROCESS_GO_TO_SERVICE, proc_flags);
  EnableMenuItem(context_menu_, IDM_ADD_TO_MONITOR, proc_flags);

  int visible_count = 0;
  for (const auto& col : settings_.columns) {
    if (col.visible) visible_count++;
  }
  UINT hide_flags = (context_header_col_index_ >= 0 && context_header_col_index_ < visible_count && visible_count > 1)
                        ? MF_ENABLED : MF_GRAYED;
  EnableMenuItem(context_menu_, IDM_HIDE_COLUMN, hide_flags);

  SetForegroundWindow(hwnd_);
  TrackPopupMenu(context_menu_, TPM_RIGHTBUTTON, x, y, 0, hwnd_, nullptr);
}

void MainWindow::ShowHeaderContextMenu(int x, int y, int col_index) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  int visible_count = 0;
  for (const auto& col : settings_.columns) {
    if (col.visible) visible_count++;
  }

  context_header_col_index_ = col_index;

  UINT hide_flags = MF_STRING;
  if (col_index < 0 || col_index >= visible_count || visible_count <= 1) {
    hide_flags |= MF_GRAYED;
  }

  AppendMenuW(menu, hide_flags, IDM_HIDE_COLUMN, LanguageManager::GetString(StringId::kMenuHideColumn));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_SELECT_COLUMNS, LanguageManager::GetString(StringId::kMenuSelectColumns));

  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
}

void MainWindow::HideColumnByIndex(int visible_col_index) {
  std::vector<size_t> visible_indices;
  for (size_t i = 0; i < settings_.columns.size(); ++i) {
    if (settings_.columns[i].visible) {
      visible_indices.push_back(i);
    }
  }

  if (visible_indices.size() <= 1) {
    return;  // Do not hide the last remaining column
  }

  if (visible_col_index < 0 || visible_col_index >= static_cast<int>(visible_indices.size())) {
    return;
  }

  size_t target_idx = visible_indices[visible_col_index];
  settings_.columns[target_idx].visible = false;
  SaveSettingsPreservingPendingTheme();

  if (sort_column_index_ == visible_col_index) {
    sort_column_index_ = -1;
  } else if (sort_column_index_ > visible_col_index) {
    sort_column_index_--;
  }

  RebuildListViewColumns();
  UpdateListView();
}

void MainWindow::OpenColumnSelectorDialog() {
  ColumnSelectorDialog dlg(hwnd_, settings_.columns, settings_.theme, ui_font_);
  if (dlg.Show()) {
    settings_.columns = dlg.GetColumns();
    SaveSettingsPreservingPendingTheme();
    RebuildListViewColumns();
    UpdateListView();
  }
}

LRESULT CALLBACK MainWindow::HeaderSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                                UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto* self = reinterpret_cast<MainWindow*>(dwRefData);
  if (!self) {
    return DefSubclassProc(hwnd, msg, wparam, lparam);
  }

  if (msg == WM_RBUTTONUP || msg == WM_CONTEXTMENU) {
    POINT screen_pt;
    if (msg == WM_CONTEXTMENU) {
      screen_pt.x = GET_X_LPARAM(lparam);
      screen_pt.y = GET_Y_LPARAM(lparam);
      if (screen_pt.x == -1 && screen_pt.y == -1) {
        GetCursorPos(&screen_pt);
      }
    } else {
      screen_pt.x = GET_X_LPARAM(lparam);
      screen_pt.y = GET_Y_LPARAM(lparam);
      ClientToScreen(hwnd, &screen_pt);
    }

    POINT client_pt = screen_pt;
    ScreenToClient(hwnd, &client_pt);

    HDHITTESTINFO hdhti = {0};
    hdhti.pt = client_pt;
    int hit_col = static_cast<int>(SendMessageW(hwnd, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hdhti)));
    if (hit_col < 0) {
      int count = Header_GetItemCount(hwnd);
      for (int i = 0; i < count; ++i) {
        RECT rc;
        if (Header_GetItemRect(hwnd, i, &rc)) {
          if (PtInRect(&rc, client_pt)) {
            hit_col = i;
            break;
          }
        }
      }
    }

    self->ShowHeaderContextMenu(screen_pt.x, screen_pt.y, hit_col);
    return 0;
  }

  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void MainWindow::UpdateTrayTooltip() {
  if (notify_icon_data_.cbSize == 0) return;

  double cpu_usage = totals_.total_cpu_usage;
  if (cpu_usage < 0.0) cpu_usage = 0.0;
  if (cpu_usage > 100.0) cpu_usage = 100.0;

  double mem_percent = 0.0;
  if (totals_.total_physical_memory > 0) {
    mem_percent = (static_cast<double>(totals_.used_physical_memory) * 100.0) /
                  static_cast<double>(totals_.total_physical_memory);
    if (mem_percent < 0.0) mem_percent = 0.0;
    if (mem_percent > 100.0) mem_percent = 100.0;
  }

  std::wstring mem_label =
      LanguageManager::GetString(StringId::kTrayMemoryLabel);
  wchar_t tip[128];
  swprintf_s(tip, L"CPU %.1f%%\n%s %.1f%%", cpu_usage, mem_label.c_str(), mem_percent);

  wcsncpy_s(notify_icon_data_.szTip, tip, _TRUNCATE);
  notify_icon_data_.uFlags = NIF_TIP | NIF_SHOWTIP;
  Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_);
}

void MainWindow::ShowTrayContextMenu() {
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(tray_menu_, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void MainWindow::MinimizeToTray() {
  ShowWindow(hwnd_, SW_HIDE);
  notify_icon_data_.uFlags |= NIF_INFO;
  wcscpy_s(notify_icon_data_.szInfoTitle, LanguageManager::GetString(StringId::kAppTitle));
  wcscpy_s(notify_icon_data_.szInfo, LanguageManager::GetString(StringId::kMsgTrayMinimized));
  notify_icon_data_.dwInfoFlags = NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_);
}

void MainWindow::RestoreFromTray() const {
  ShowWindow(hwnd_, SW_SHOW);
  ShowWindow(hwnd_, SW_RESTORE);
  SetForegroundWindow(hwnd_);
}

void MainWindow::UpdateLanguageAndUI() {
  LanguageManager::SetLanguage(settings_.language);

  // 1. Re-create Fonts & Apply to Controls
  if (list_font_) DeleteObject(list_font_);
  list_font_ = ThemeManager::CreateAppFont(settings_.list_font_name, settings_.list_font_size);

  if (ui_font_) DeleteObject(ui_font_);
  ui_font_ = ThemeManager::CreateAppFont(settings_.ui_font_name, settings_.ui_font_size);

  WPARAM ui_font_param = reinterpret_cast<WPARAM>(ui_font_ ? ui_font_ : GetStockObject(DEFAULT_GUI_FONT));
  WPARAM list_font_param = reinterpret_cast<WPARAM>(list_font_ ? list_font_ : GetStockObject(DEFAULT_GUI_FONT));

  // UI Font applied to toolbar, inputs, buttons, statusbar
  SendMessageW(search_edit_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(interval_combo_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(tab_control_, WM_SETFONT, ui_font_param, TRUE);

  InvalidateRect(search_edit_, nullptr, TRUE);

  // Re-populate interval combo box with localized strings and select current interval
  PopulateIntervalComboBox();

  SendMessageW(btn_tree_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_columns_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_topmost_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_options_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_monitor_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_refresh_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_endtask_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(btn_restart_admin_, WM_SETFONT, ui_font_param, TRUE);
  SendMessageW(statusbar_hwnd_, WM_SETFONT, ui_font_param, TRUE);

  // Condition filter controls localization and font
  if (filter_col_combo_) {
    SendMessageW(filter_col_combo_, WM_SETFONT, ui_font_param, TRUE);
    int cur_col_sel = static_cast<int>(SendMessageW(filter_col_combo_, CB_GETCURSEL, 0, 0));
    ProcessColumnId cur_col_id = (cur_col_sel >= 0) ? GetFilterColumnId(cur_col_sel) : settings_.display_filter_condition.column_id;
    SendMessageW(filter_col_combo_, CB_RESETCONTENT, 0, 0);
    for (int ci = 0; ci < kFilterColumnCount; ++ci) {
      std::wstring col_name = LanguageManager::GetColumnHeaderText(kFilterColumnIds[ci]);
      SendMessageW(filter_col_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(col_name.c_str()));
    }
    int new_col_sel = FindFilterColumnIndex(cur_col_id);
    SendMessageW(filter_col_combo_, CB_SETCURSEL, new_col_sel >= 0 ? new_col_sel : 0, 0);
    InvalidateRect(filter_col_combo_, nullptr, TRUE);
    UpdateWindow(filter_col_combo_);
  }
  if (filter_op_combo_) {
    SendMessageW(filter_op_combo_, WM_SETFONT, ui_font_param, TRUE);
    int cur_op_sel = static_cast<int>(SendMessageW(filter_op_combo_, CB_GETCURSEL, 0, 0));
    SendMessageW(filter_op_combo_, CB_RESETCONTENT, 0, 0);
    const wchar_t* op_labels[] = {L">", L">=", L"<", L"<=", L"==", L"!="};
    for (const auto* lbl : op_labels) {
      SendMessageW(filter_op_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lbl));
    }
    std::wstring contains_str = LanguageManager::GetString(StringId::kOpContains);
    SendMessageW(filter_op_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(contains_str.c_str()));
    if (cur_op_sel >= 0) {
      SendMessageW(filter_op_combo_, CB_SETCURSEL, cur_op_sel, 0);
    }
    InvalidateRect(filter_op_combo_, nullptr, TRUE);
    UpdateWindow(filter_op_combo_);
  }
  if (filter_val_edit_)  SendMessageW(filter_val_edit_, WM_SETFONT, ui_font_param, TRUE);
  if (filter_clear_btn_) SendMessageW(filter_clear_btn_, WM_SETFONT, ui_font_param, TRUE);

  // List Font (Mono preferred) applied to ListView, TreeView, and Column Header
  SendMessageW(listview_hwnd_, WM_SETFONT, list_font_param, TRUE);
  SendMessageW(treeview_hwnd_, WM_SETFONT, list_font_param, TRUE);
  SendMessageW(service_list_view_, WM_SETFONT, list_font_param, TRUE);

  HWND header_hwnd = ListView_GetHeader(listview_hwnd_);
  if (header_hwnd) {
    SendMessageW(header_hwnd, WM_SETFONT, list_font_param, TRUE);
  }
  HWND service_header_hwnd = ListView_GetHeader(service_list_view_);
  if (service_header_hwnd) {
    SendMessageW(service_header_hwnd, WM_SETFONT, list_font_param, TRUE);
  }

  // Ensure ListView and TreeView use the real process icon image list
  ListView_SetImageList(listview_hwnd_, icon_helper_.GetImageList(), LVSIL_SMALL);
  TreeView_SetImageList(treeview_hwnd_, icon_helper_.GetImageList(), TVSIL_NORMAL);

  std::wstring title = LanguageManager::GetString(StringId::kAppTitle);
  if (is_elevated_) {
    title += LanguageManager::GetString(StringId::kAdministratorSuffix);
  }
  SetWindowTextW(hwnd_, title.c_str());

  // Re-build menu
  HMENU old_menu = GetMenu(hwnd_);
  HMENU new_menu = CreateMenu();

  HMENU file_menu = CreatePopupMenu();
  if (!is_elevated_) {
    AppendMenuW(file_menu, MF_STRING, IDM_RESTART_AS_ADMIN, LanguageManager::GetString(StringId::kMenuRestartAsAdmin));
    AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
  }
  AppendMenuW(file_menu, MF_STRING, IDM_REFRESH_NOW, LanguageManager::GetString(StringId::kMenuRefreshNow));
  AppendMenuW(file_menu, MF_STRING, IDM_OPTIONS, LanguageManager::GetString(StringId::kMenuOptions));
  AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file_menu, MF_STRING, IDM_TRAY_EXIT, LanguageManager::GetString(StringId::kMenuExit));
  AppendMenuW(new_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), LanguageManager::GetString(StringId::kMenuFile));

  HMENU view_menu = CreatePopupMenu();
  AppendMenuW(view_menu, MF_STRING, IDM_SELECT_COLUMNS, LanguageManager::GetString(StringId::kMenuSelectColumns));
  AppendMenuW(view_menu, MF_STRING, IDM_MONITOR_SETTINGS, LanguageManager::GetString(StringId::kMenuMonitorRules));
  AppendMenuW(view_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view_menu, MF_STRING, IDM_ALWAYS_ON_TOP, LanguageManager::GetString(StringId::kMenuAlwaysOnTop));
  AppendMenuW(new_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view_menu), LanguageManager::GetString(StringId::kMenuView));

  HMENU help_menu = CreatePopupMenu();
  wchar_t version_label[128] = {0};
  swprintf_s(
      version_label,
      LanguageManager::GetString(StringId::kMenuAboutVersionFormat),
      APP_VERSION_STR);
  AppendMenuW(help_menu, MF_STRING, IDM_ABOUT, version_label);
  AppendMenuW(new_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help_menu), LanguageManager::GetString(StringId::kMenuHelp));

  SetMenu(hwnd_, new_menu);
  if (old_menu) DestroyMenu(old_menu);
  DrawMenuBar(hwnd_);

  // Update Context Menu
  if (context_menu_) DestroyMenu(context_menu_);
  context_menu_ = CreatePopupMenu();
  AppendMenuW(context_menu_, MF_STRING, IDM_END_TASK, LanguageManager::GetString(StringId::kMenuEndProcess));
  AppendMenuW(context_menu_, MF_STRING, IDM_END_TREE, LanguageManager::GetString(StringId::kMenuEndProcessTree));
  AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);

  HMENU priority_menu = CreatePopupMenu();
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_REALTIME, LanguageManager::GetString(StringId::kPriorityRealtime));
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_HIGH, LanguageManager::GetString(StringId::kPriorityHigh));
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_ABOVE_NORMAL, LanguageManager::GetString(StringId::kPriorityAboveNormal));
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_NORMAL, LanguageManager::GetString(StringId::kPriorityNormal));
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_BELOW_NORMAL, LanguageManager::GetString(StringId::kPriorityBelowNormal));
  AppendMenuW(priority_menu, MF_STRING, IDM_PRIORITY_IDLE, LanguageManager::GetString(StringId::kPriorityLow));
  AppendMenuW(context_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(priority_menu), LanguageManager::GetString(StringId::kMenuSetPriority));

  AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(context_menu_, MF_STRING, IDM_OPEN_FILE_LOCATION, LanguageManager::GetString(StringId::kMenuOpenLocation));
  AppendMenuW(context_menu_, MF_STRING, IDM_SEARCH_ONLINE, LanguageManager::GetString(StringId::kMenuOnlineSearch));
  AppendMenuW(context_menu_, MF_STRING, IDM_PROPERTIES, LanguageManager::GetString(StringId::kMenuProperties));
  AppendMenuW(context_menu_, MF_STRING, IDM_PROCESS_GO_TO_SERVICE, LanguageManager::GetString(StringId::kMenuProcessGoToService));
  AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);

  HMENU copy_menu = CreatePopupMenu();
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_NAME, LanguageManager::GetString(StringId::kMatchTargetProcessName));
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_PID, LanguageManager::GetString(StringId::kMatchTargetPid));
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_PATH, LanguageManager::GetString(StringId::kMenuCopyPath));
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_COMMAND_LINE, LanguageManager::GetString(StringId::kMenuCopyCommandLine));
  AppendMenuW(copy_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_JSON, LanguageManager::GetString(StringId::kMenuCopyJson));
  AppendMenuW(copy_menu, MF_STRING, IDM_COPY_TSV, LanguageManager::GetString(StringId::kMenuCopyTsv));
  AppendMenuW(copy_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(copy_menu, MF_STRING, IDM_EXPORT_JSON, LanguageManager::GetString(StringId::kMenuExportJson));
  AppendMenuW(copy_menu, MF_STRING, IDM_EXPORT_TSV, LanguageManager::GetString(StringId::kMenuExportTsv));
  AppendMenuW(context_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(copy_menu), LanguageManager::GetString(StringId::kMenuCopy));

  AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(context_menu_, MF_STRING, IDM_ADD_TO_MONITOR, LanguageManager::GetString(StringId::kRuleEditTitleAdd));
  AppendMenuW(context_menu_, MF_STRING, IDM_MONITOR_SETTINGS, LanguageManager::GetString(StringId::kMenuMonitorRules));
  AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(context_menu_, MF_STRING, IDM_HIDE_COLUMN, LanguageManager::GetString(StringId::kMenuHideColumn));
  AppendMenuW(context_menu_, MF_STRING, IDM_SELECT_COLUMNS, LanguageManager::GetString(StringId::kMenuSelectColumns));

  // Update Tray Menu
  if (tray_menu_) DestroyMenu(tray_menu_);
  tray_menu_ = CreatePopupMenu();
  AppendMenuW(tray_menu_, MF_STRING, IDM_TRAY_RESTORE, LanguageManager::GetString(StringId::kMenuOpen));
  AppendMenuW(tray_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(tray_menu_, MF_STRING, IDM_MONITOR_SETTINGS, LanguageManager::GetString(StringId::kMenuMonitorRules));
  AppendMenuW(tray_menu_, MF_STRING, IDM_ALWAYS_ON_TOP, LanguageManager::GetString(StringId::kMenuAlwaysOnTop));
  AppendMenuW(tray_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(tray_menu_, MF_STRING, IDM_TRAY_EXIT, LanguageManager::GetString(StringId::kMenuExit));

  bool is_tree = (settings_.display_mode == ViewDisplayMode::kProcessTree);
  SetControlTooltip(btn_tree_, is_tree ? LanguageManager::GetString(StringId::kTooltipViewList) : LanguageManager::GetString(StringId::kTooltipViewTree));
  SetControlTooltip(btn_refresh_, LanguageManager::GetString(StringId::kTooltipRefresh));
  SetControlTooltip(btn_options_, LanguageManager::GetString(StringId::kTooltipOptions));
  SetControlTooltip(btn_monitor_, LanguageManager::GetString(StringId::kTooltipMonitor));
  SetControlTooltip(btn_columns_, LanguageManager::GetString(StringId::kTooltipColumns));
  SetControlTooltip(btn_restart_admin_, is_elevated_
      ? LanguageManager::GetString(StringId::kTooltipRunningAsAdmin)
      : LanguageManager::GetString(StringId::kTooltipRestartAdmin));
  SetControlTooltip(btn_topmost_, LanguageManager::GetString(StringId::kTooltipAlwaysOnTop));
  SetControlTooltip(btn_endtask_, LanguageManager::GetString(StringId::kTooltipEndProcess));
  SetControlTooltip(search_edit_, LanguageManager::GetString(StringId::kTooltipQuickFilter));
  SetControlTooltip(interval_combo_, LanguageManager::GetString(StringId::kTooltipInterval));

  // Update Tab Control items
  if (tab_control_) {
    TCITEMW tie = {0};
    tie.mask = TCIF_TEXT;
    std::wstring tab_proc = LanguageManager::GetString(StringId::kTabProcesses);
    tie.pszText = const_cast<LPWSTR>(tab_proc.c_str());
    TabCtrl_SetItem(tab_control_, 0, &tie);

    std::wstring tab_svc = LanguageManager::GetString(StringId::kTabServices);
    tie.pszText = const_cast<LPWSTR>(tab_svc.c_str());
    TabCtrl_SetItem(tab_control_, 1, &tie);
  }

  // Re-build columns text
  for (auto& col : settings_.columns) {
    col.header_text = LanguageManager::GetColumnHeaderText(col.id);
  }
  RebuildListViewColumns();
  if (service_list_view_) {
    RebuildServiceListViewColumns();
  }
  RefreshData();
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::ResizeChildren(int width, int height) {
  RECT sb_rc = {0};
  GetWindowRect(statusbar_hwnd_, &sb_rc);
  int sb_height = sb_rc.bottom - sb_rc.top;
  if (sb_height <= 0) sb_height = 24;

  // Layout Left Group (Row 1)
  SetWindowPos(search_edit_, nullptr, 10, 8, 280, 24, SWP_NOZORDER);
  SetWindowPos(interval_combo_, nullptr, 298, 8, 80, 200, SWP_NOZORDER);
  SetWindowPos(btn_tree_, nullptr, 386, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_columns_, nullptr, 422, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_restart_admin_, nullptr, 458, 7, 32, 26, SWP_NOZORDER);

  // Layout Right Group (Anchored to right edge, never clipped)
  int right_endtask_x = width - 10 - 32;
  int right_refresh_x = right_endtask_x - 4 - 32;
  int right_monitor_x = right_refresh_x - 4 - 32;
  int right_options_x = right_monitor_x - 4 - 32;
  int right_topmost_x = right_options_x - 4 - 32;

  // Guard against overlap if window is narrower than min width
  if (right_topmost_x < 496) {
    right_topmost_x = 496;
    right_options_x = right_topmost_x + 32 + 4;
    right_monitor_x = right_options_x + 32 + 4;
    right_refresh_x = right_monitor_x + 32 + 4;
    right_endtask_x = right_refresh_x + 32 + 4;
  }

  SetWindowPos(btn_topmost_, nullptr, right_topmost_x, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_options_, nullptr, right_options_x, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_monitor_, nullptr, right_monitor_x, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_refresh_, nullptr, right_refresh_x, 7, 32, 26, SWP_NOZORDER);
  SetWindowPos(btn_endtask_, nullptr, right_endtask_x, 7, 32, 26, SWP_NOZORDER);

  // Layout Tab Control (y=36, height=26)
  if (tab_control_) {
    SetWindowPos(tab_control_, nullptr, 0, 36, width, 26, SWP_NOZORDER);
  }

  if (current_tab_ == MainTab::kProcesses) {
    // Show Condition Filter Row (Row 2, y=64)
    if (filter_col_combo_) {
      SetWindowPos(filter_col_combo_, nullptr, 10, 64, 220, 300, SWP_NOZORDER);
      ShowWindow(filter_col_combo_, SW_SHOW);
    }
    if (filter_op_combo_) {
      SetWindowPos(filter_op_combo_, nullptr, 238, 64, 90, 200, SWP_NOZORDER);
      ShowWindow(filter_op_combo_, SW_SHOW);
    }
    if (filter_val_edit_) {
      SetWindowPos(filter_val_edit_, nullptr, 336, 64, 200, 24, SWP_NOZORDER);
      ShowWindow(filter_val_edit_, SW_SHOW);
    }
    if (filter_clear_btn_) {
      SetWindowPos(filter_clear_btn_, nullptr, 544, 64, 28, 24, SWP_NOZORDER);
      ShowWindow(filter_clear_btn_, SW_SHOW);
    }

    int top_y = 92;
    int content_height = height - top_y - sb_height;
    if (content_height < 0) content_height = 0;

    SetWindowPos(listview_hwnd_, nullptr, 0, top_y, width, content_height, SWP_NOZORDER);
    SetWindowPos(treeview_hwnd_, nullptr, 0, top_y, width, content_height, SWP_NOZORDER);

    if (service_list_view_) ShowWindow(service_list_view_, SW_HIDE);
    if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
      ShowWindow(listview_hwnd_, SW_HIDE);
      ShowWindow(treeview_hwnd_, SW_SHOW);
    } else {
      ShowWindow(listview_hwnd_, SW_SHOW);
      ShowWindow(treeview_hwnd_, SW_HIDE);
    }
  } else {
    // Services Tab: Hide condition filter controls
    if (filter_col_combo_) ShowWindow(filter_col_combo_, SW_HIDE);
    if (filter_op_combo_) ShowWindow(filter_op_combo_, SW_HIDE);
    if (filter_val_edit_) ShowWindow(filter_val_edit_, SW_HIDE);
    if (filter_clear_btn_) ShowWindow(filter_clear_btn_, SW_HIDE);

    // Hide process list/tree
    ShowWindow(listview_hwnd_, SW_HIDE);
    ShowWindow(treeview_hwnd_, SW_HIDE);

    int top_y = 62;
    int content_height = height - top_y - sb_height;
    if (content_height < 0) content_height = 0;

    if (service_list_view_) {
      SetWindowPos(service_list_view_, nullptr, 0, top_y, width, content_height, SWP_NOZORDER);
      ShowWindow(service_list_view_, SW_SHOW);
    }
  }

  SendMessageW(statusbar_hwnd_, WM_SIZE, 0, 0);
}

void MainWindow::OnTabChanged() {
  KillTimer(hwnd_, IDT_SEARCH_DEBOUNCE_TIMER);

  int sel = TabCtrl_GetCurSel(tab_control_);
  current_tab_ = (sel == 1) ? MainTab::kServices : MainTab::kProcesses;
  InvalidateRect(tab_control_, nullptr, FALSE);

  RECT rc;
  GetClientRect(hwnd_, &rc);
  ResizeChildren(rc.right, rc.bottom);

  if (current_tab_ == MainTab::kProcesses) {
    ApplyFilterAndDisplay();
    UpdateStatusLabels();
  } else {
    if (all_services_.empty()) {
      RefreshServicesData();
    } else {
      ApplyServiceFilterAndDisplay();
      UpdateStatusLabels();
    }
  }
}

void MainWindow::RebuildServiceListViewColumns() {
  HWND header = ListView_GetHeader(service_list_view_);
  if (header) {
    int col_count = Header_GetItemCount(header);
    for (int i = col_count - 1; i >= 0; --i) {
      ListView_DeleteColumn(service_list_view_, i);
    }
  }

  struct ServiceColDef {
    StringId title_id;
    int width;
    int fmt;
  };

  ServiceColDef defs[] = {
    {StringId::kSvcColName, 180, LVCFMT_LEFT},
    {StringId::kSvcColPid, 70, LVCFMT_RIGHT},
    {StringId::kSvcColState, 90, LVCFMT_LEFT},
    {StringId::kSvcColStartType, 110, LVCFMT_LEFT},
    {StringId::kSvcColDisplayName, 240, LVCFMT_LEFT},
    {StringId::kSvcColAccount, 160, LVCFMT_LEFT},
    {StringId::kSvcColDescription, 350, LVCFMT_LEFT},
  };

  for (int i = 0; i < static_cast<int>(std::size(defs)); ++i) {
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
    lvc.cx = defs[i].width;
    std::wstring title = LanguageManager::GetString(defs[i].title_id);
    lvc.pszText = const_cast<LPWSTR>(title.c_str());
    lvc.fmt = defs[i].fmt;
    lvc.iSubItem = i;
    ListView_InsertColumn(service_list_view_, i, &lvc);
  }

  ThemeManager::ApplyListViewHeaderTheme(service_list_view_, settings_.theme, service_sort_column_, service_sort_ascending_);
}

void MainWindow::RefreshServicesData() {
  {
    std::lock_guard<std::mutex> lock(service_refresh_mutex_);
    if (service_refresh_stopping_) return;
    ++service_refresh_requested_generation_;
    service_refresh_requested_ = true;
  }
  service_refresh_cv_.notify_one();
}

void MainWindow::StartServiceRefreshWorker() {
  service_refresh_cancellation_.store(false, std::memory_order_relaxed);
  service_refresh_thread_ = std::thread(&MainWindow::ServiceRefreshWorker, this);
}

void MainWindow::StopServiceRefreshWorker() {
  {
    std::lock_guard<std::mutex> lock(service_refresh_mutex_);
    service_refresh_stopping_ = true;
    service_refresh_cancellation_.store(true, std::memory_order_relaxed);
  }
  service_refresh_cv_.notify_one();

  if (service_refresh_thread_.joinable()) {
    service_refresh_thread_.join();
  }

  if (hwnd_) {
    MSG message{};
    while (PeekMessageW(&message, hwnd_, WM_APP_SERVICE_SNAPSHOT_READY,
                        WM_APP_SERVICE_SNAPSHOT_READY, PM_REMOVE)) {
      delete reinterpret_cast<ServiceSnapshotMessage*>(message.lParam);
    }
  }
}

void MainWindow::ServiceRefreshWorker() {
  while (true) {
    uint64_t generation = 0;
    {
      std::unique_lock<std::mutex> lock(service_refresh_mutex_);
      service_refresh_cv_.wait(lock, [this] {
        return service_refresh_stopping_ || service_refresh_requested_;
      });
      if (service_refresh_stopping_) return;

      generation = service_refresh_requested_generation_;
      service_refresh_requested_ = false;
    }

    auto services = service_manager_service_.GetServicesSnapshot(
        &service_refresh_cancellation_);
    if (service_refresh_cancellation_.load(std::memory_order_relaxed)) {
      return;
    }

    auto message = std::make_unique<ServiceSnapshotMessage>();
    message->generation = generation;
    message->services = std::move(services);
    if (PostMessageW(hwnd_, WM_APP_SERVICE_SNAPSHOT_READY, 0,
                     reinterpret_cast<LPARAM>(message.get()))) {
      message.release();
    }
  }
}

void MainWindow::SortServices() {
  std::sort(filtered_services_.begin(), filtered_services_.end(),
            [this](const std::shared_ptr<ServiceItem>& a, const std::shared_ptr<ServiceItem>& b) {
              int cmp = 0;
              switch (service_sort_column_) {
                case 0:  // Name
                  cmp = _wcsicmp(a->service_name.c_str(), b->service_name.c_str());
                  break;
                case 1:  // PID
                  if (a->pid != b->pid) return service_sort_ascending_ ? (a->pid < b->pid) : (a->pid > b->pid);
                  break;
                case 2:  // State
                  cmp = _wcsicmp(a->GetStateString().c_str(), b->GetStateString().c_str());
                  break;
                case 3:  // Start Type
                  cmp = _wcsicmp(a->GetStartTypeString().c_str(), b->GetStartTypeString().c_str());
                  break;
                case 4:  // Display Name
                  cmp = _wcsicmp(a->display_name.c_str(), b->display_name.c_str());
                  break;
                case 5:  // Account
                  cmp = _wcsicmp(a->account_name.c_str(), b->account_name.c_str());
                  break;
                case 6:  // Description
                  cmp = _wcsicmp(a->description.c_str(), b->description.c_str());
                  break;
                default:
                  cmp = _wcsicmp(a->service_name.c_str(), b->service_name.c_str());
                  break;
              }
              if (cmp != 0) {
                return service_sort_ascending_ ? (cmp < 0) : (cmp > 0);
              }
              return _wcsicmp(a->service_name.c_str(), b->service_name.c_str()) < 0;
            });
}

void MainWindow::ApplyServiceFilterAndDisplay() {
  wchar_t query[256] = {0};
  GetWindowTextW(search_edit_, query, 256);
  std::wstring q(query);
  if (IsSearchPlaceholderOrEmpty(q)) {
    q.clear();
  }

  filtered_services_.clear();
  filtered_services_.reserve(all_services_.size());

  for (const auto& svc : all_services_) {
    if (q.empty() ||
        CaseInsensitiveContains(svc->service_name, q) ||
        CaseInsensitiveContains(svc->display_name, q) ||
        (svc->pid > 0 && CaseInsensitiveContains(std::to_wstring(svc->pid), q)) ||
        CaseInsensitiveContains(svc->description, q)) {
      filtered_services_.push_back(svc);
    }
  }

  SortServices();
  UpdateServiceListView();
}

void MainWindow::UpdateServiceListView() {
  SendMessageW(service_list_view_, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(service_list_view_);

  for (int i = 0; i < static_cast<int>(filtered_services_.size()); ++i) {
    const auto& svc = filtered_services_[i];
    LVITEMW lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = i;
    lvi.pszText = const_cast<LPWSTR>(svc->service_name.c_str());
    ListView_InsertItem(service_list_view_, &lvi);

    std::wstring pid_str = (svc->pid > 0) ? std::to_wstring(svc->pid) : L"";
    ListView_SetItemText(service_list_view_, i, 1, const_cast<LPWSTR>(pid_str.c_str()));

    std::wstring state_str = svc->GetStateString();
    ListView_SetItemText(service_list_view_, i, 2, const_cast<LPWSTR>(state_str.c_str()));

    std::wstring start_str = svc->GetStartTypeString();
    ListView_SetItemText(service_list_view_, i, 3, const_cast<LPWSTR>(start_str.c_str()));

    ListView_SetItemText(service_list_view_, i, 4, const_cast<LPWSTR>(svc->display_name.c_str()));
    ListView_SetItemText(service_list_view_, i, 5, const_cast<LPWSTR>(svc->account_name.c_str()));
    ListView_SetItemText(service_list_view_, i, 6, const_cast<LPWSTR>(svc->description.c_str()));
  }

  SendMessageW(service_list_view_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(service_list_view_, nullptr, TRUE);
}

std::shared_ptr<ServiceItem> MainWindow::GetSelectedService() {
  int sel = ListView_GetNextItem(service_list_view_, -1, LVNI_SELECTED);
  if (sel >= 0 && sel < static_cast<int>(filtered_services_.size())) {
    return filtered_services_[sel];
  }
  return nullptr;
}

void MainWindow::StartSelectedService() {
  auto svc = GetSelectedService();
  if (!svc) return;

  std::wstring err;
  if (!service_manager_service_.StartServiceByName(svc->service_name, &err)) {
    std::wstring msg =
        LanguageManager::GetString(StringId::kMsgServiceStartFailed) +
        std::wstring(L"\n") + err;
    if (!is_elevated_) {
      msg += L"\n\n";
      msg += LanguageManager::GetString(StringId::kMsgAdministratorRequired);
    }
    MessageBoxW(hwnd_, msg.c_str(), LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
  } else {
    RefreshServicesData();
  }
}

void MainWindow::StopSelectedService() {
  auto svc = GetSelectedService();
  if (!svc) return;

  std::wstring err;
  if (!service_manager_service_.StopServiceByName(svc->service_name, &err)) {
    std::wstring msg =
        LanguageManager::GetString(StringId::kMsgServiceStopFailed) +
        std::wstring(L"\n") + err;
    if (!is_elevated_) {
      msg += L"\n\n";
      msg += LanguageManager::GetString(StringId::kMsgAdministratorRequired);
    }
    MessageBoxW(hwnd_, msg.c_str(), LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
  } else {
    RefreshServicesData();
  }
}

void MainWindow::RestartSelectedService() {
  auto svc = GetSelectedService();
  if (!svc) return;

  std::wstring err;
  if (!service_manager_service_.RestartServiceByName(svc->service_name, &err)) {
    std::wstring msg =
        LanguageManager::GetString(StringId::kMsgServiceRestartFailed) +
        std::wstring(L"\n") + err;
    if (!is_elevated_) {
      msg += L"\n\n";
      msg += LanguageManager::GetString(StringId::kMsgAdministratorRequired);
    }
    MessageBoxW(hwnd_, msg.c_str(), LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
  } else {
    RefreshServicesData();
  }
}

void MainWindow::ChangeSelectedServiceStartupType(DWORD start_type) {
  auto svc = GetSelectedService();
  if (!svc) return;

  std::wstring err;
  if (!service_manager_service_.ChangeStartupType(svc->service_name, start_type, &err)) {
    std::wstring msg =
        LanguageManager::GetString(StringId::kMsgServiceStartupTypeFailed) +
        std::wstring(L"\n") + err;
    if (!is_elevated_) {
      msg += L"\n\n";
      msg += LanguageManager::GetString(StringId::kMsgAdministratorRequired);
    }
    MessageBoxW(hwnd_, msg.c_str(), LanguageManager::GetString(StringId::kTitleError), MB_OK | MB_ICONERROR);
  } else {
    RefreshServicesData();
  }
}

void MainWindow::ShowServiceContextMenu(int x, int y) {
  auto svc = GetSelectedService();
  if (!svc) return;

  if (service_context_menu_) {
    DestroyMenu(service_context_menu_);
  }

  service_context_menu_ = CreatePopupMenu();

  bool is_running = (svc->state == SERVICE_RUNNING);
  bool is_stopped = (svc->state == SERVICE_STOPPED);

  UINT start_flags = is_stopped ? MF_STRING : (MF_STRING | MF_GRAYED);
  UINT stop_flags = is_running ? MF_STRING : (MF_STRING | MF_GRAYED);
  UINT restart_flags = is_running ? MF_STRING : (MF_STRING | MF_GRAYED);

  AppendMenuW(service_context_menu_, start_flags, IDM_SERVICE_START, LanguageManager::GetString(StringId::kMenuServiceStart));
  AppendMenuW(service_context_menu_, stop_flags, IDM_SERVICE_STOP, LanguageManager::GetString(StringId::kMenuServiceStop));
  AppendMenuW(service_context_menu_, restart_flags, IDM_SERVICE_RESTART, LanguageManager::GetString(StringId::kMenuServiceRestart));
  AppendMenuW(service_context_menu_, MF_SEPARATOR, 0, nullptr);

  HMENU startup_sub = CreatePopupMenu();
  UINT auto_check = (svc->start_type == SERVICE_AUTO_START) ? (MF_STRING | MF_CHECKED) : MF_STRING;
  UINT manual_check = (svc->start_type == SERVICE_DEMAND_START) ? (MF_STRING | MF_CHECKED) : MF_STRING;
  UINT disabled_check = (svc->start_type == SERVICE_DISABLED) ? (MF_STRING | MF_CHECKED) : MF_STRING;
  AppendMenuW(startup_sub, auto_check, IDM_SERVICE_STARTUP_AUTO, LanguageManager::GetString(StringId::kServiceStartTypeAuto));
  AppendMenuW(startup_sub, manual_check, IDM_SERVICE_STARTUP_MANUAL, LanguageManager::GetString(StringId::kServiceStartTypeManual));
  AppendMenuW(startup_sub, disabled_check, IDM_SERVICE_STARTUP_DISABLED, LanguageManager::GetString(StringId::kServiceStartTypeDisabled));
  AppendMenuW(service_context_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(startup_sub), LanguageManager::GetString(StringId::kMenuServiceStartup));

  AppendMenuW(service_context_menu_, MF_SEPARATOR, 0, nullptr);
  UINT goto_flags = (svc->pid > 0) ? MF_STRING : (MF_STRING | MF_GRAYED);
  AppendMenuW(service_context_menu_, goto_flags, IDM_SERVICE_GO_TO_PROCESS, LanguageManager::GetString(StringId::kMenuServiceGoToProcess));

  TrackPopupMenuEx(service_context_menu_, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, hwnd_, nullptr);
}

void MainWindow::GoToSelectedServiceProcess() {
  auto svc = GetSelectedService();
  if (!svc || svc->pid == 0) return;

  current_tab_ = MainTab::kProcesses;
  TabCtrl_SetCurSel(tab_control_, 0);
  OnTabChanged();

  if (settings_.display_mode == ViewDisplayMode::kProcessTree) {
    std::vector<HTREEITEM> pending_items;
    for (HTREEITEM root = TreeView_GetRoot(treeview_hwnd_); root != nullptr;
         root = TreeView_GetNextSibling(treeview_hwnd_, root)) {
      pending_items.push_back(root);
    }
    while (!pending_items.empty()) {
      HTREEITEM item = pending_items.back();
      pending_items.pop_back();

      TVITEMW tree_item{};
      tree_item.mask = TVIF_PARAM;
      tree_item.hItem = item;
      if (TreeView_GetItem(treeview_hwnd_, &tree_item) && tree_item.lParam != 0 &&
          reinterpret_cast<ProcessItem*>(tree_item.lParam)->process_id == svc->pid) {
        TreeView_SelectItem(treeview_hwnd_, item);
        TreeView_EnsureVisible(treeview_hwnd_, item);
        SetFocus(treeview_hwnd_);
        return;
      }

      for (HTREEITEM child = TreeView_GetChild(treeview_hwnd_, item); child != nullptr;
           child = TreeView_GetNextSibling(treeview_hwnd_, child)) {
        pending_items.push_back(child);
      }
    }
    return;
  }

  for (size_t i = 0; i < filtered_processes_.size(); ++i) {
    if (filtered_processes_[i]->process_id == svc->pid) {
      ListView_SetItemState(listview_hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
      ListView_SetItemState(listview_hwnd_, static_cast<int>(i),
                            LVIS_SELECTED | LVIS_FOCUSED,
                            LVIS_SELECTED | LVIS_FOCUSED);
      ListView_EnsureVisible(listview_hwnd_, static_cast<int>(i), FALSE);
      SetFocus(listview_hwnd_);
      break;
    }
  }
}

void MainWindow::GoToProcessRelatedServices() {
  auto proc = GetSelectedProcess();
  if (!proc) return;

  current_tab_ = MainTab::kServices;
  TabCtrl_SetCurSel(tab_control_, 1);
  OnTabChanged();

  int count = ListView_GetItemCount(service_list_view_);
  int found_idx = -1;
  for (int i = 0; i < static_cast<int>(filtered_services_.size()) && i < count; ++i) {
    if (filtered_services_[i]->pid == proc->process_id) {
      if (found_idx == -1) found_idx = i;
      ListView_SetItemState(service_list_view_, i, LVIS_SELECTED, LVIS_SELECTED);
    } else {
      ListView_SetItemState(service_list_view_, i, 0, LVIS_SELECTED | LVIS_FOCUSED);
    }
  }

  if (found_idx != -1) {
    ListView_SetItemState(service_list_view_, found_idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(service_list_view_, found_idx, FALSE);
    SetFocus(service_list_view_);
  } else {
    MessageBoxW(hwnd_,
                LanguageManager::GetString(StringId::kMsgNoRelatedServices),
                LanguageManager::GetString(StringId::kTitleInfo), MB_OK | MB_ICONINFORMATION);
  }
}

namespace {
#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092

typedef struct tagUAHMENUHEADER {
  HMENU hmenu;
  HDC hdc;
  DWORD dwFlags;
} UAHMENUHEADER;

typedef struct tagUAHMENUITEM {
  int iPosition;
  UAHMENUHEADER menuHeader;
} UAHMENUITEM;

typedef struct tagUAHMENUITEMDRAW {
  UAHMENUHEADER menuHeader;
  UAHMENUITEM item;
  RECT rcItem;
  DWORD dwState;
} UAHMENUITEMDRAW;
#endif
}  // namespace

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_NCPAINT:
    case WM_NCACTIVATE: {
      LRESULT res = DefWindowProcW(hwnd, msg, wparam, lparam);
      if (settings_.theme == AppTheme::kDark) {
        MENUBARINFO mbi = {sizeof(MENUBARINFO)};
        if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
          HDC hdc = GetWindowDC(hwnd);
          RECT window_rc;
          GetWindowRect(hwnd, &window_rc);
          RECT menu_rc = mbi.rcBar;
          OffsetRect(&menu_rc, -window_rc.left, -window_rc.top);

          const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
          FillRect(hdc, &menu_rc, palette.window_brush);

          HMENU menu = GetMenu(hwnd);
          if (menu) {
            int count = GetMenuItemCount(menu);
            HFONT hfont = ui_font_ ? ui_font_
                                   : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT old_font = static_cast<HFONT>(SelectObject(hdc, hfont));
            SetTextColor(hdc, palette.text_primary);
            SetBkMode(hdc, TRANSPARENT);

            for (int i = 0; i < count; ++i) {
              RECT item_rc;
              if (GetMenuItemRect(hwnd, menu, i, &item_rc)) {
                OffsetRect(&item_rc, -window_rc.left, -window_rc.top);
                wchar_t item_text[256] = {0};
                GetMenuStringW(menu, i, item_text, 256, MF_BYPOSITION);

                // Add padding
                InflateRect(&item_rc, -2, 0);
                DrawTextW(hdc, item_text, -1, &item_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
              }
            }
            SelectObject(hdc, old_font);
          }
          ReleaseDC(hwnd, hdc);
        }
      }
      return res;
    }

    case WM_UAHDRAWMENU: {
      if (settings_.theme == AppTheme::kDark) {
        auto* uah_menu = reinterpret_cast<UAHMENUHEADER*>(lparam);
        MENUBARINFO mbi = {sizeof(MENUBARINFO)};
        if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
          RECT window_rc;
          GetWindowRect(hwnd, &window_rc);
          RECT menu_rc = mbi.rcBar;
          OffsetRect(&menu_rc, -window_rc.left, -window_rc.top);

          const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
          FillRect(uah_menu->hdc, &menu_rc, palette.window_brush);
          return TRUE;
        }
      }
      break;
    }

    case WM_UAHDRAWMENUITEM: {
      if (settings_.theme == AppTheme::kDark) {
        auto* uah_item = reinterpret_cast<UAHMENUITEMDRAW*>(lparam);
        wchar_t menu_text[256] = {0};
        GetMenuStringW(uah_item->menuHeader.hmenu, uah_item->item.iPosition, menu_text, 256, MF_BYPOSITION);

        if (menu_text[0] != L'\0') {
          const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);

          bool is_hovered = (uah_item->dwState & 0x0001) || (uah_item->dwState & 0x0040) || (uah_item->dwState & 0x0100);
          HBRUSH bg_brush = is_hovered ? palette.control_brush : palette.window_brush;
          FillRect(uah_item->menuHeader.hdc, &uah_item->rcItem, bg_brush);

          HFONT hfont = ui_font_ ? ui_font_
                                 : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
          HFONT old_font = static_cast<HFONT>(SelectObject(uah_item->menuHeader.hdc, hfont));

          SetTextColor(uah_item->menuHeader.hdc, palette.text_primary);
          SetBkMode(uah_item->menuHeader.hdc, TRANSPARENT);
          DrawTextW(uah_item->menuHeader.hdc, menu_text, -1, &uah_item->rcItem,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

          SelectObject(uah_item->menuHeader.hdc, old_font);
          return TRUE;
        }
      }
      break;
    }

    case WM_SIZE: {
      int w = LOWORD(lparam);
      int h = HIWORD(lparam);

      if (wparam == SIZE_MINIMIZED && settings_.minimize_to_tray) {
        MinimizeToTray();
        return 0;
      }

      if (wparam != SIZE_MINIMIZED) {
        settings_.window_width = w;
        settings_.window_height = h;
        settings_.is_maximized = (wparam == SIZE_MAXIMIZED);
      }

      ResizeChildren(w, h);
      return 0;
    }

    case WM_APP_SERVICE_SNAPSHOT_READY: {
      std::unique_ptr<ServiceSnapshotMessage> snapshot(
          reinterpret_cast<ServiceSnapshotMessage*>(lparam));
      if (snapshot && snapshot->generation > service_refresh_applied_generation_) {
        service_refresh_applied_generation_ = snapshot->generation;
        all_services_ = std::move(snapshot->services);
        if (current_tab_ == MainTab::kServices) {
          ApplyServiceFilterAndDisplay();
          UpdateStatusLabels();
        }
      }
      return 0;
    }

    case WM_TIMER: {
      if (wparam == IDT_REFRESH_TIMER) {
        RefreshData();
      } else if (wparam == IDT_SEARCH_DEBOUNCE_TIMER) {
        KillTimer(hwnd_, IDT_SEARCH_DEBOUNCE_TIMER);
        if (current_tab_ == MainTab::kServices) {
          ApplyServiceFilterAndDisplay();
        } else {
          ApplyFilterAndDisplay();
        }
      } else if (wparam == IDT_FILTER_SAVE_TIMER) {
        FlushPendingFilterSettingsSave();
      }
      return 0;
    }

    case WM_COMMAND: {
      int id = LOWORD(wparam);
      int code = HIWORD(wparam);

      if (id == IDC_SEARCH_EDIT && code == EN_CHANGE) {
        KillTimer(hwnd_, IDT_SEARCH_DEBOUNCE_TIMER);
        SetTimer(hwnd_, IDT_SEARCH_DEBOUNCE_TIMER,
                 kSearchDebounceMilliseconds, nullptr);
        InvalidateRect(search_edit_, nullptr, TRUE);
        return 0;
      }

      if (id == IDC_INTERVAL_COMBO && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(interval_combo_, CB_GETCURSEL, 0, 0));
        if (sel >= 0) {
          LRESULT sec = SendMessageW(interval_combo_, CB_GETITEMDATA, sel, 0);
          if (sec != CB_ERR) {
            settings_.refresh_interval_seconds = static_cast<int>(sec);
            UpdateTimerInterval();
            SaveSettingsPreservingPendingTheme();
          }
        }
        return 0;
      }

      // Condition filter controls
      if ((id == IDC_FILTER_COL_COMBO && code == CBN_SELCHANGE) ||
          (id == IDC_FILTER_OP_COMBO  && code == CBN_SELCHANGE) ||
          (id == IDC_FILTER_VAL_EDIT  && code == EN_CHANGE)) {
        OnFilterConditionChanged();
        return 0;
      }

      if (id == IDC_FILTER_VAL_EDIT && code == EN_KILLFOCUS) {
        FlushPendingFilterSettingsSave();
        return 0;
      }

      if (id == IDC_FILTER_CLEAR_BTN) {
        SetWindowTextW(filter_val_edit_, L"");
        settings_.display_filter_enabled = false;
        ScheduleFilterSettingsSave();
        ApplyConditionFilter();
        ApplyFilterAndDisplay();
        InvalidateRect(filter_val_edit_, nullptr, TRUE);
        return 0;
      }

      switch (id) {
        case IDC_BTN_TREE:
        case IDM_TOGGLE_TREE: {
          bool is_tree = (settings_.display_mode == ViewDisplayMode::kProcessTree);
          settings_.display_mode = is_tree ? ViewDisplayMode::kFlatList : ViewDisplayMode::kProcessTree;
          SendMessageW(btn_tree_, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(is_tree ? hicon_tree_ : hicon_list_));
          SetControlTooltip(btn_tree_, is_tree ? LanguageManager::GetString(StringId::kTooltipViewTree)
                                                : LanguageManager::GetString(StringId::kTooltipViewList));
          ShowWindow(listview_hwnd_, is_tree ? SW_SHOW : SW_HIDE);
          ShowWindow(treeview_hwnd_, is_tree ? SW_HIDE : SW_SHOW);
          ApplyFilterAndDisplay();
          break;
        }

        case IDM_HIDE_COLUMN:
          HideColumnByIndex(context_header_col_index_);
          break;

        case IDC_BTN_COLUMNS:
        case IDM_SELECT_COLUMNS:
          OpenColumnSelectorDialog();
          break;

        case IDC_BTN_TOPMOST:
        case IDM_ALWAYS_ON_TOP: {
          settings_.always_on_top = !settings_.always_on_top;
          SetWindowPos(hwnd_, settings_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
          SendMessageW(btn_topmost_, BM_SETIMAGE, IMAGE_ICON,
                       reinterpret_cast<LPARAM>(settings_.always_on_top ? hicon_topmost_ : hicon_topmost_off_));
          SetControlTooltip(btn_topmost_, settings_.always_on_top ? LanguageManager::GetString(StringId::kTooltipTopmostOff)
                                                                 : LanguageManager::GetString(StringId::kTooltipTopmostOn));
          CheckMenuItem(GetMenu(hwnd_), IDM_ALWAYS_ON_TOP, settings_.always_on_top ? MF_CHECKED : MF_UNCHECKED);
          break;
        }

        case IDC_BTN_OPTIONS:
        case IDM_OPTIONS:
          OpenOptionsDialog();
          break;

        case IDM_ABOUT:
          ShowAboutDialog();
          break;

        case IDC_BTN_MONITOR:
        case IDM_MONITOR_SETTINGS:
          OpenMonitorSettings();
          break;

        case IDM_ADD_TO_MONITOR:
          AddSelectedProcessToMonitor();
          break;

        case IDC_BTN_REFRESH:
        case IDM_REFRESH_NOW:
          RefreshData();
          break;

        case IDC_BTN_RESTART_ADMIN:
        case IDM_RESTART_AS_ADMIN:
          RestartAsAdministrator();
          break;

        case IDC_BTN_ENDTASK:
        case IDM_END_TASK:
          EndSelectedProcess();
          break;

        case IDM_END_TREE:
          EndSelectedProcessTree();
          break;

        case IDM_PRIORITY_REALTIME:
          SetSelectedProcessPriority(ProcessPriorityClass::kRealtime);
          break;
        case IDM_PRIORITY_HIGH:
          SetSelectedProcessPriority(ProcessPriorityClass::kHigh);
          break;
        case IDM_PRIORITY_ABOVE_NORMAL:
          SetSelectedProcessPriority(ProcessPriorityClass::kAboveNormal);
          break;
        case IDM_PRIORITY_NORMAL:
          SetSelectedProcessPriority(ProcessPriorityClass::kNormal);
          break;
        case IDM_PRIORITY_BELOW_NORMAL:
          SetSelectedProcessPriority(ProcessPriorityClass::kBelowNormal);
          break;
        case IDM_PRIORITY_IDLE:
          SetSelectedProcessPriority(ProcessPriorityClass::kIdle);
          break;

        case IDM_OPEN_FILE_LOCATION:
          OpenSelectedFileLocation();
          break;

        case IDM_SEARCH_ONLINE:
          SearchSelectedProcessOnline();
          break;

        case IDM_PROPERTIES:
          ShowSelectedProcessProperties();
          break;

        case IDM_COPY_NAME:
          CopySelectedInfo(ProcessColumnId::kName);
          break;
        case IDM_COPY_PID:
          CopySelectedInfo(ProcessColumnId::kPid);
          break;
        case IDM_COPY_PATH:
          CopySelectedInfo(ProcessColumnId::kFilePath);
          break;
        case IDM_COPY_COMMAND_LINE:
          CopySelectedInfo(ProcessColumnId::kCommandLine);
          break;
        case IDM_COPY_JSON:
          CopySelectedAsJson();
          break;
        case IDM_COPY_TSV:
          CopySelectedAsTsv();
          break;
        case IDM_EXPORT_JSON:
          ExportSelectedAsJson();
          break;
        case IDM_EXPORT_TSV:
          ExportSelectedAsTsv();
          break;

        // Service Commands
        case IDM_SERVICE_START:
          StartSelectedService();
          break;
        case IDM_SERVICE_STOP:
          StopSelectedService();
          break;
        case IDM_SERVICE_RESTART:
          RestartSelectedService();
          break;
        case IDM_SERVICE_STARTUP_AUTO:
          ChangeSelectedServiceStartupType(SERVICE_AUTO_START);
          break;
        case IDM_SERVICE_STARTUP_MANUAL:
          ChangeSelectedServiceStartupType(SERVICE_DEMAND_START);
          break;
        case IDM_SERVICE_STARTUP_DISABLED:
          ChangeSelectedServiceStartupType(SERVICE_DISABLED);
          break;
        case IDM_SERVICE_GO_TO_PROCESS:
          GoToSelectedServiceProcess();
          break;
        case IDM_PROCESS_GO_TO_SERVICE:
          GoToProcessRelatedServices();
          break;

        case IDM_TRAY_RESTORE:
          RestoreFromTray();
          break;

        case IDM_TRAY_EXIT:
          RestoreFromTray();
          SendMessageW(hwnd_, WM_CLOSE, 0, 0);
          break;
      }
      return 0;
    }

    case WM_NOTIFY: {
      auto* nmhdr = reinterpret_cast<NMHDR*>(lparam);

      // Tab Control selection change
      if (nmhdr->hwndFrom == tab_control_ && nmhdr->code == TCN_SELCHANGE) {
        OnTabChanged();
        return 0;
      }

      if (nmhdr->hwndFrom == listview_hwnd_) {
        if (nmhdr->code == LVN_GETDISPINFOW) {
          auto* display_info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
          int item_index = display_info->item.iItem;
          int subitem_index = display_info->item.iSubItem;
          if (item_index < 0 ||
              item_index >= static_cast<int>(filtered_processes_.size())) {
            return 0;
          }

          const auto& process = filtered_processes_[item_index];
          if ((display_info->item.mask & LVIF_TEXT) != 0 &&
              display_info->item.pszText != nullptr &&
              display_info->item.cchTextMax > 0) {
            display_info->item.pszText[0] = L'\0';
            if (subitem_index >= 0 &&
                subitem_index < static_cast<int>(visible_process_columns_.size())) {
              std::wstring value = process->GetColumnValue(
                  visible_process_columns_[subitem_index]);
              wcsncpy_s(display_info->item.pszText,
                        display_info->item.cchTextMax, value.c_str(), _TRUNCATE);
            }
          }

          if ((display_info->item.mask & LVIF_IMAGE) != 0 &&
              subitem_index == 0) {
            display_info->item.iImage = icon_helper_.GetIconIndex(process->file_path);
          }
          return 0;
        } else if (nmhdr->code == NM_CUSTOMDRAW) {
          if (settings_.theme == AppTheme::kDark) {
            auto* lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lparam);
            switch (lplvcd->nmcd.dwDrawStage) {
              case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
              case CDDS_ITEMPREPAINT: {
                const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
                int item_idx = static_cast<int>(lplvcd->nmcd.dwItemSpec);
                bool is_selected = (ListView_GetItemState(listview_hwnd_, item_idx, LVIS_SELECTED) & LVIS_SELECTED) != 0;

                if (is_selected) {
                  lplvcd->clrText = palette.selected_item_text;
                  lplvcd->clrTextBk = palette.selected_item;
                } else {
                  lplvcd->clrText = palette.text_primary;
                  lplvcd->clrTextBk = palette.surface_background;
                }
                return CDRF_DODEFAULT;
              }
            }
          }
          return CDRF_DODEFAULT;
        } else if (nmhdr->code == LVN_COLUMNCLICK) {
          auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lparam);
          if (sort_column_index_ == nmlv->iSubItem) {
            sort_ascending_ = !sort_ascending_;
          } else {
            sort_column_index_ = nmlv->iSubItem;
            sort_ascending_ = true;
          }
          ThemeManager::ApplyListViewHeaderTheme(listview_hwnd_, settings_.theme, sort_column_index_, sort_ascending_);
          UpdateListView();
          return 0;
        } else if (nmhdr->code == NM_RCLICK) {
          POINT pt;
          GetCursorPos(&pt);
          POINT client_pt = pt;
          ScreenToClient(listview_hwnd_, &client_pt);
          LVHITTESTINFO lvhti = {0};
          lvhti.pt = client_pt;
          ListView_SubItemHitTest(listview_hwnd_, &lvhti);
          context_header_col_index_ = (lvhti.iSubItem >= 0) ? lvhti.iSubItem : -1;
          ShowContextMenu(pt.x, pt.y);
          return 0;
        } else if (nmhdr->code == NM_DBLCLK) {
          ShowSelectedProcessProperties();
          return 0;
        }
      } else if (nmhdr->hwndFrom == service_list_view_) {
        if (nmhdr->code == NM_CUSTOMDRAW) {
          if (settings_.theme == AppTheme::kDark) {
            auto* lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lparam);
            switch (lplvcd->nmcd.dwDrawStage) {
              case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
              case CDDS_ITEMPREPAINT: {
                const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
                int item_idx = static_cast<int>(lplvcd->nmcd.dwItemSpec);
                bool is_selected = (ListView_GetItemState(service_list_view_, item_idx, LVIS_SELECTED) & LVIS_SELECTED) != 0;

                if (is_selected) {
                  lplvcd->clrText = palette.selected_item_text;
                  lplvcd->clrTextBk = palette.selected_item;
                } else {
                  lplvcd->clrText = palette.text_primary;
                  lplvcd->clrTextBk = palette.surface_background;
                }
                return CDRF_DODEFAULT;
              }
            }
          }
          return CDRF_DODEFAULT;
        } else if (nmhdr->code == LVN_COLUMNCLICK) {
          auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lparam);
          if (service_sort_column_ == nmlv->iSubItem) {
            service_sort_ascending_ = !service_sort_ascending_;
          } else {
            service_sort_column_ = nmlv->iSubItem;
            service_sort_ascending_ = true;
          }
          ThemeManager::ApplyListViewHeaderTheme(service_list_view_, settings_.theme, service_sort_column_, service_sort_ascending_);
          SortServices();
          UpdateServiceListView();
          return 0;
        } else if (nmhdr->code == NM_RCLICK) {
          POINT pt;
          GetCursorPos(&pt);
          ShowServiceContextMenu(pt.x, pt.y);
          return 0;
        } else if (nmhdr->code == NM_DBLCLK) {
          GoToSelectedServiceProcess();
          return 0;
        }
      } else if (nmhdr->hwndFrom == treeview_hwnd_) {
        if (nmhdr->code == TVN_GETDISPINFOW) {
          auto* display_info = reinterpret_cast<NMTVDISPINFOW*>(lparam);
          TVITEMW tree_item{};
          tree_item.mask = TVIF_PARAM;
          tree_item.hItem = display_info->item.hItem;
          if (!TreeView_GetItem(treeview_hwnd_, &tree_item) ||
              tree_item.lParam == 0) {
            return 0;
          }

          auto* process = reinterpret_cast<ProcessItem*>(tree_item.lParam);
          int icon_index = icon_helper_.GetIconIndex(process->file_path);
          if ((display_info->item.mask & TVIF_IMAGE) != 0) {
            display_info->item.iImage = icon_index;
          }
          if ((display_info->item.mask & TVIF_SELECTEDIMAGE) != 0) {
            display_info->item.iSelectedImage = icon_index;
          }
          return 0;
        } else if (nmhdr->code == NM_CUSTOMDRAW) {
          if (settings_.theme == AppTheme::kDark) {
            auto* nmtvcd = reinterpret_cast<LPNMTVCUSTOMDRAW>(lparam);
            switch (nmtvcd->nmcd.dwDrawStage) {
              case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
              case CDDS_ITEMPREPAINT: {
                const auto& palette = ThemeManager::GetPalette(AppTheme::kDark);
                bool is_selected = (nmtvcd->nmcd.uItemState & CDIS_SELECTED) != 0;

                if (is_selected) {
                  nmtvcd->clrText = palette.selected_item_text;
                  nmtvcd->clrTextBk = palette.selected_item;
                } else {
                  nmtvcd->clrText = palette.text_primary;
                  nmtvcd->clrTextBk = palette.surface_background;
                }
                return CDRF_DODEFAULT;
              }
            }
          }
          return CDRF_DODEFAULT;
        } else if (nmhdr->code == NM_RCLICK) {
          POINT pt;
          GetCursorPos(&pt);
          ShowContextMenu(pt.x, pt.y);
          return 0;
        } else if (nmhdr->code == NM_DBLCLK) {
          ShowSelectedProcessProperties();
          return 0;
        }
      }
      break;
    }

    case WM_APP_TRAYMSG: {
      UINT event_msg = LOWORD(lparam);
      if (event_msg == WM_MOUSEMOVE || event_msg == NIN_POPUPOPEN) {
        UpdateTrayTooltip();
      } else if (event_msg == WM_LBUTTONUP || event_msg == WM_LBUTTONDBLCLK ||
          event_msg == NIN_SELECT || event_msg == NIN_KEYSELECT) {
        RestoreFromTray();
      } else if (event_msg == WM_RBUTTONUP || event_msg == WM_CONTEXTMENU) {
        ShowTrayContextMenu();
      }
      return 0;
    }

    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      HWND edit_hwnd = reinterpret_cast<HWND>(lparam);
      if (edit_hwnd == search_edit_ || edit_hwnd == filter_val_edit_) {
        wchar_t buf[256] = {0};
        GetWindowTextW(edit_hwnd, buf, static_cast<int>(std::size(buf)));
        bool has_active_text = false;
        if (edit_hwnd == search_edit_) {
          has_active_text = !IsSearchPlaceholderOrEmpty(buf);
        } else {
          std::wstring val(buf);
          size_t first = val.find_first_not_of(L" \t\r\n");
          has_active_text = (first != std::wstring::npos);
        }
        if (has_active_text) {
          SetTextColor(hdc, RGB(0, 0, 0));
          SetBkColor(hdc, RGB(253, 253, 223));  // #FDFDDF
          return reinterpret_cast<INT_PTR>(search_active_brush_);
        }
      }
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.control_background);
      return reinterpret_cast<INT_PTR>(palette.control_brush);
    }

    case WM_CTLCOLORLISTBOX: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.control_background);
      return reinterpret_cast<INT_PTR>(palette.control_brush);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wparam);
      HWND ctl_hwnd = reinterpret_cast<HWND>(lparam);
      if (ctl_hwnd == search_edit_ || ctl_hwnd == filter_val_edit_) {
        wchar_t buf[256] = {0};
        GetWindowTextW(ctl_hwnd, buf, static_cast<int>(std::size(buf)));
        bool has_active_text = false;
        if (ctl_hwnd == search_edit_) {
          has_active_text = !IsSearchPlaceholderOrEmpty(buf);
        } else {
          std::wstring val(buf);
          size_t first = val.find_first_not_of(L" \t\r\n");
          has_active_text = (first != std::wstring::npos);
        }
        if (has_active_text) {
          SetTextColor(hdc, RGB(0, 0, 0));
          SetBkColor(hdc, RGB(253, 253, 223));  // #FDFDDF
          return reinterpret_cast<INT_PTR>(search_active_brush_);
        }
        const auto& palette = ThemeManager::GetPalette(settings_.theme);
        SetTextColor(hdc, palette.text_secondary);
        SetBkColor(hdc, palette.control_background);
        return reinterpret_cast<INT_PTR>(palette.control_brush);
      }
      const auto& palette = ThemeManager::GetPalette(settings_.theme);
      SetTextColor(hdc, palette.text_primary);
      SetBkColor(hdc, palette.window_background);
      return reinterpret_cast<INT_PTR>(palette.window_brush);
    }

    case WM_CLOSE: {
      std::wstring title = LanguageManager::GetString(StringId::kAppTitle);
      std::wstring msg_text = LanguageManager::GetString(StringId::kMsgConfirmExit);
      if (MessageBoxW(hwnd_, msg_text.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return 0;
      }
      if (pending_theme_change_.has_value()) {
        settings_.theme = pending_theme_change_.value();
        pending_theme_change_.reset();
      }
      KillTimer(hwnd_, IDT_FILTER_SAVE_TIMER);
      filter_settings_save_pending_ = false;
      settings_.SaveSettings();
      DestroyWindow(hwnd_);
      return 0;
    }

    case WM_DESTROY: {
      FlushPendingFilterSettingsSave();
      StopServiceRefreshWorker();
      HWND header = ListView_GetHeader(listview_hwnd_);
      if (header) {
        RemoveWindowSubclass(header, HeaderSubclassProc, 1);
      }
      KillTimer(hwnd_, IDT_REFRESH_TIMER);
      KillTimer(hwnd_, IDT_SEARCH_DEBOUNCE_TIMER);
      KillTimer(hwnd_, IDT_FILTER_SAVE_TIMER);
      PostQuitMessage(0);
      return 0;
    }
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace lite_proc_manager
