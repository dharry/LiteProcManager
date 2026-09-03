// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "icon_helper.h"
#include "resource.h"

#include <shellapi.h>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace lite_proc_manager {

IconHelper::IconHelper() = default;

IconHelper::~IconHelper() {
  if (image_list_) {
    ImageList_Destroy(image_list_);
    image_list_ = nullptr;
  }
  if (default_icon_) {
    DestroyIcon(default_icon_);
    default_icon_ = nullptr;
  }
}

void IconHelper::Initialize(int icon_size) {
  if (image_list_) return;

  image_list_ = ImageList_Create(icon_size, icon_size, ILC_COLOR32 | ILC_MASK, 100, 100);
  default_icon_ = CreateDefaultProcessIcon(icon_size);
  if (default_icon_) {
    default_icon_index_ = ImageList_AddIcon(image_list_, default_icon_);
  }
}

HICON IconHelper::CreateDefaultProcessIcon(int size) {
  HDC hdc_screen = GetDC(nullptr);
  HDC hdc_mem = CreateCompatibleDC(hdc_screen);
  HBITMAP hbm_color = CreateCompatibleBitmap(hdc_screen, size, size);
  HBITMAP hbm_mask = CreateBitmap(size, size, 1, 1, nullptr);

  HBITMAP hbm_old = static_cast<HBITMAP>(SelectObject(hdc_mem, hbm_color));

  // Background rect
  RECT rc = {0, 0, size, size};
  HBRUSH hbr_bg = CreateSolidBrush(RGB(0, 120, 215));
  FillRect(hdc_mem, &rc, hbr_bg);
  DeleteObject(hbr_bg);

  // Inner border
  HPEN hpen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
  HPEN hpen_old = static_cast<HPEN>(SelectObject(hdc_mem, hpen));
  HBRUSH hbr_null = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  HBRUSH hbr_old = static_cast<HBRUSH>(SelectObject(hdc_mem, hbr_null));

  Rectangle(hdc_mem, 2, 2, size - 2, size - 2);

  SelectObject(hdc_mem, hbr_old);
  SelectObject(hdc_mem, hpen_old);
  DeleteObject(hpen);

  SelectObject(hdc_mem, hbm_old);
  DeleteDC(hdc_mem);
  ReleaseDC(nullptr, hdc_screen);

  ICONINFO ii = {0};
  ii.fIcon = TRUE;
  ii.hbmColor = hbm_color;
  ii.hbmMask = hbm_mask;

  HICON hicon = CreateIconIndirect(&ii);

  DeleteObject(hbm_color);
  DeleteObject(hbm_mask);

  return hicon;
}

HICON IconHelper::CreateToolbarIcon(ToolbarIconType type, int size) {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  int res_id = 0;
  switch (type) {
    case ToolbarIconType::kTree:       res_id = IDI_TB_TREE; break;
    case ToolbarIconType::kList:       res_id = IDI_TB_LIST; break;
    case ToolbarIconType::kColumns:    res_id = IDI_TB_COLUMNS; break;
    case ToolbarIconType::kShield:     res_id = IDI_TB_SHIELD; break;
    case ToolbarIconType::kTopmost:    res_id = IDI_TB_TOPMOST; break;
    case ToolbarIconType::kTopmostOff: res_id = IDI_TB_TOPMOST_OFF; break;
    case ToolbarIconType::kSettings:   res_id = IDI_TB_OPTIONS; break;
    case ToolbarIconType::kMonitor:    res_id = IDI_TB_MONITOR; break;
    case ToolbarIconType::kRefresh:    res_id = IDI_TB_REFRESH; break;
    case ToolbarIconType::kEndTask:    res_id = IDI_TB_ENDTASK; break;
    default: return nullptr;
  }
  return static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(res_id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

int IconHelper::GetIconIndex(const std::wstring& file_path) {
  if (file_path.empty()) {
    return default_icon_index_;
  }

  auto it = icon_cache_.find(file_path);
  if (it != icon_cache_.end()) {
    return it->second;
  }

  SHFILEINFOW sfi = {0};
  DWORD_PTR res = SHGetFileInfoW(
      file_path.c_str(), 0, &sfi, sizeof(sfi),
      SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);

  if (res != 0 && sfi.hIcon != nullptr) {
    int idx = ImageList_AddIcon(image_list_, sfi.hIcon);
    DestroyIcon(sfi.hIcon);
    icon_cache_[file_path] = idx;
    return idx;
  }

  icon_cache_[file_path] = default_icon_index_;
  return default_icon_index_;
}

}  // namespace lite_proc_manager
