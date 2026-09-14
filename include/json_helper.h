// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_JSON_HELPER_H_
#define LITE_PROC_MANAGER_JSON_HELPER_H_

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

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

// Compatibility wrapper around JsonCpp. All parsing and serialization is
// delegated to JsonCpp; the wide-string API is retained for existing callers.
class JsonValue {
 public:
  JsonValue() = default;
  JsonValue(bool value) : value_(value) {}
  JsonValue(int value) : value_(value) {}
  JsonValue(uint32_t value) : value_(value) {}
  JsonValue(int64_t value) : value_(Json::Int64(value)) {}
  JsonValue(uint64_t value) : value_(Json::UInt64(value)) {}
  JsonValue(double value) {
    if (std::isfinite(value)) {
      value_ = value;
    }
  }
  JsonValue(const wchar_t* value)
      : JsonValue(std::wstring(value != nullptr ? value : L"")) {}
  JsonValue(std::wstring value) {
    std::string utf8;
    if (WideToUtf8(value, &utf8)) {
      value_ = std::move(utf8);
    }
  }
  JsonValue(JsonArray values) : value_(Json::arrayValue) {
    for (const auto& value : values) {
      value_.append(value.value_);
    }
  }
  JsonValue(JsonObject values) : value_(Json::objectValue) {
    for (const auto& pair : values) {
      std::string key;
      if (WideToUtf8(pair.first, &key)) {
        value_[key] = pair.second.value_;
      }
    }
  }

  JsonType type() const {
    switch (value_.type()) {
      case Json::booleanValue:
        return JsonType::kBoolean;
      case Json::intValue:
      case Json::uintValue:
      case Json::realValue:
        return JsonType::kNumber;
      case Json::stringValue:
        return JsonType::kString;
      case Json::arrayValue:
        return JsonType::kArray;
      case Json::objectValue:
        return JsonType::kObject;
      case Json::nullValue:
      default:
        return JsonType::kNull;
    }
  }

  bool is_null() const { return value_.isNull(); }
  bool is_bool() const { return value_.isBool(); }
  bool is_number() const { return value_.isNumeric(); }
  bool is_string() const { return value_.isString(); }
  bool is_array() const { return value_.isArray(); }
  bool is_object() const { return value_.isObject(); }

  bool as_bool(bool default_value = false) const {
    return value_.isBool() ? value_.asBool() : default_value;
  }

  int as_int(int default_value = 0) const {
    if (value_.isInt64()) {
      Json::Int64 number = value_.asInt64();
      if (number >= std::numeric_limits<int>::min() &&
          number <= std::numeric_limits<int>::max()) {
        return static_cast<int>(number);
      }
    } else if (value_.isUInt64()) {
      Json::UInt64 number = value_.asUInt64();
      if (number <= static_cast<Json::UInt64>(std::numeric_limits<int>::max())) {
        return static_cast<int>(number);
      }
    }
    return default_value;
  }

  uint32_t as_uint(uint32_t default_value = 0) const {
    if (value_.isUInt64()) {
      Json::UInt64 number = value_.asUInt64();
      if (number <= std::numeric_limits<uint32_t>::max()) {
        return static_cast<uint32_t>(number);
      }
    } else if (value_.isInt64()) {
      Json::Int64 number = value_.asInt64();
      if (number >= 0 &&
          static_cast<Json::UInt64>(number) <=
              std::numeric_limits<uint32_t>::max()) {
        return static_cast<uint32_t>(number);
      }
    }
    return default_value;
  }

  int64_t as_int64(int64_t default_value = 0) const {
    if (value_.isInt64()) {
      return static_cast<int64_t>(value_.asInt64());
    }
    if (value_.isUInt64()) {
      Json::UInt64 number = value_.asUInt64();
      if (number <= static_cast<Json::UInt64>(
                        std::numeric_limits<int64_t>::max())) {
        return static_cast<int64_t>(number);
      }
    }
    return default_value;
  }

