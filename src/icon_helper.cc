// Copyright 2026 LiteProcManager Authors. All rights reserved.

#include "icon_helper.h"
#include "resource.h"

#include <shellapi.h>
#include <iterator>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace lite_proc_manager {

IconHelper::IconHelper(size_t cache_capacity)
    : cache_capacity_(cache_capacity) {}

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

std::optional<IconHelper::FileIdentity> IconHelper::GetFileIdentity(
    const std::wstring& file_path) {
  HANDLE file = CreateFileW(
      file_path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  BY_HANDLE_FILE_INFORMATION file_info{};
  bool succeeded = GetFileInformationByHandle(file, &file_info) != FALSE;
  CloseHandle(file);
  if (!succeeded) {
    return std::nullopt;
  }

  return FileIdentity{
      file_info.dwVolumeSerialNumber,
      file_info.nFileIndexHigh,
      file_info.nFileIndexLow,
      file_info.ftCreationTime,
  };
}

bool IconHelper::IsSameFileIdentity(const FileIdentity& lhs,
                                    const FileIdentity& rhs) {
  return lhs.volume_serial_number == rhs.volume_serial_number &&
         lhs.file_index_high == rhs.file_index_high &&
         lhs.file_index_low == rhs.file_index_low &&
         CompareFileTime(&lhs.creation_time, &rhs.creation_time) == 0;
}

int IconHelper::GetIconIndex(const std::wstring& file_path) {
  if (file_path.empty() || image_list_ == nullptr) {
    return default_icon_index_;
  }

  auto it = icon_cache_.find(file_path);
  if (it != icon_cache_.end()) {
    lru_paths_.splice(lru_paths_.begin(), lru_paths_, it->second.lru_position);
    return it->second.image_index;
  }

  auto file_identity = GetFileIdentity(file_path);
  if (file_identity.has_value()) {
    for (auto& cached : icon_cache_) {
      if (cached.second.file_identity.has_value() &&
          IsSameFileIdentity(file_identity.value(),
                             cached.second.file_identity.value())) {
        lru_paths_.splice(lru_paths_.begin(), lru_paths_,
                          cached.second.lru_position);
        return cached.second.image_index;
      }
    }
  }

  SHFILEINFOW sfi = {0};
  DWORD_PTR res = SHGetFileInfoW(
      file_path.c_str(), 0, &sfi, sizeof(sfi),
      SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);

  if (res != 0 && sfi.hIcon != nullptr) {
    int image_index = -1;
    if (cache_capacity_ == 0) {
      DestroyIcon(sfi.hIcon);
      return default_icon_index_;
    }

    if (icon_cache_.size() < cache_capacity_) {
      image_index = ImageList_AddIcon(image_list_, sfi.hIcon);
    } else {
      auto least_recent_path = std::prev(lru_paths_.end());
      auto least_recent = icon_cache_.find(*least_recent_path);
      if (least_recent != icon_cache_.end()) {
        image_index = least_recent->second.image_index;
        if (ImageList_ReplaceIcon(image_list_, image_index, sfi.hIcon) == -1) {
          image_index = -1;
        } else {
          icon_cache_.erase(least_recent);
          lru_paths_.erase(least_recent_path);
        }
      }
    }
    DestroyIcon(sfi.hIcon);

    if (image_index >= 0) {
      lru_paths_.push_front(file_path);
      icon_cache_.emplace(
          file_path,
          IconCacheEntry{image_index, lru_paths_.begin(), file_identity});
      return image_index;
    }
  }

  return default_icon_index_;
}

}  // namespace lite_proc_manager
