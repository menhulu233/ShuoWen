#pragma once

#include <string>

namespace shuowen {

class TextInserter {
 public:
  static void InsertText(const std::wstring& text);
  static void Backspace(int count);
  static void SendPaste();
  static void ReplaceWith(const std::wstring& previous,
                          const std::wstring& next);
  static void CopyToClipboard(const std::wstring& text);

 private:
  static void ClearModifiers();
};

}  // namespace shuowen
