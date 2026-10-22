#include "inserter.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

constexpr WORD kVkBack = 0x08;
constexpr WORD kVkControl = 0x11;
constexpr WORD kVkV = 0x56;
constexpr WORD kVkLShift = 0xA0;
constexpr WORD kVkRShift = 0xA1;
constexpr WORD kVkLControl = 0xA2;
constexpr WORD kVkRControl = 0xA3;
constexpr WORD kVkLMenu = 0xA4;
constexpr WORD kVkRMenu = 0xA5;
constexpr WORD kVkLWin = 0x5B;
constexpr WORD kVkRWin = 0x5C;

INPUT MakeUnicode(wchar_t c, bool key_up) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = 0;
  input.ki.wScan = c;
  input.ki.dwFlags = KEYEVENTF_UNICODE | (key_up ? KEYEVENTF_KEYUP : 0);
  return input;
}

INPUT MakeVk(WORD vk, bool key_up) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = vk;
  input.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
  return input;
}

void Send(const std::vector<INPUT>& inputs) {
  if (inputs.empty()) return;
  const UINT sent = SendInput(static_cast<UINT>(inputs.size()),
                              const_cast<INPUT*>(inputs.data()),
                              static_cast<int>(sizeof(INPUT)));
  if (sent != inputs.size()) {
    LogF("inserter: SendInput sent %u/%zu (error %lu)", sent, inputs.size(),
         GetLastError());
  }
}

}  // namespace

void TextInserter::ClearModifiers() {
  const WORD modifiers[] = {kVkLShift, kVkRShift, kVkLControl, kVkRControl,
                            kVkLMenu,  kVkRMenu,  kVkLWin,     kVkRWin};
  std::vector<INPUT> inputs;
  inputs.reserve(std::size(modifiers));
  for (WORD vk : modifiers) inputs.push_back(MakeVk(vk, true));
  Send(inputs);
}

void TextInserter::InsertText(const std::wstring& text) {
  if (text.empty()) return;
  ClearModifiers();

  constexpr size_t kChunk = 256;
  for (size_t start = 0; start < text.size(); start += kChunk) {
    const size_t end = std::min(text.size(), start + kChunk);
    std::vector<INPUT> inputs;
    inputs.reserve((end - start) * 2);
    for (size_t i = start; i < end; ++i) {
      inputs.push_back(MakeUnicode(text[i], false));
      inputs.push_back(MakeUnicode(text[i], true));
    }
    Send(inputs);
  }
}

void TextInserter::Backspace(int count) {
  if (count <= 0) return;
  ClearModifiers();
  constexpr int kChunk = 128;
  while (count > 0) {
    const int batch = std::min(count, kChunk);
    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<size_t>(batch) * 2);
    for (int i = 0; i < batch; ++i) {
      inputs.push_back(MakeVk(kVkBack, false));
      inputs.push_back(MakeVk(kVkBack, true));
    }
    Send(inputs);
    count -= batch;
  }
}

void TextInserter::SendPaste() {
  ClearModifiers();
  Send({MakeVk(kVkControl, false), MakeVk(kVkV, false), MakeVk(kVkV, true),
        MakeVk(kVkControl, true)});
}

void TextInserter::ReplaceWith(const std::wstring& previous,
                               const std::wstring& next) {
  if (previous == next) return;
  const size_t prefix = CommonPrefixCodePoints(previous, next);
  const size_t previous_count = CodePoints(previous).size();
  const size_t remove = previous_count > prefix ? previous_count - prefix : 0;
  if (remove > 0) Backspace(static_cast<int>(remove));
  const std::wstring suffix = CodePointSuffix(next, prefix);
  if (!suffix.empty()) InsertText(suffix);
}

void TextInserter::CopyToClipboard(const std::wstring& text) {
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory) {
    void* target = GlobalLock(memory);
    if (target) {
      std::memcpy(target, text.c_str(), bytes);
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
      }
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

}  // namespace shuowen
