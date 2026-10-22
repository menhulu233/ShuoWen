# ShuoWen UI

Flutter Windows renderer for ShuoWen (说文). It never performs dictation itself:
the C++ engine owns the microphone, VAD, ASR, hotkey and text insertion, while
this process renders the settings, history, HUD, OnCall and tray panel.

The runner embeds a native named-pipe client (`windows/runner/shuowen_ipc.cpp`)
and forwards NDJSON events to Dart through an EventChannel.

```powershell
flutter pub get
flutter build windows --release
flutter analyze
flutter test
```

The engine must be running for live data; `dist\shuowen_engine.exe` launches the
matching renderer processes on demand.
