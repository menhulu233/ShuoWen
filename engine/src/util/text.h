#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace shuowen {

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

std::wstring TrimW(const std::wstring& text);
std::string TrimUtf8(const std::string& text);
std::vector<std::wstring> SplitLines(const std::wstring& text);
std::wstring JoinW(const std::vector<std::wstring>& parts, const std::wstring& sep);

bool IsCjk(wchar_t c);
bool IsCjkish(wchar_t c);
std::wstring NormalizeCjk(const std::wstring& text);
std::wstring EnsureFinalPunct(const std::wstring& text);

std::vector<uint32_t> CodePoints(const std::wstring& text);
size_t CommonPrefixCodePoints(const std::wstring& a, const std::wstring& b);
std::wstring CodePointSuffix(const std::wstring& text, size_t skipCodePoints);

std::wstring ToTraditional(const std::wstring& text);

std::string NowIso8601();
int64_t NowUnixMs();
std::wstring NewUuid();

}  // namespace shuowen
