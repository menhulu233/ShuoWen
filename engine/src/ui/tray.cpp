#include "tray.h"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <iterator>

#include "config.h"
#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuSettings = 2;
constexpr UINT kMenuHistory = 3;
constexpr UINT kMenuMute = 4;
constexpr UINT kMenuPause = 5;
constexpr UINT kMenuQuit = 6;
constexpr UINT kMenuModePaste = 10;
constexpr UINT kMenuModeType = 11;
constexpr UINT kMenuModeOnCall = 12;

const wchar_t kWindowClass[] = L"ShuoWenTrayWindow";

}  // namespace

Tray::~Tray() { Destroy(); }

bool Tray::Create(Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, kWindowClass, L"ShuoWen", WS_OVERLAPPED, 0, 0,
                              0, 0, nullptr, nullptr, GetModuleHandleW(nullptr),
                              this);
  if (!hwnd) {
    LogF("tray: CreateWindow failed (%lu)", GetLastError());
    return false;
  }
  hwnd_ = hwnd;

  const auto icon_path = paths::ExeDir() / L"assets" / L"shuowen.ico";
  HICON icon = static_cast<HICON>(LoadImageW(
      nullptr, icon_path.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
  if (!icon) {
    icon = LoadIconW(nullptr, IDI_APPLICATION);
  }
  icon_ = icon;

  AddIcon();
  Log("tray: icon added");
  return true;
}

void Tray::AddIcon() {
  if (!hwnd_ || !icon_) return;
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = static_cast<HWND>(hwnd_);
  data.uID = 1;
  data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data.uCallbackMessage = kTrayMessage;
  data.hIcon = static_cast<HICON>(icon_);
  std::wcsncpy(data.szTip, L"说文 ShuoWen · 就绪", std::size(data.szTip) - 1);
  added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void Tray::Destroy() {
  if (!hwnd_) return;
  if (added_) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = static_cast<HWND>(hwnd_);
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
  }
  DestroyWindow(static_cast<HWND>(hwnd_));
  hwnd_ = nullptr;
  UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void Tray::SetTooltip(const std::wstring& text) {
  if (!hwnd_) return;
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = static_cast<HWND>(hwnd_);
  data.uID = 1;
  data.uFlags = NIF_TIP;
  std::wcsncpy(data.szTip, text.c_str(), std::size(data.szTip) - 1);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

void Tray::SetMode(DictationMode mode) { mode_ = mode; }
void Tray::SetMuted(bool muted) { muted_ = muted; }
void Tray::SetPaused(bool paused) { paused_ = paused; }

HMENU Tray::BuildMenu() const {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuOpen, L"打开说文面板");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  HMENU modes = CreatePopupMenu();
  AppendMenuW(modes, MF_STRING | (mode_ == DictationMode::Paste ? MF_CHECKED : 0),
              kMenuModePaste, L"说完插入");
  AppendMenuW(modes, MF_STRING | (mode_ == DictationMode::Type ? MF_CHECKED : 0),
              kMenuModeType, L"逐字插入");
  AppendMenuW(modes,
              MF_STRING | (mode_ == DictationMode::OnCall ? MF_CHECKED : 0),
              kMenuModeOnCall, L"持续候机 OnCall");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"听写模式");

  AppendMenuW(menu,
              MF_STRING | (muted_ ? MF_CHECKED : 0), kMenuMute,
              L"静音麦克风");
  AppendMenuW(menu,
              MF_STRING | (paused_ ? MF_CHECKED : 0) |
                  (mode_ == DictationMode::OnCall ? 0 : MF_GRAYED),
              kMenuPause, L"暂停候机");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuSettings, L"设置…");
  AppendMenuW(menu, MF_STRING, kMenuHistory, L"历史…");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, L"退出说文");
  return menu;
}

void Tray::ShowMenu() {
  if (!hwnd_) return;
  HMENU menu = BuildMenu();
  POINT cursor;
  GetCursorPos(&cursor);
  SetForegroundWindow(static_cast<HWND>(hwnd_));
  const UINT command = TrackPopupMenu(
      menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y,
      0, static_cast<HWND>(hwnd_), nullptr);
  DestroyMenu(menu);

  switch (command) {
    case kMenuOpen:
      if (callbacks_.open_popup) callbacks_.open_popup();
      break;
    case kMenuSettings:
      if (callbacks_.open_settings) callbacks_.open_settings();
      break;
    case kMenuHistory:
      if (callbacks_.open_history) callbacks_.open_history();
      break;
    case kMenuModePaste:
      if (callbacks_.set_mode) callbacks_.set_mode(DictationMode::Paste);
      break;
    case kMenuModeType:
      if (callbacks_.set_mode) callbacks_.set_mode(DictationMode::Type);
      break;
    case kMenuModeOnCall:
      if (callbacks_.set_mode) callbacks_.set_mode(DictationMode::OnCall);
      break;
    case kMenuMute:
      if (callbacks_.toggle_mute) callbacks_.toggle_mute();
      break;
    case kMenuPause:
      if (callbacks_.toggle_pause) callbacks_.toggle_pause();
      break;
    case kMenuQuit:
      if (callbacks_.quit) callbacks_.quit();
      break;
    default:
      break;
  }
}

LRESULT CALLBACK Tray::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  auto* self =
      reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_) return self->Handle(hwnd, message, wparam, lparam);
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Tray::Handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == taskbar_created_ && taskbar_created_ != 0) {
    AddIcon();
    return 0;
  }
  if (message == kTrayMessage) {
    const UINT event = LOWORD(lparam);
    if (event == WM_LBUTTONUP || event == NIN_SELECT) {
      if (callbacks_.open_popup) callbacks_.open_popup();
    } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
      ShowMenu();
    }
    return 0;
  }
  if (message == WM_COMMAND) {
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace shuowen
