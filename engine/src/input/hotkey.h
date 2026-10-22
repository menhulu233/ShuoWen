#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace shuowen {

class GlobalHotkey {
 public:
  using Handler = std::function<void()>;
  using CaptureHandler = std::function<void(int vk, int mods)>;

  static constexpr int kModCtrl = 1;
  static constexpr int kModAlt = 2;
  static constexpr int kModShift = 4;
  static constexpr int kModWin = 8;

  GlobalHotkey() = default;
  ~GlobalHotkey();

  bool Install(int vk, int mods, Handler on_down, Handler on_up,
               Handler on_cancel, CaptureHandler on_capture);
  void SetBinding(int vk, int mods);
  void SetCaptureMode(bool enabled);
  void Uninstall();
  bool installed() const { return hook_ != nullptr; }

 private:
  static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(int code, WPARAM wparam, LPARAM lparam);
  bool Matches(int vk) const;
  int CurrentMods() const;
  void UpdateModifier(int vk, bool down);
  void ResetState();

  void* hook_ = nullptr;
  int vk_ = 0;
  int mods_ = 0;
  bool capture_ = false;
  bool active_ = false;
  bool lctrl_ = false, rctrl_ = false;
  bool lalt_ = false, ralt_ = false;
  bool lshift_ = false, rshift_ = false;
  bool lwin_ = false, rwin_ = false;
  Handler on_down_;
  Handler on_up_;
  Handler on_cancel_;
  CaptureHandler on_capture_;
};

std::string HotkeyName(int vk);

}  // namespace shuowen
