// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_URL_HELPER_H_
#define LITE_PROC_MANAGER_URL_HELPER_H_

#include <windows.h>

#include <limits>
#include <optional>
#include <string>

namespace lite_proc_manager {

// Percent-encodes a URL component according to RFC 3986 after converting the
// input from UTF-16 to UTF-8. Only unreserved ASCII characters remain literal.
inline std::optional<std::wstring> PercentEncodeUrlComponent(
    const std::wstring& value) {
  if (value.empty()) return std::wstring();
  if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  int utf8_length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0) return std::nullopt;

  std::string utf8(static_cast<size_t>(utf8_length), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), utf8.data(), utf8_length, nullptr,
          nullptr) != utf8_length) {
    return std::nullopt;
  }

  constexpr wchar_t kHexDigits[] = L"0123456789ABCDEF";
  std::wstring encoded;
  encoded.reserve(utf8.size() * 3);
  for (unsigned char byte : utf8) {
    bool unreserved =
        (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
        byte == '_' || byte == '~';
    if (unreserved) {
      encoded.push_back(static_cast<wchar_t>(byte));
    } else {
      encoded.push_back(L'%');
      encoded.push_back(kHexDigits[(byte >> 4) & 0x0F]);
      encoded.push_back(kHexDigits[byte & 0x0F]);
    }
  }
  return encoded;
}

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_URL_HELPER_H_
