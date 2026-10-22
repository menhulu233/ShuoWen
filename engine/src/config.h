#pragma once

#include <filesystem>
#include <string>

namespace shuowen {

inline constexpr wchar_t kAppName[] = L"ShuoWen";
inline constexpr char kAppNameUtf8[] = "ShuoWen";
inline constexpr char kVersion[] = "0.1.0";
inline constexpr int kSampleRate = 16000;
inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\shuowen.engine";
inline constexpr wchar_t kSingleInstanceMutex[] = L"Local\\ShuoWenEngine";

namespace paths {

std::filesystem::path ExeDir();
std::filesystem::path ExePath();
std::filesystem::path DataDir();
std::filesystem::path ModelsWritableRoot();
std::filesystem::path BundledModelsRoot();
std::filesystem::path LogFile();
std::filesystem::path UiExe();

}  // namespace paths

}  // namespace shuowen