  double as_double(double default_value = 0.0) const {
    if (!value_.isNumeric()) {
      return default_value;
    }
    double number = value_.asDouble();
    return std::isfinite(number) ? number : default_value;
  }

  std::wstring as_string(const std::wstring& default_value = L"") const {
    if (!value_.isString()) {
      return default_value;
    }
    std::wstring wide;
    return Utf8ToWide(value_.asString(), &wide) ? wide : default_value;
  }

  const JsonArray& as_array() const {
    EnsureArrayCache();
    return array_cache_;
  }

  const JsonObject& as_object() const {
    EnsureObjectCache();
    return object_cache_;
  }

  bool has_key(const std::wstring& key) const {
    if (!value_.isObject()) {
      return false;
    }
    std::string utf8_key;
    return WideToUtf8(key, &utf8_key) && value_.isMember(utf8_key);
  }

  const JsonValue& operator[](const std::wstring& key) const {
    static const JsonValue null_value;
    if (!value_.isObject()) {
      return null_value;
    }

    EnsureObjectCache();
    auto found = object_index_.find(key);
    return found != object_index_.end()
               ? object_cache_[found->second].second
               : null_value;
  }

  std::wstring Serialize(int indent = 0) const {
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = std::string(
        static_cast<size_t>(std::clamp(indent, 0, 16)), ' ');
    builder["enableYAMLCompatibility"] = false;
    builder["dropNullPlaceholders"] = false;
    builder["useSpecialFloats"] = false;
    builder["emitUTF8"] = true;
    builder["precision"] = 17;
    builder["precisionType"] = "significant";

    try {
      std::string utf8 = Json::writeString(builder, value_);
      std::wstring wide;
      return Utf8ToWide(utf8, &wide) ? wide : L"";
    } catch (const std::exception&) {
      return L"";
    }
  }

  static bool TryParse(const std::wstring& json_text, JsonValue* result,
                       std::wstring* error = nullptr) {
    if (result == nullptr) {
      return false;
    }

    std::string utf8;
    if (!WideToUtf8(json_text, &utf8)) {
      if (error != nullptr) {
        *error = L"JSON contains invalid Unicode.";
      }
      return false;
    }
    if (utf8.size() > kMaximumInputBytes) {
      if (error != nullptr) {
        *error = L"JSON input exceeds the size limit.";
      }
      return false;
    }
    if (!HasValidNumberTokens(utf8)) {
      if (error != nullptr) {
        *error = L"JSON contains an invalid number.";
      }
      return false;
    }

    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::ecma404Mode(&builder.settings_);
    builder["collectComments"] = false;
    builder["rejectDupKeys"] = true;
    builder["skipBom"] = true;
    builder["stackLimit"] = kMaximumNestingDepth;

    Json::Value parsed;
    std::string parse_error;
    try {
      std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
      if (!reader ||
          !reader->parse(utf8.data(), utf8.data() + utf8.size(),
                         &parsed, &parse_error)) {
        if (error != nullptr) {
          std::wstring wide_error;
          *error = Utf8ToWide(parse_error, &wide_error)
                       ? wide_error
                       : L"Invalid JSON input.";
        }
        return false;
      }
    } catch (const std::exception& exception) {
      if (error != nullptr) {
        std::wstring wide_error;
        *error = Utf8ToWide(exception.what(), &wide_error)
                     ? wide_error
                     : L"Invalid JSON input.";
      }
      return false;
    }

    *result = JsonValue(std::move(parsed));
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }

  static JsonValue Parse(const std::wstring& json_text) {
    JsonValue result;
    TryParse(json_text, &result);
    return result;
  }

 private:
  static constexpr size_t kMaximumInputBytes = 4 * 1024 * 1024;
  static constexpr int kMaximumNestingDepth = 128;

  explicit JsonValue(Json::Value value) : value_(std::move(value)) {}

