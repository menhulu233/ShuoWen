#include "hotkey.h"

#include <windows.h>

#include <array>
#include <cstdio>

#include "util/log.h"

namespace shuowen {
namespace {

GlobalHotkey* g_instance = nullptr;

constexpr int kVkShift = 0x10;
constexpr int kVkControl = 0x11;
constexpr int kVkMenu = 0x12;
constexpr int kVkLShift = 0xA0;
constexpr int kVkRShift = 0xA1;
constexpr int kVkLControl = 0xA2;
constexpr int kVkRControl = 0xA3;
constexpr int kVkLMenu = 0xA4;
constexpr int kVkRMenu = 0xA5;
constexpr int kVkLWin = 0x5B;
constexpr int kVkRWin = 0x5C;
constexpr int kVkEscape = 0x1B;

int OwnModBit(int vk) {
  switch (vk) {
    case kVkLControl:
    case kVkRControl:
    case kVkControl:
      return GlobalHotkey::kModCtrl;
    case kVkLMenu:
    case kVkRMenu:
    case kVkMenu:
      return GlobalHotkey::kModAlt;
    case kVkLShift:
    case kVkRShift:
    case kVkShift:
      return GlobalHotkey::kModShift;
    case kVkLWin:
    case kVkRWin:
      return GlobalHotkey::kModWin;
    default:
      return 0;
  }
}

bool IsModifier(int vk) { return OwnModBit(vk) != 0; }

}  // namespace

GlobalHotkey::~GlobalHotkey() { Uninstall(); }

bool GlobalHotkey::Install(int vk, int mods, Handler on_down, Handler on_up,
                           Handler on_cancel, CaptureHandler on_capture) {
  Uninstall();
  vk_ = vk;
  mods_ = mods;
  on_down_ = std::move(on_down);
  on_up_ = std::move(on_up);
  on_cancel_ = std::move(on_cancel);
  on_capture_ = std::move(on_capture);
  g_instance = this;
  hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr),
                            0);
  if (!hook_) {
    g_instance = nullptr;
    LogF("hotkey: failed to install hook (error %lu)", GetLastError());
    return false;
  }
  LogF("hotkey: installed vk=0x%02X mods=%d (%s)", vk_, mods_,
       HotkeyName(vk_).c_str());
  return true;
}

void GlobalHotkey::SetBinding(int vk, int mods) {
  vk_ = vk;
  mods_ = mods;
  active_ = false;
  LogF("hotkey: binding changed to vk=0x%02X mods=%d (%s)", vk_, mods_,
       HotkeyName(vk_).c_str());
}

void GlobalHotkey::SetCaptureMode(bool enabled) { capture_ = enabled; }

void GlobalHotkey::Uninstall() {
  if (hook_) {
    UnhookWindowsHookEx(static_cast<HHOOK>(hook_));
    hook_ = nullptr;
    if (g_instance == this) g_instance = nullptr;
  }
}

void GlobalHotkey::ResetState() {
  lctrl_ = rctrl_ = lalt_ = ralt_ = false;
  lshift_ = rshift_ = lwin_ = rwin_ = false;
  active_ = false;
}

int GlobalHotkey::CurrentMods() const {
  int mods = 0;
  if (lctrl_ || rctrl_) mods |= kModCtrl;
  if (lalt_ || ralt_) mods |= kModAlt;
  if (lshift_ || rshift_) mods |= kModShift;
  if (lwin_ || rwin_) mods |= kModWin;
  return mods;
}

void GlobalHotkey::UpdateModifier(int vk, bool down) {
  switch (vk) {
    case kVkLControl:
    case kVkControl:
      lctrl_ = down;
      break;
    case kVkRControl:
      rctrl_ = down;
      break;
    case kVkLMenu:
    case kVkMenu:
      lalt_ = down;
      break;
    case kVkRMenu:
      ralt_ = down;
      break;
    case kVkLShift:
    case kVkShift:
      lshift_ = down;
      break;
    case kVkRShift:
      rshift_ = down;
      break;
    case kVkLWin:
      lwin_ = down;
      break;
    case kVkRWin:
      rwin_ = down;
      break;
    default:
      break;
  }
}

