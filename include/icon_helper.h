// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_ICON_HELPER_H_
#define LITE_PROC_MANAGER_ICON_HELPER_H_

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace lite_proc_manager {

enum class ToolbarIconType {
  kTree,       // Emerald Tree
  kList,       // Blue Clipboard
  kColumns,    // Colorful Chart Bars (Blue, Green, Amber)
  kTopmost,    // Crimson Pushpin (Active)
  kTopmostOff, // Gray Pushpin (Inactive)
  kMoon,       // Golden Crescent Moon
  kSun,        // Radiant Sun
  kRefresh,    // Sky Blue Sync Circle
  kEndTask,    // Vivid Red Cross
  kMonitor,    // Vibrant Shield / Monitor
  kSettings,   // Purple / Slate Gear
  kShield,     // UAC / Administrator Shield
};

class IconHelper {
 public:
  explicit IconHelper(size_t cache_capacity = 512);
  ~IconHelper();

  void Initialize(int icon_size = 16);
  HIMAGELIST GetImageList() const { return image_list_; }

  int GetIconIndex(const std::wstring& file_path);
  HICON GetDefaultIcon() const { return default_icon_; }
  size_t GetCachedIconCount() const { return icon_cache_.size(); }

  static HICON CreateToolbarIcon(ToolbarIconType type, int size = 16);

 private:
  struct FileIdentity {
    DWORD volume_serial_number{0};
    DWORD file_index_high{0};
    DWORD file_index_low{0};
    FILETIME creation_time{};
  };

  struct IconCacheEntry {
    int image_index{0};
    std::list<std::wstring>::iterator lru_position;
    std::optional<FileIdentity> file_identity;
  };

  HICON CreateDefaultProcessIcon(int size);
  static std::optional<FileIdentity> GetFileIdentity(const std::wstring& file_path);
  static bool IsSameFileIdentity(const FileIdentity& lhs, const FileIdentity& rhs);

  HIMAGELIST image_list_{nullptr};
  HICON default_icon_{nullptr};
  int default_icon_index_{0};
  size_t cache_capacity_{512};
  std::list<std::wstring> lru_paths_;
  std::unordered_map<std::wstring, IconCacheEntry> icon_cache_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_ICON_HELPER_H_