  static bool HasValidNumberTokens(std::string_view json) {
    size_t position = 0;
    while (position < json.size()) {
      if (json[position] == '"') {
        ++position;
        while (position < json.size()) {
          if (json[position] == '\\') {
            position += std::min<size_t>(2, json.size() - position);
          } else if (json[position] == '"') {
            ++position;
            break;
          } else {
            ++position;
          }
        }
        continue;
      }

      if (json[position] != '-' &&
          (json[position] < '0' || json[position] > '9')) {
        ++position;
        continue;
      }

      if (json[position] == '-') {
        ++position;
        if (position >= json.size() || json[position] < '0' ||
            json[position] > '9') {
          return false;
        }
      }

      if (json[position] == '0') {
        ++position;
        if (position < json.size() && json[position] >= '0' &&
            json[position] <= '9') {
          return false;
        }
      } else {
        if (json[position] < '1' || json[position] > '9') {
          return false;
        }
        while (position < json.size() && json[position] >= '0' &&
               json[position] <= '9') {
          ++position;
        }
      }

      if (position < json.size() && json[position] == '.') {
        ++position;
        if (position >= json.size() || json[position] < '0' ||
            json[position] > '9') {
          return false;
        }
        while (position < json.size() && json[position] >= '0' &&
               json[position] <= '9') {
          ++position;
        }
      }

      if (position < json.size() &&
          (json[position] == 'e' || json[position] == 'E')) {
        ++position;
        if (position < json.size() &&
            (json[position] == '+' || json[position] == '-')) {
          ++position;
        }
        if (position >= json.size() || json[position] < '0' ||
            json[position] > '9') {
          return false;
        }
        while (position < json.size() && json[position] >= '0' &&
               json[position] <= '9') {
          ++position;
        }
      }

      if (position < json.size() && json[position] != ' ' &&
          json[position] != '\t' && json[position] != '\r' &&
          json[position] != '\n' && json[position] != ',' &&
          json[position] != ']' && json[position] != '}') {
        return false;
      }
    }
    return true;
  }

  static bool WideToUtf8(const std::wstring& wide, std::string* utf8) {
    if (utf8 == nullptr) {
      return false;
    }
    if (wide.empty()) {
      utf8->clear();
      return true;
    }
    if (wide.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
      return false;
    }

    int length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
      return false;
    }

    utf8->assign(static_cast<size_t>(length), '\0');
    return WideCharToMultiByte(
               CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
               static_cast<int>(wide.size()), utf8->data(), length,
               nullptr, nullptr) == length;
  }

  static bool Utf8ToWide(const std::string& utf8, std::wstring* wide) {
    if (wide == nullptr) {
      return false;
    }
    if (utf8.empty()) {
      wide->clear();
      return true;
    }
    if (utf8.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
      return false;
    }

    int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
      return false;
    }

    wide->assign(static_cast<size_t>(length), L'\0');
    return MultiByteToWideChar(
               CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
               static_cast<int>(utf8.size()), wide->data(), length) == length;
  }

  void EnsureArrayCache() const {
    if (array_cache_initialized_) {
      return;
    }
    array_cache_initialized_ = true;
    if (!value_.isArray()) {
      return;
    }
    array_cache_.reserve(value_.size());
    for (const auto& child : value_) {
      array_cache_.push_back(JsonValue(child));
    }
  }

  void EnsureObjectCache() const {
    if (object_cache_initialized_) {
      return;
    }
    object_cache_initialized_ = true;
    if (!value_.isObject()) {
      return;
    }

    for (const auto& name : value_.getMemberNames()) {
      std::wstring wide_name;
      if (!Utf8ToWide(name, &wide_name)) {
        continue;
      }
      size_t index = object_cache_.size();
      object_cache_.push_back({wide_name, JsonValue(value_[name])});
      object_index_.emplace(std::move(wide_name), index);
    }
  }

  Json::Value value_;
  mutable bool array_cache_initialized_{false};
  mutable JsonArray array_cache_;
  mutable bool object_cache_initialized_{false};
  mutable JsonObject object_cache_;
  mutable std::map<std::wstring, size_t> object_index_;
};

}  // namespace lite_proc_manager

#endif  // LITE_PROC_MANAGER_JSON_HELPER_H_
