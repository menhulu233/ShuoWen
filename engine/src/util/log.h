#pragma once

#include <string>

namespace shuowen {

void Log(const std::string& message);
void LogF(const char* format, ...);
void LogW(const std::wstring& message);

}  // namespace shuowen
