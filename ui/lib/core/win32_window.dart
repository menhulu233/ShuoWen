import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';

typedef _RectNative = Int32 Function(
    Uint32 uiAction, Uint32 uiParam, Pointer<_Rect> pvParam, Uint32 fWinIni);
typedef _RectDart = int Function(
    int uiAction, int uiParam, Pointer<_Rect> pvParam, int fWinIni);

typedef _FindWindowNative = IntPtr Function(IntPtr, Pointer<Utf16>);
typedef _FindWindowDart = int Function(int, Pointer<Utf16>);

typedef _GetWindowRectNative = Int32 Function(IntPtr, Pointer<_Rect>);
typedef _GetWindowRectDart = int Function(int, Pointer<_Rect>);

typedef _CreateRoundRectRgnNative = IntPtr Function(
    Int32, Int32, Int32, Int32, Int32, Int32);
typedef _CreateRoundRectRgnDart = int Function(
    int, int, int, int, int, int);

typedef _SetWindowRgnNative = Int32 Function(IntPtr, IntPtr, Int32);
typedef _SetWindowRgnDart = int Function(int, int, int);

final class _Rect extends Struct {
  @Int32()
  external int left;
  @Int32()
  external int top;
  @Int32()
  external int right;
  @Int32()
  external int bottom;
}

class Win32Window {
  static const MethodChannel _channel = MethodChannel('shuowen/ipc');
  static final DynamicLibrary _user32 = DynamicLibrary.open('user32.dll');
  static final DynamicLibrary _gdi32 = DynamicLibrary.open('gdi32.dll');

  static Rect workArea() {
    final rect = calloc<_Rect>();
    try {
      final spi = _user32.lookupFunction<_RectNative, _RectDart>(
          'SystemParametersInfoW');
      if (spi(0x0030, 0, rect, 0) == 0) {
        return const Rect.fromLTWH(0, 0, 1280, 720);
      }
      return Rect.fromLTWH(
        rect.ref.left.toDouble(),
        rect.ref.top.toDouble(),
        (rect.ref.right - rect.ref.left).toDouble(),
        (rect.ref.bottom - rect.ref.top).toDouble(),
      );
    } finally {
      calloc.free(rect);
    }
  }

  static int findWindow(String title) {
    final text = title.toNativeUtf16();
    try {
      final find = _user32.lookupFunction<_FindWindowNative, _FindWindowDart>(
          'FindWindowW');
      return find(0, text);
    } finally {
      calloc.free(text);
    }
  }

  /// Shows a panel without activating it. HUD passes [noActivate] so it can
  /// never steal focus from the target application mid-dictation.
  static Future<void> showInactive({bool noActivate = false}) async {
    try {
      await _channel
          .invokeMethod<void>('window.showInactive', noActivate);
    } catch (_) {
      // Window operations must never break the UI.
    }
  }

  /// Shows the HUD on the monitor that owns the foreground app, anchored to
  /// the bottom center of that monitor's work area.
  static Future<void> showHud() async {
    try {
      await _channel.invokeMethod<void>('window.showHud');
    } catch (_) {
      // Window operations must never break the UI.
    }
  }

  static Future<void> hide() async {
    try {
      await _channel.invokeMethod<void>('window.hide');
    } catch (_) {
      // Window operations must never break the UI.
    }
  }

  /// Clips a frameless window to a rounded rectangle. Flutter on Windows does
  /// not expose per-pixel transparency cheaply; a window region gives clean
  /// rounded corners without a compositor workaround.
  static bool applyRoundedRegion(String title, double radius) {
    final hwnd = findWindow(title);
    if (hwnd == 0) return false;
    final rect = calloc<_Rect>();
    try {
      final getRect = _user32
          .lookupFunction<_GetWindowRectNative, _GetWindowRectDart>(
              'GetWindowRect');
      if (getRect(hwnd, rect) == 0) return false;
      final width = rect.ref.right - rect.ref.left;
      final height = rect.ref.bottom - rect.ref.top;
      if (width <= 0 || height <= 0) return false;
      final createRgn = _gdi32.lookupFunction<_CreateRoundRectRgnNative,
          _CreateRoundRectRgnDart>('CreateRoundRectRgn');
      final setRgn = _user32.lookupFunction<_SetWindowRgnNative,
          _SetWindowRgnDart>('SetWindowRgn');
      final diameter = (radius * 2).round();
      final region = createRgn(0, 0, width + 1, height + 1, diameter, diameter);
      if (region == 0) return false;
      return setRgn(hwnd, region, 1) != 0;
    } finally {
      calloc.free(rect);
    }
  }
}
