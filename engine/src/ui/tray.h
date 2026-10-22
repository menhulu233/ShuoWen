#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "store/settings.h"

namespace shuowen {

class Tray {
 public:
  struct Callbacks {
    std::function<void()> open_popup;
    std::function<void()> open_settings;
    std::function<void()> open_history;
    std::function<void(DictationMode)> set_mode;
    std::function<void()> toggle_mute;
    std::function<void()> toggle_pause;
    std::function<void()> quit;
  };

  Tray() = default;
  ~Tray();

  bool Create(Callbacks callbacks);
  void Destroy();
  void SetTooltip(const std::wstring& text);
  void SetMode(DictationMode mode);
  void SetMuted(bool muted);
  void SetPaused(bool paused);

  void* hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  void ShowMenu();
  void AddIcon();
  HMENU BuildMenu() const;

  void* hwnd_ = nullptr;
  void* icon_ = nullptr;
  UINT taskbar_created_ = 0;
  bool added_ = false;
  bool muted_ = false;
  bool paused_ = false;
  DictationMode mode_ = DictationMode::Paste;
  Callbacks callbacks_;
};

}  // namespace shuowen