bool GlobalHotkey::Matches(int vk) const {
  if (vk != vk_) return false;
  const int effective = CurrentMods() & ~OwnModBit(vk);
  return effective == mods_;
}

LRESULT CALLBACK GlobalHotkey::HookProc(int code, WPARAM wparam, LPARAM lparam) {
  if (code < 0 || !g_instance) return CallNextHookEx(nullptr, code, wparam, lparam);
  return g_instance->Handle(code, wparam, lparam);
}

LRESULT GlobalHotkey::Handle(int code, WPARAM wparam, LPARAM lparam) {
  if (code < 0) return CallNextHookEx(nullptr, code, wparam, lparam);
  const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
  const int vk = static_cast<int>(data->vkCode);
  const bool injected = (data->flags & LLKHF_INJECTED) != 0;

  // Our own SendInput releases modifiers while inserting; if the hook treated
  // those as the user letting go, dictation would stop mid-sentence.
  if (injected) return CallNextHookEx(nullptr, code, wparam, lparam);

  const bool is_down =
      wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
  const bool is_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
  if (!is_down && !is_up) {
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

  if (is_down) UpdateModifier(vk, true);
  else if (is_up) UpdateModifier(vk, false);

  if (capture_ && is_down) {
    const int captured_mods = CurrentMods() & ~OwnModBit(vk);
    capture_ = false;
    LogF("hotkey: captured vk=0x%02X mods=%d (%s)", vk, captured_mods,
         HotkeyName(vk).c_str());
    if (on_capture_) on_capture_(vk, captured_mods);
    return IsModifier(vk) ? CallNextHookEx(nullptr, code, wparam, lparam)
                          : static_cast<LRESULT>(1);
  }

  if (is_down && !active_ && Matches(vk)) {
    active_ = true;
    LogF("hotkey: DOWN vk=0x%02X mods=%d", vk, mods_);
    if (on_down_) on_down_();
    const bool swallow = mods_ != 0 || OwnModBit(vk) == 0;
    return swallow ? static_cast<LRESULT>(1)
                   : CallNextHookEx(nullptr, code, wparam, lparam);
  }

  if (is_down && active_ && vk == kVkEscape) {
    active_ = false;
    Log("hotkey: ESC cancel");
    if (on_cancel_) on_cancel_();
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

  if (is_up && active_ && vk == vk_) {
    active_ = false;
    LogF("hotkey: UP vk=0x%02X", vk);
    if (on_up_) on_up_();
  }

  return CallNextHookEx(nullptr, code, wparam, lparam);
}

std::string HotkeyName(int vk) {
  if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
  if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
  if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
  switch (vk) {
    case VK_LBUTTON: return "MouseLeft";
    case VK_RBUTTON: return "MouseRight";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_PAUSE: return "Pause";
    case VK_CAPITAL: return "CapsLock";
    case VK_ESCAPE: return "Esc";
    case VK_SPACE: return "Space";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_LEFT: return "Left";
    case VK_UP: return "Up";
    case VK_RIGHT: return "Right";
    case VK_DOWN: return "Down";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_LWIN: return "LeftWin";
    case VK_RWIN: return "RightWin";
    case VK_APPS: return "Menu";
    case VK_LSHIFT: return "LeftShift";
    case VK_RSHIFT: return "RightShift";
    case VK_LCONTROL: return "LeftCtrl";
    case VK_RCONTROL: return "RightCtrl";
    case VK_LMENU: return "LeftAlt";
    case VK_RMENU: return "RightAlt";
    case VK_OEM_1: return ";";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2: return "/";
    case VK_OEM_3: return "`";
    case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]";
    case VK_OEM_7: return "'";
    default: break;
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "VK%02X", vk);
  return buffer;
}

}  // namespace shuowen
