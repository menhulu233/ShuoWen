#include "text.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <unordered_set>

namespace shuowen {
namespace {

bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

const std::unordered_set<wchar_t>& CjkPunct() {
  static const std::unordered_set<wchar_t> set = {
      L'，', L'。', L'！', L'？', L'；', L'：', L'、', L'（', L'）', L'《',
      L'》', L'〈', L'〉', L'【', L'】', L'「', L'」', L'『', L'』', L'“',
      L'”', L'‘', L'’', L'…', L'—', L'·', L'～'};
  return set;
}

const std::unordered_set<wchar_t>& AsciiPunct() {
  static const std::unordered_set<wchar_t> set = {
      L',', L'.', L'!', L'?', L';', L':', L'%', L')', L']', L'}'};
  return set;
}

}  // namespace

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
  }
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), length);
  return out;
}

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (length <= 0) {
    length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0,
                                 nullptr, nullptr);
    if (length <= 0) return {};
  }
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), length, nullptr, nullptr);
  return out;
}

std::wstring TrimW(const std::wstring& text) {
  size_t begin = 0;
  size_t end = text.size();
  auto isSpace = [](wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
           c == 0x3000 || c == 0x00A0;
  };
  while (begin < end && isSpace(text[begin])) ++begin;
  while (end > begin && isSpace(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

std::string TrimUtf8(const std::string& text) {
  return WideToUtf8(TrimW(Utf8ToWide(text)));
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
  std::vector<std::wstring> lines;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == L'\n' || text[i] == L'\r') {
      if (i > start) lines.push_back(text.substr(start, i - start));
      if (i < text.size() && text[i] == L'\r' && i + 1 < text.size() &&
          text[i + 1] == L'\n') {
        ++i;
      }
      start = i + 1;
    }
  }
  return lines;
}

std::wstring JoinW(const std::vector<std::wstring>& parts,
                   const std::wstring& sep) {
  std::wstring out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

bool IsCjk(wchar_t c) {
  return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
         (c >= 0xF900 && c <= 0xFAFF);
}

bool IsCjkish(wchar_t c) {
  return IsCjk(c) || CjkPunct().count(c) > 0;
}

std::wstring NormalizeCjk(const std::wstring& text) {
  if (text.empty()) return text;
  std::wstring out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    wchar_t c = text[i];
    if (c == L' ') {
      wchar_t prev = out.empty() ? 0 : out.back();
      size_t j = i + 1;
      while (j < text.size() && text[j] == L' ') ++j;
      wchar_t next = j < text.size() ? text[j] : 0;
      bool dropAscii = next && AsciiPunct().count(next) > 0;
      bool dropCjk = prev && next && IsCjkish(prev) && IsCjkish(next);
      if (dropAscii || dropCjk) {
        ++i;
        continue;
      }
    }
    out.push_back(c);
    ++i;
  }
  return out;
}

std::wstring EnsureFinalPunct(const std::wstring& text) {
  if (text.empty()) return text;
  wchar_t last = text.back();
  if (CjkPunct().count(last) || AsciiPunct().count(last)) return text;
  return text + (IsCjk(last) ? L"。" : L".");
}

std::vector<uint32_t> CodePoints(const std::wstring& text) {
  std::vector<uint32_t> out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    wchar_t c = text[i];
    if (IsHighSurrogate(c) && i + 1 < text.size() &&
        IsLowSurrogate(text[i + 1])) {
      uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) +
                    (static_cast<uint32_t>(text[i + 1]) - 0xDC00);
      out.push_back(cp);
      ++i;
    } else {
      out.push_back(static_cast<uint32_t>(c));
    }
  }
  return out;
}

size_t CommonPrefixCodePoints(const std::wstring& a, const std::wstring& b) {
  auto ca = CodePoints(a);
  auto cb = CodePoints(b);
  size_t n = std::min(ca.size(), cb.size());
  size_t i = 0;
  while (i < n && ca[i] == cb[i]) ++i;
  return i;
}

std::wstring CodePointSuffix(const std::wstring& text, size_t skipCodePoints) {
  size_t index = 0;
  size_t count = 0;
  while (index < text.size() && count < skipCodePoints) {
    wchar_t c = text[index];
    if (IsHighSurrogate(c) && index + 1 < text.size() &&
        IsLowSurrogate(text[index + 1])) {
      index += 2;
    } else {
      ++index;
    }
    ++count;
  }
  return text.substr(std::min(index, text.size()));
}

std::wstring ToTraditional(const std::wstring& text) {
  if (text.empty()) return text;
  int needed = LCMapStringEx(LOCALE_NAME_SYSTEM_DEFAULT, LCMAP_TRADITIONAL_CHINESE,
                             text.data(), static_cast<int>(text.size()), nullptr, 0,
                             nullptr, nullptr, 0);
  if (needed <= 0) return text;
  std::wstring out(static_cast<size_t>(needed), L'\0');
  int written = LCMapStringEx(LOCALE_NAME_SYSTEM_DEFAULT,
                              LCMAP_TRADITIONAL_CHINESE, text.data(),
                              static_cast<int>(text.size()), out.data(), needed,
                              nullptr, nullptr, 0);
  if (written <= 0) return text;
  out.resize(static_cast<size_t>(written));
  return out;
}

std::string NowIso8601() {
  SYSTEMTIME utc;
  GetSystemTime(&utc);
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                utc.wSecond, utc.wMilliseconds);
  return buffer;
}

int64_t NowUnixMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  // 100-ns intervals since 1601-01-01 -> milliseconds since 1970-01-01.
  return static_cast<int64_t>(value.QuadPart / 10000ULL) - 11644473600000LL;
}

std::wstring NewUuid() {
  GUID guid{};
  if (CoCreateGuid(&guid) != S_OK) return L"";
  wchar_t buffer[64];
  if (StringFromGUID2(guid, buffer, 64) <= 0) return L"";
  std::wstring out(buffer);
  if (out.size() >= 2 && out.front() == L'{' && out.back() == L'}') {
    out = out.substr(1, out.size() - 2);
  }
  std::transform(out.begin(), out.end(), out.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return out;
}

}  // namespace shuowen
