// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_TSV_HELPER_H_
#define LITE_PROC_MANAGER_TSV_HELPER_H_

#include <cwctype>
#include <string>

namespace lite_proc_manager {

inline std::wstring SanitizeTsvCell(std::wstring value) {
  for (wchar_t& ch : value) {
    if (ch == L'\t' || ch == L'\r' || ch == L'\n') {
      ch = L' ';
    }
  }

  size_t content_start = 0;
  while (content_start < value.size() &&
         std::iswspace(static_cast<wint_t>(value[content_start])) != 0) {
    ++content_start;
  }

  if (content_start < value.size()) {
    wchar_t first = value[content_start];
    if (first == L'=' || first == L'+' || first == L'-' || first == L'@') {
      value.insert(value.begin(), L'\'');
    }
  }

  return value;
}

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_TSV_HELPER_H_
