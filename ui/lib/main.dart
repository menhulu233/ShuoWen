import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import 'app.dart';
import 'core/engine_client.dart';
import 'core/theme.dart';
import 'core/win32_window.dart';

const String hudWindowTitle = 'ShuoWen HUD';
const String oncallWindowTitle = 'ShuoWen OnCall';
const String popupWindowTitle = 'ShuoWen Popup';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();
  final windowType = _parseWindowType(args);
  FlutterError.onError = (details) {
    _log('FlutterError: ${details.exceptionAsString()}\n${details.stack}');
  };
  await windowManager.ensureInitialized();
  var windowId = await windowManager.getId();
  for (var attempt = 0; attempt < 50 && windowId == 0; attempt++) {
    await Future<void>.delayed(const Duration(milliseconds: 100));
    await windowManager.ensureInitialized();
    windowId = await windowManager.getId();
  }

  final client = EngineClient(windowType);
  unawaited(client.start());
  runApp(ShuoWenApp(client: client, windowType: windowType));
  unawaited(_configureWindow(windowType));
}

void _log(String message) {
  try {
    final file = File('${Directory.systemTemp.path}\\shuowen_ui_$pid.log');
    file.writeAsStringSync(
      '${DateTime.now().toIso8601String()} $message\n',
      mode: FileMode.append,
    );
  } catch (_) {
    // Diagnostics must never break the UI.
  }
}

String _parseWindowType(List<String> args) {
  for (final arg in args) {
    if (arg.startsWith('--window=')) {
      return arg.substring('--window='.length);
    }
  }
  return Platform.environment['SHUOWEN_WINDOW'] ?? 'settings';
}

Future<void> _configureWindow(String windowType) async {
  final work = Win32Window.workArea();
  // Give the embedder a moment to finish attaching the view. When the engine
  // spawns the renderer, window operations issued before the first frame can
  // deadlock against Flutter's resize handling.
  await Future<void>.delayed(const Duration(milliseconds: 800));
  final isPanel = windowType == 'hud' ||
      windowType == 'oncall' ||
      windowType == 'popup';

  final options = _optionsFor(windowType);
  // NOTE: window_manager's setTitleBarStyle can block when the process was
  // launched by the engine before the first frame. The Windows runner is
  // borderless already, so the title bar style does not need a second call.
  if (options.size != null) {
    await _step('size', () => windowManager.setSize(options.size!));
  }
  if (options.minimumSize != null) {
    await _step('minSize', () => windowManager.setMinimumSize(options.minimumSize!));
  }
  if (options.alwaysOnTop == true) {
    await _step('alwaysOnTop', () => windowManager.setAlwaysOnTop(true));
  }
  if (options.backgroundColor != null) {
    await _step('background',
        () => windowManager.setBackgroundColor(options.backgroundColor!));
  }
  if (options.title != null) {
    await _step('title', () => windowManager.setTitle(options.title!));
  }
  await _step(
    'show',
    () async {
      if (windowType == 'hud') {
        // The HUD stays hidden until the engine reports an active dictation.
        // Showing it here would flash an empty pill at every launch.
        return;
      }
      if (isPanel) {
        // Panels are topmost popups; the native path avoids activating the
        // HUD while keeping OnCall/popup clickable after they appear.
        await Win32Window.showInactive();
      } else {
        await windowManager.show();
      }
    },
  );
  await _step('preventClose', () => windowManager.setPreventClose(true));

  if (!isPanel) {
    await _step('center', () => windowManager.center());
    await _step('focus', () => windowManager.focus());
    return;
  }

  await _step('alwaysOnTop2', () => windowManager.setAlwaysOnTop(true));
  await _step('resizable', () => windowManager.setResizable(false));
  if (windowType == 'popup') {
    await _step('focusPopup', () => windowManager.focus());
  }

  if (windowType == 'hud') {
    await _step('ignoreMouse', () => windowManager.setIgnoreMouseEvents(true));
    // ShowHud() positions the window on the foreground monitor whenever it
    // becomes visible; positioning here would only target the primary screen.
    await Future<void>.delayed(const Duration(milliseconds: 450));
    Win32Window.applyRoundedRegion(hudWindowTitle, 26);
  } else if (windowType == 'oncall') {
    await _step(
      'positionOnCall',
      () => windowManager.setPosition(Offset(
        work.right - 480 - 24,
        work.bottom - 400 - 24,
      )),
    );
    await Future<void>.delayed(const Duration(milliseconds: 450));
    Win32Window.applyRoundedRegion(oncallWindowTitle, 20);
  } else if (windowType == 'popup') {
    await _step(
      'positionPopup',
      () => windowManager.setPosition(Offset(
        work.right - 340 - 14,
        work.bottom - 470 - 14,
      )),
    );
    await Future<void>.delayed(const Duration(milliseconds: 450));
    Win32Window.applyRoundedRegion(popupWindowTitle, 16);
  }
}

Future<void> _step(String name, Future<void> Function() action) async {
  try {
    await action().timeout(const Duration(seconds: 4));
  } catch (error) {
    _log('step $name failed: $error');
  }
}

WindowOptions _optionsFor(String windowType) {
  switch (windowType) {
    case 'hud':
      return WindowOptions(
        size: const Size(500, 104),
        title: hudWindowTitle,
        backgroundColor: SwColors.surface,
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        windowButtonVisibility: false,
        alwaysOnTop: true,
      );
    case 'oncall':
      return WindowOptions(
        size: const Size(480, 400),
        title: oncallWindowTitle,
        backgroundColor: SwColors.surface,
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        windowButtonVisibility: false,
        alwaysOnTop: true,
      );
    case 'popup':
      return WindowOptions(
        size: const Size(340, 470),
        title: popupWindowTitle,
        backgroundColor: SwColors.surface,
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        windowButtonVisibility: false,
        alwaysOnTop: true,
      );
    case 'history':
      return WindowOptions(
        size: const Size(1080, 760),
        minimumSize: const Size(900, 620),
        title: '说文 — 历史',
        backgroundColor: SwColors.bg,
        titleBarStyle: TitleBarStyle.hidden,
        windowButtonVisibility: false,
      );
    default:
      return WindowOptions(
        size: const Size(1080, 760),
        minimumSize: const Size(900, 620),
        title: '说文 — 设置',
        backgroundColor: SwColors.bg,
        titleBarStyle: TitleBarStyle.hidden,
        windowButtonVisibility: false,
      );
  }
}
