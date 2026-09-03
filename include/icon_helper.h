// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_ICON_HELPER_H_
#define LITE_PROC_MANAGER_ICON_HELPER_H_

#include <windows.h>
#include <commctrl.h>

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
  IconHelper();
  ~IconHelper();

  void Initialize(int icon_size = 16);
  HIMAGELIST GetImageList() const { return image_list_; }

  int GetIconIndex(const std::wstring& file_path);
  HICON GetDefaultIcon() const { return default_icon_; }

  static HICON CreateToolbarIcon(ToolbarIconType type, int size = 16);

 private:
  HICON CreateDefaultProcessIcon(int size);

  HIMAGELIST image_list_{nullptr};
  HICON default_icon_{nullptr};
  int default_icon_index_{0};
  std::unordered_map<std::wstring, int> icon_cache_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_ICON_HELPER_H_
