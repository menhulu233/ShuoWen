#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shuowen {
namespace {

const char* kEmpty = "";

class Parser {
 public:
  Parser(const std::string& text, std::string* error) : s_(text), error_(error) {}

  bool Parse(Json& out) {
    SkipWs();
    if (!ParseValue(out, 0)) return false;
    SkipWs();
    if (i_ != s_.size()) {
      Fail("trailing characters");
      return false;
    }
    return true;
  }

 private:
  void Fail(const std::string& message) {
    if (error_ && error_->empty()) {
      *error_ = message + " at offset " + std::to_string(i_);
    }
  }

  void SkipWs() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++i_;
      } else {
        break;
      }
    }
  }

  bool ParseValue(Json& out, int depth) {
    if (depth > 64) {
      Fail("nesting too deep");
      return false;
    }
    if (i_ >= s_.size()) {
      Fail("unexpected end");
      return false;
    }
    char c = s_[i_];
    switch (c) {
      case '{':
        return ParseObject(out, depth);
      case '[':
        return ParseArray(out, depth);
      case '"': {
        std::string value;
        if (!ParseString(value)) return false;
        out = Json(std::move(value));
        return true;
      }
      case 't':
        return Match("true", Json(true), out);
      case 'f':
        return Match("false", Json(false), out);
      case 'n':
        return Match("null", Json(), out);
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        Fail("unexpected character");
        return false;
    }
  }

  bool Match(const char* literal, Json value, Json& out) {
    size_t n = std::strlen(literal);
    if (s_.compare(i_, n, literal) != 0) {
      Fail("invalid literal");
      return false;
    }
    i_ += n;
    out = std::move(value);
    return true;
  }

  bool ParseObject(Json& out, int depth) {
    out = Json::Object();
    ++i_;  // '{'
    SkipWs();
    if (i_ < s_.size() && s_[i_] == '}') {
      ++i_;
      return true;
    }
    while (true) {
      SkipWs();
      if (i_ >= s_.size() || s_[i_] != '"') {
        Fail("expected object key");
        return false;
      }
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (i_ >= s_.size() || s_[i_] != ':') {
        Fail("expected ':'");
        return false;
      }
      ++i_;
      SkipWs();
      Json value;
      if (!ParseValue(value, depth + 1)) return false;
      out.Set(key, std::move(value));
      SkipWs();
      if (i_ >= s_.size()) {
        Fail("unterminated object");
        return false;
      }
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == '}') {
        ++i_;
        return true;
      }
      Fail("expected ',' or '}'");
      return false;
    }
  }

  bool ParseArray(Json& out, int depth) {
    out = Json::Array();
    ++i_;  // '['
    SkipWs();
    if (i_ < s_.size() && s_[i_] == ']') {
      ++i_;
      return true;
    }
    while (true) {
      Json value;
      if (!ParseValue(value, depth + 1)) return false;
      out.Push(std::move(value));
      SkipWs();
      if (i_ >= s_.size()) {
        Fail("unterminated array");
        return false;
      }
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == ']') {
        ++i_;
        return true;
      }
      Fail("expected ',' or ']'");
      return false;
    }
  }

  static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseHex4(uint32_t& value) {
    if (i_ + 4 > s_.size()) {
      Fail("truncated \\u escape");
      return false;
    }
    value = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s_[i_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        Fail("invalid hex digit");
        return false;
      }
    }
    return true;
  }

  bool ParseString(std::string& out) {
    ++i_;  // opening quote
    out.clear();
    while (i_ < s_.size()) {
      unsigned char c = static_cast<unsigned char>(s_[i_++]);
      if (c == '"') return true;
      if (c == '\\') {
        if (i_ >= s_.size()) {
          Fail("truncated escape");
          return false;
        }
        char e = s_[i_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            uint32_t cp = 0;
            if (!ParseHex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                i_ += 2;
                uint32_t low = 0;
                if (!ParseHex4(low)) return false;
                if (low >= 0xDC00 && low <= 0xDFFF) {
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                  AppendUtf8(out, cp);
                  cp = low;
                }
              }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
            AppendUtf8(out, cp);
            break;
          }
          default:
            Fail("invalid escape");
            return false;
        }
      } else {
        if (c < 0x20) {
          Fail("control character in string");
          return false;
        }
        out.push_back(static_cast<char>(c));
      }
    }
    Fail("unterminated string");
    return false;
  }

  bool ParseNumber(Json& out) {
    size_t start = i_;
    if (s_[i_] == '-') ++i_;
    if (i_ < s_.size() && s_[i_] == '0') {
      ++i_;
    } else {
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ < s_.size() && s_[i_] == '.') {
      ++i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ == start) {
      Fail("invalid number");
      return false;
    }
    std::string token = s_.substr(start, i_ - start);
    char* end = nullptr;
    double value = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(value)) {
      Fail("invalid number");
      return false;
    }
    out = Json(value);
    return true;
  }

  const std::string& s_;
  size_t i_ = 0;
  std::string* error_;
};

}  // namespace

Json Json::Array() {
  Json value;
  value.type_ = Type::Array;
  return value;
}

Json Json::Object() {
  Json value;
  value.type_ = Type::Object;
  return value;
}

Json Json::Parse(const std::string& text, std::string* error) {
  Json out;
  Parser parser(text, error);
  if (!parser.Parse(out)) return Json();
  return out;
}

bool Json::Has(const std::string& key) const {
  return type_ == Type::Object && obj_.find(key) != obj_.end();
}

const Json* Json::Find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  auto it = obj_.find(key);
  return it == obj_.end() ? nullptr : &it->second;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    arr_.clear();
    str_.clear();
    num_ = 0;
    bool_ = false;
  }
  return obj_[key];
}

void Json::Set(const std::string& key, Json value) {
  if (type_ != Type::Object) type_ = Type::Object;
  obj_[key] = std::move(value);
}

void Json::Push(Json value) {
  if (type_ != Type::Array) type_ = Type::Array;
  arr_.push_back(std::move(value));
}

std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

void Json::DumpTo(std::string& out, int indent, int depth) const {
  auto newline = [&](int d) {
    if (indent < 0) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(indent * d), ' ');
  };
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      break;
    case Type::Number: {
      if (!std::isfinite(num_)) {
        out += "0";
      } else if (num_ == static_cast<int64_t>(num_)) {
        out += std::to_string(static_cast<int64_t>(num_));
      } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", num_);
        out += buf;
      }
      break;
    }
    case Type::String:
      out.push_back('"');
      out += JsonEscape(str_);
      out.push_back('"');
      break;
    case Type::Array: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : arr_) {
        if (!first) out.push_back(',');
        first = false;
        if (indent >= 0) newline(depth + 1);
        item.DumpTo(out, indent, depth + 1);
      }
      if (!arr_.empty() && indent >= 0) newline(depth);
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, value] : obj_) {
        if (!first) out.push_back(',');
        first = false;
        if (indent >= 0) newline(depth + 1);
        out.push_back('"');
        out += JsonEscape(key);
        out += "\":";
        if (indent >= 0) out.push_back(' ');
        value.DumpTo(out, indent, depth + 1);
      }
      if (!obj_.empty() && indent >= 0) newline(depth);
      out.push_back('}');
      break;
    }
  }
}

std::string Json::Dump(int indent) const {
  std::string out;
  out.reserve(256);
  DumpTo(out, indent, 0);
  return out;
}

}  // namespace shuowen
