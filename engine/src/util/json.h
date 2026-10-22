#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace shuowen {

// Minimal JSON value used by the engine IPC, settings and history files.
// It intentionally supports only what this project needs, with deterministic
// object ordering (std::map) so saved files diff cleanly.
class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(bool value) : type_(Type::Bool), bool_(value) {}
  Json(int value) : type_(Type::Number), num_(value) {}
  Json(int64_t value) : type_(Type::Number), num_(static_cast<double>(value)) {}
  Json(double value) : type_(Type::Number), num_(value) {}
  Json(const char* value) : type_(Type::String), str_(value ? value : "") {}
  Json(std::string value) : type_(Type::String), str_(std::move(value)) {}

  static Json Array();
  static Json Object();
  static Json Parse(const std::string& text, std::string* error = nullptr);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool AsBool(bool fallback = false) const {
    return type_ == Type::Bool ? bool_ : fallback;
  }
  double AsDouble(double fallback = 0.0) const {
    return type_ == Type::Number ? num_ : fallback;
  }
  int AsInt(int fallback = 0) const {
    return type_ == Type::Number ? static_cast<int>(num_) : fallback;
  }
  int64_t AsInt64(int64_t fallback = 0) const {
    return type_ == Type::Number ? static_cast<int64_t>(num_) : fallback;
  }
  const std::string& AsString() const { return str_; }

  const std::vector<Json>& AsArray() const { return arr_; }
  std::vector<Json>& MutableArray() { return arr_; }
  const std::map<std::string, Json>& AsObject() const { return obj_; }
  std::map<std::string, Json>& MutableObject() { return obj_; }

  bool Has(const std::string& key) const;
  const Json* Find(const std::string& key) const;
  Json& operator[](const std::string& key);
  void Set(const std::string& key, Json value);
  void Push(Json value);

  std::string Dump(int indent = -1) const;

 private:
  void DumpTo(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  std::vector<Json> arr_;
  std::map<std::string, Json> obj_;
};

std::string JsonEscape(const std::string& text);

}  // namespace shuowen
