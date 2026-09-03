// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_JSON_HELPER_H_
#define LITE_PROC_MANAGER_JSON_HELPER_H_

#include <windows.h>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lite_proc_manager {

class JsonValue;
using JsonObject = std::vector<std::pair<std::wstring, JsonValue>>;
using JsonArray = std::vector<JsonValue>;

enum class JsonType {
  kNull,
  kBoolean,
  kNumber,
  kString,
  kArray,
  kObject,
};

class JsonValue {
 public:
  JsonValue() : type_(JsonType::kNull) {}
  JsonValue(bool b) : type_(JsonType::kBoolean), bool_val_(b) {}
  JsonValue(int i) : type_(JsonType::kNumber), num_val_(static_cast<double>(i)) {}
  JsonValue(uint32_t u) : type_(JsonType::kNumber), num_val_(static_cast<double>(u)) {}
  JsonValue(int64_t i) : type_(JsonType::kNumber), num_val_(static_cast<double>(i)) {}
  JsonValue(uint64_t u) : type_(JsonType::kNumber), num_val_(static_cast<double>(u)) {}
  JsonValue(double d) : type_(JsonType::kNumber), num_val_(d) {}
  JsonValue(const wchar_t* s) : type_(JsonType::kString), str_val_(s ? s : L"") {}
  JsonValue(std::wstring s) : type_(JsonType::kString), str_val_(std::move(s)) {}
  JsonValue(JsonArray arr) : type_(JsonType::kArray), arr_val_(std::move(arr)) {}
  JsonValue(JsonObject obj) : type_(JsonType::kObject), obj_val_(std::move(obj)) {}

  JsonType type() const { return type_; }
  bool is_null() const { return type_ == JsonType::kNull; }
  bool is_bool() const { return type_ == JsonType::kBoolean; }
  bool is_number() const { return type_ == JsonType::kNumber; }
  bool is_string() const { return type_ == JsonType::kString; }
  bool is_array() const { return type_ == JsonType::kArray; }
  bool is_object() const { return type_ == JsonType::kObject; }

  bool as_bool(bool def = false) const { return is_bool() ? bool_val_ : def; }
  int as_int(int def = 0) const { return is_number() ? static_cast<int>(num_val_) : def; }
  uint32_t as_uint(uint32_t def = 0) const { return is_number() ? static_cast<uint32_t>(num_val_) : def; }
  int64_t as_int64(int64_t def = 0) const { return is_number() ? static_cast<int64_t>(num_val_) : def; }
  double as_double(double def = 0.0) const { return is_number() ? num_val_ : def; }
  std::wstring as_string(const std::wstring& def = L"") const { return is_string() ? str_val_ : def; }

  const JsonArray& as_array() const { return arr_val_; }
  const JsonObject& as_object() const { return obj_val_; }

  bool has_key(const std::wstring& key) const {
    if (!is_object()) return false;
    for (const auto& pair : obj_val_) {
      if (pair.first == key) return true;
    }
    return false;
  }

  const JsonValue& operator[](const std::wstring& key) const {
    static const JsonValue null_val;
    if (!is_object()) return null_val;
    for (const auto& pair : obj_val_) {
      if (pair.first == key) return pair.second;
    }
    return null_val;
  }

  std::wstring Serialize(int indent = 0) const {
    std::wostringstream oss;
    Write(oss, indent, 0);
    return oss.str();
  }

  static JsonValue Parse(const std::wstring& json_str) {
    size_t pos = 0;
    SkipWhitespace(json_str, pos);
    return ParseValue(json_str, pos);
  }

 private:
  void Write(std::wostringstream& oss, int indent_step, int current_indent) const {
    std::wstring ind(current_indent, L' ');
    std::wstring next_ind(current_indent + indent_step, L' ');

    switch (type_) {
      case JsonType::kNull:
        oss << L"null";
        break;
      case JsonType::kBoolean:
        oss << (bool_val_ ? L"true" : L"false");
        break;
      case JsonType::kNumber: {
        if (std::floor(num_val_) == num_val_ && std::abs(num_val_) < 1e14) {
          oss << static_cast<int64_t>(num_val_);
        } else {
          wchar_t buf[64];
          swprintf_s(buf, L"%.4f", num_val_);
          // Remove trailing zeros
          wchar_t* end = buf + wcslen(buf) - 1;
          while (end > buf && *end == L'0') { *end = L'\0'; --end; }
          if (end > buf && *end == L'.') { *end = L'\0'; }
          oss << buf;
        }
        break;
      }
      case JsonType::kString:
        oss << L"\"" << EscapeString(str_val_) << L"\"";
        break;
      case JsonType::kArray: {
        if (arr_val_.empty()) {
          oss << L"[]";
        } else {
          oss << L"[\n";
          for (size_t i = 0; i < arr_val_.size(); ++i) {
            oss << next_ind;
            arr_val_[i].Write(oss, indent_step, current_indent + indent_step);
            if (i + 1 < arr_val_.size()) oss << L",";
            oss << L"\n";
          }
          oss << ind << L"]";
        }
        break;
      }
      case JsonType::kObject: {
        if (obj_val_.empty()) {
          oss << L"{}";
        } else {
          oss << L"{\n";
          for (size_t i = 0; i < obj_val_.size(); ++i) {
            oss << next_ind << L"\"" << EscapeString(obj_val_[i].first) << L"\": ";
            obj_val_[i].second.Write(oss, indent_step, current_indent + indent_step);
            if (i + 1 < obj_val_.size()) oss << L",";
            oss << L"\n";
          }
          oss << ind << L"}";
        }
        break;
      }
    }
  }

  static std::wstring EscapeString(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
      if (c == L'"') oss << L"\\\"";
      else if (c == L'\\') oss << L"\\\\";
      else if (c == L'\b') oss << L"\\b";
      else if (c == L'\f') oss << L"\\f";
      else if (c == L'\n') oss << L"\\n";
      else if (c == L'\r') oss << L"\\r";
      else if (c == L'\t') oss << L"\\t";
      else oss << c;
    }
    return oss.str();
  }

  static void SkipWhitespace(const std::wstring& s, size_t& pos) {
    while (pos < s.length() && (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\n' || s[pos] == L'\r')) {
      ++pos;
    }
  }

  static JsonValue ParseValue(const std::wstring& s, size_t& pos) {
    SkipWhitespace(s, pos);
    if (pos >= s.length()) return JsonValue();

    wchar_t c = s[pos];
    if (c == L'{') return ParseObject(s, pos);
    if (c == L'[') return ParseArray(s, pos);
    if (c == L'"') return ParseString(s, pos);
    if (c == L't' || c == L'f') return ParseBool(s, pos);
    if (c == L'n') return ParseNull(s, pos);
    if (c == L'-' || (c >= L'0' && c <= L'9')) return ParseNumber(s, pos);

    return JsonValue();
  }

  static JsonValue ParseObject(const std::wstring& s, size_t& pos) {
    JsonObject obj;
    ++pos; // skip '{'
    SkipWhitespace(s, pos);

    if (pos < s.length() && s[pos] == L'}') {
      ++pos;
      return JsonValue(obj);
    }

    while (pos < s.length()) {
      SkipWhitespace(s, pos);
      if (pos >= s.length() || s[pos] != L'"') break;
      std::wstring key = ParseStringVal(s, pos);
      SkipWhitespace(s, pos);
      if (pos >= s.length() || s[pos] != L':') break;
      ++pos; // skip ':'
      JsonValue val = ParseValue(s, pos);
      obj.push_back({key, std::move(val)});

      SkipWhitespace(s, pos);
      if (pos < s.length() && s[pos] == L',') {
        ++pos;
      } else if (pos < s.length() && s[pos] == L'}') {
        ++pos;
        break;
      } else {
        break;
      }
    }
    return JsonValue(obj);
  }

  static JsonValue ParseArray(const std::wstring& s, size_t& pos) {
    JsonArray arr;
    ++pos; // skip '['
    SkipWhitespace(s, pos);

    if (pos < s.length() && s[pos] == L']') {
      ++pos;
      return JsonValue(arr);
    }

    while (pos < s.length()) {
      JsonValue val = ParseValue(s, pos);
      arr.push_back(std::move(val));

      SkipWhitespace(s, pos);
      if (pos < s.length() && s[pos] == L',') {
        ++pos;
      } else if (pos < s.length() && s[pos] == L']') {
        ++pos;
        break;
      } else {
        break;
      }
    }
    return JsonValue(arr);
  }

  static std::wstring ParseStringVal(const std::wstring& s, size_t& pos) {
    std::wstring result;
    ++pos; // skip '"'
    while (pos < s.length()) {
      wchar_t c = s[pos++];
      if (c == L'"') break;
      if (c == L'\\' && pos < s.length()) {
        wchar_t esc = s[pos++];
        if (esc == L'"') result += L'"';
        else if (esc == L'\\') result += L'\\';
        else if (esc == L'/') result += L'/';
        else if (esc == L'b') result += L'\b';
        else if (esc == L'f') result += L'\f';
        else if (esc == L'n') result += L'\n';
        else if (esc == L'r') result += L'\r';
        else if (esc == L't') result += L'\t';
        else result += esc;
      } else {
        result += c;
      }
    }
    return result;
  }

  static JsonValue ParseString(const std::wstring& s, size_t& pos) {
    return JsonValue(ParseStringVal(s, pos));
  }

  static JsonValue ParseBool(const std::wstring& s, size_t& pos) {
    if (s.compare(pos, 4, L"true") == 0) {
      pos += 4;
      return JsonValue(true);
    }
    if (s.compare(pos, 5, L"false") == 0) {
      pos += 5;
      return JsonValue(false);
    }
    return JsonValue();
  }

  static JsonValue ParseNull(const std::wstring& s, size_t& pos) {
    if (s.compare(pos, 4, L"null") == 0) {
      pos += 4;
    }
    return JsonValue();
  }

  static JsonValue ParseNumber(const std::wstring& s, size_t& pos) {
    size_t start = pos;
    if (pos < s.length() && s[pos] == L'-') ++pos;
    while (pos < s.length() && ((s[pos] >= L'0' && s[pos] <= L'9') || s[pos] == L'.' || s[pos] == L'e' || s[pos] == L'E' || s[pos] == L'+' || s[pos] == L'-')) {
      ++pos;
    }
    std::wstring num_str = s.substr(start, pos - start);
    double val = _wtof(num_str.c_str());
    return JsonValue(val);
  }

  JsonType type_{JsonType::kNull};
  bool bool_val_{false};
  double num_val_{0.0};
  std::wstring str_val_;
  JsonArray arr_val_;
  JsonObject obj_val_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_JSON_HELPER_H_
