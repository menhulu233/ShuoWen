# ShuoWen 架构设计 — WeType 式双进程语音输入法

- **日期**: 2026-10-22
- **状态**: 已定稿，进入实现
- **范围**: 当前仓库 `D:\workspace\ShuoWen`（Windows 首发）
- **参考实现**: `D:\workspace\X-ASR\projects\Vibe_XASR`（C#/WinForms + Swift 两套单进程实现）
- **架构范本**: 微信输入法 WeType Windows 3.0（TSF 薄壳 + 引擎服务进程 + Flutter 渲染进程）

---

## 1. 结论

Vibe_XASR 的能力可以完整迁移到 WeType 的分层方式，但**只学“进程分家 + UI 壳化”，不学它的全家桶**：

| WeType 做法 | ShuoWen 对应 | 是否采用 |
|---|---|---|
| 系统目录薄壳 + 版本化核心 | 单一安装目录，不注入系统目录 | 否（不是 TSF IME） |
| 引擎服务进程（C++，命名管道） | `shuowen_engine.exe`，命名管道 JSON-RPC | **是** |
| Flutter 渲染进程 | `shuowen_ui.exe`，多窗口按需启动 | **是** |
| TSF COM TIP 注入 | 全局热键 + `SendInput(KEYEVENTF_UNICODE)` | 否（听写型产品，不需要 IME 候选） |
| 5 进程 + LocalSystem 服务 + gRPC 证书 | 2 类进程 + 按窗口启动的 UI 进程 | 否（规模不匹配） |
| 命名管道 + protobuf | 命名管道 + 换行分隔 JSON | 是（简化） |

**技术选型：Flutter + C++20。** 不使用 Nim 包一层 C++ 库：引擎要直接操作 WASAPI COM、`WH_KEYBOARD_LL`、`SendInput`、`Shell_NotifyIcon` 和 sherpa-onnx C API，C++ 是零胶水层的最短路径；Nim 只会在中间再加一层 FFI 和运行时，没有收益。Flutter 只负责画面，不参与打字热路径。

## 2. 目标与非目标

### 2.1 v1 目标（功能对齐 Vibe_XASR 的 Electron v1 规格）

- 常驻托盘、无主窗口；右键菜单切换模式、打开面板/历史/设置、静音、退出。
- 全局热键**按住说话**、松手上屏；支持 Hold / Latch / Toggle 三种触发方式。
- 本地流式识别：sherpa-onnx streaming zipformer2 transducer，16 kHz mono，greedy。
- 三种模式：`Paste`（松手一次性插入）、`Type`（逐字流式 + 退格差分）、`OnCall`（常开 VAD 分段 + 浮层）。
- VAD：优先 sherpa-onnx Silero；模型缺失时回退到能量 VAD，保证开箱可用。
- 300 ms preroll、150 ms partial 节流、长按逐字分段（4 s 后按 VAD 断句）、松手强制 flush。
- 模型四档 160 / 480 / 960 / 1920 ms，默认 960 ms；双根路径（可写 `%APPDATA%` + 安装目录内置）。
- 模型下载：官方 CDN 归档 / ModelScope / HuggingFace 三源，进度事件。
- 设置与历史 JSON；历史按日期、搜索、编辑、删除、导出、清空；累计字数。
- 四语言 UI：中 / 英 / 日 / 韩，Auto 跟随系统。
- HUD、OnCall 浮层、托盘弹出面板、首次启动引导。
- 提示音、简繁转换、剪贴板保留、开机自启。
- Flutter UI 可关闭后重开；引擎不退出，听写不中断。

### 2.2 v1 非目标

- TSF/IME 候选词注入（产品是听写，不是拼音输入法）。
- 云端 ASR、云端 LLM 润色、本地 GGUF 润色。
- 会议录制、AI 摘要。
- 热词/同音字纠正/口令片段/ITN/去口头禅（引擎已留后处理管线接口）。
- macOS 安装包（架构预留传输层与窗口层抽象，v1 只构建 Windows）。
- 自动更新。

## 3. 进程与线程模型

```
┌────────────────────────── shuowen_engine.exe (C++20) ──────────────────────────┐
│ 主线程: Win32 消息循环                                                         │
│   ├─ WH_KEYBOARD_LL 全局热键 → 状态机 Hold/Latch/Toggle                        │
│   ├─ Shell_NotifyIcon 托盘 + 菜单                                              │
│   └─ 启动/聚焦 UI 子进程                                                       │
│                                                                                │
│ IPC 线程池: \\.\pipe\shuowen.engine（多实例，每客户端一线程）                   │
│ 音频线程: WASAPI shared 16 kHz mono float → FrameQueue                         │
│ 推理线程: preroll → VAD → sherpa OnlineStream → partial/final                   │
│ 下载线程: WinHTTP 模型下载 → 进度事件                                           │
│ 持久化: settings.json / history.json（原子写、防抖 flush）                      │
└───────────────┬───────────────────────────────────────────────┬────────────────┘
                │ 命名管道（NDJSON）                             │ CreateProcess
      ┌─────────▼─────────┐   ┌──────────────┐   ┌──────────────▼──────────────┐
      │ shuowen_ui.exe    │   │ shuowen_ui   │   │ shuowen_ui.exe              │
      │ --window=settings │   │ --window=hud │   │ --window=oncall / popup     │
      └───────────────────┘   └──────────────┘   └─────────────────────────────┘
```

**为什么 UI 是多个进程而不是一个 Flutter 多窗口进程**：Flutter Windows 的多窗口需要额外插件或自建窗口管理；按窗口启动进程是 WeType 同款隔离思路，实现简单、崩溃互不影响、HUD 常驻时设置窗口可随时退出。每个 UI 进程只连一根管道，窗口类型由命令行决定。

**热路径**：按键 → 钩子 → 引擎队列 → WASAPI → ASR → `SendInput`，全程 C++，Flutter 不参与。

## 4. IPC 协议

- 传输：Windows 命名管道 `\\.\pipe\shuowen.engine`，`PIPE_UNLIMITED_INSTANCES`，消息模式为字节流。
- 编码：UTF-8，**一行一个 JSON**（NDJSON），行内不允许裸换行。
- 请求：`{"id":1,"cmd":"settings.get","args":{...}}`
- 响应：`{"id":1,"ok":true,"data":{...}}` 或 `{"id":1,"ok":false,"error":{"code":"...","message":"..."}}`
- 事件：`{"event":"partial","data":{...}}`，无 `id`，广播给所有客户端。
- 连接后客户端先发 `hello`，引擎回 `hello` 快照（版本、设置、状态、模型、历史摘要）。
- 断线重连：UI 侧每秒重试一次，引擎不保存会话状态。

### 4.1 命令表

| 命令 | 作用 |
|---|---|
| `hello` | 握手 + 全量快照 |
| `settings.get` / `settings.patch` | 读取 / 局部更新设置（写回保留未知字段） |
| `history.list` / `history.update` / `history.delete` / `history.clear` / `history.export` | 历史 |
| `models.status` / `models.download` / `models.cancel` / `models.delete` / `models.use` | 模型分档 |
| `engine.status` / `engine.reload` | 引擎状态 / 重建识别器 |
| `dictation.start` / `dictation.stop` / `dictation.toggle` | 手动控制 |
| `dictation.copy` / `dictation.clear` | OnCall 文本 |
| `mic.devices` / `mic.set` / `mic.mute` | 麦克风 |
| `app.open` / `app.quit` | 打开某窗口 / 退出 |
| `ui.ready` | UI 进程注册窗口类型 |

### 4.2 事件表

| 事件 | 载荷 |
|---|---|
| `state` | 引擎状态、模式、听写状态、麦克风、模型档位、热键 |
| `partial` | 当前增量文本 + 电平 |
| `final` | 最终文本 + 模式 + 是否入历史 |
| `level` | 0..1 麦克风电平（HUD 波形） |
| `models` | 各档位状态 / 进度 |
| `settings` | 更新后的全量设置 |
| `history` | 历史变更原因 |
| `toast` | `{kind, message}` |
| `hud` | `{visible, state, text, level, mode}` |
| `oncall` | `{segments:[...], paused}` |
| `focus` | `{window}` 让已存在的同类窗口置前 |
| `shutdown` | 引擎退出，UI 应关闭 |

## 5. 引擎模块

```
engine/
├─ CMakeLists.txt
├─ include/shuowen/...
├─ src/
│  ├─ main.cpp                 入口、单实例、命令行（--selftest）
│  ├─ app.{h,cpp}              生命周期编排
│  ├─ config.h                 路径、常量、版本
│  ├─ util/json.{h,cpp}        最小 JSON（对象/数组/字符串/数字/布尔/null）
│  ├─ util/log.{h,cpp}         追加日志 + 轮转
│  ├─ util/text.{h,cpp}        UTF-8/16、CJK 去空格、标点、码点差分、简繁
│  ├─ ipc/pipe_server.{h,cpp}  多客户端 NDJSON 服务
│  ├─ ipc/protocol.{h,cpp}     请求分发、事件广播
│  ├─ store/settings.{h,cpp}   默认值、读写、迁移、未知字段保留
│  ├─ store/history.{h,cpp}    条目、累计字数、防抖落盘
│  ├─ audio/wasapi_capture.*   WASAPI 共享模式 + AUTOCONVERTPCM 到 16k mono
│  ├─ audio/frame_queue.*      有界队列，丢最旧
│  ├─ asr/sherpa_api.*         LoadLibrary 动态绑定 C API（含版本隔离）
│  ├─ asr/sherpa_engine.*      OnlineRecognizer + Silero VAD / 能量 VAD
│  ├─ asr/dictation.*          三模式、preroll、节流、端点、后处理
│  ├─ input/hotkey.*           WH_KEYBOARD_LL + 组合键匹配
│  ├─ input/inserter.*         SendInput Unicode / Backspace / Ctrl+V
│  ├─ input/cue.*              内存 WAV + PlaySound
│  ├─ models/paths.*           双根解析、档位文件名
│  ├─ models/downloader.*      WinHTTP 下载 + tar.exe 解包
│  ├─ ui/tray.*                Shell_NotifyIcon + 菜单
│  ├─ ui/launcher.*            CreateProcess 启动/聚焦 UI
│  └─ selftest.cpp             JSON、文本、存储、路径、差分的无麦自测
└─ third_party/sherpa/c-api.h  随仓库提供（Apache-2.0，注明来源）
```

- sherpa 通过 `LoadLibraryW("sherpa-onnx-c-api.dll")` + `GetProcAddress` 绑定，不产生导入库；`onnxruntime.dll` 与 sherpa DLL 由构建脚本放到引擎同目录。
- 模型加载在独立线程，状态 `loading → ready / error`；加载期间按热键只提示“模型加载中”。
- 引擎进程崩溃不会带崩 UI；UI 会显示“引擎未连接”并自动重试。

## 6. UI 结构

```
ui/
├─ pubspec.yaml
├─ lib/
│  ├─ main.dart                --window= 分发
│  ├─ app.dart                 MaterialApp / 主题 / 语言
│  ├─ core/
│  │  ├─ ipc/plugin_client.dart  MethodChannel / EventChannel 封装（C++ 管道客户端）
│  │  ├─ engine_client.dart    ChangeNotifier：连接、快照、请求、事件
│  │  ├─ theme.dart            设计 token（#7C5CFF → #38E1D6，深色优先）
│  │  ├─ l10n.dart             中/英/日/韩
│  │  ├─ win32_window.dart     圆角窗口区域、定位、点击穿透辅助
│  │  └─ format.dart           日期/字数/进度
│  ├─ windows/
│  │  ├─ settings_window.dart  6 tab 侧栏
│  │  ├─ history_window.dart   统计 + 搜索 + 列表 + 编辑
│  │  ├─ popup_window.dart     托盘弹出
│  │  ├─ hud_window.dart       置顶/点击穿透/圆角悬浮条
│  │  ├─ oncall_window.dart    常开面板
│  │  └─ onboarding.dart       首次启动
│  └─ widgets/                 Card / Toggle / Segmented / Meter / TitleBar ...
└─ windows/                    Flutter Windows runner
   └─ runner/shuowen_ipc.cpp   原生命名管道客户端；读线程 PostMessage 回窗口线程
```

- **窗口形态**：Settings/History 为 980×720 可调窗口；Popup 320×440 无边框；HUD 480×96 无边框置顶点击穿透；OnCall 460×360 无边框置顶。
- **圆角**：用 Win32 `CreateRoundRectRgn + SetWindowRgn`（Dart FFI）裁剪，避免 Flutter Windows 透明窗口的兼容问题；HUD/OnCall 使用纯色圆角卡片而非逐像素透明。
- **点击穿透**：`window_manager` 的 `setIgnoreMouseEvents(true)`。
- **Flutter 依赖**：`window_manager` + `ffi`。命名管道走 runner 内的 C++ 客户端（EventChannel 上行），窗口区域/系统信息用 `dart:ffi` 直连 Win32。

## 7. 数据与目录

```
%APPDATA%\ShuoWen\
├─ settings.json
├─ history.json
├─ models\                     可写模型根（下载目标）
│  ├─ chunk-960ms-model\{encoder,decoder,joiner}-960ms.onnx + tokens.txt
│  └─ silero_vad.onnx
├─ logs\engine.log
└─ runtime\ui.lock.*           窗口单实例互斥（可选）
安装目录\
├─ shuowen_engine.exe
├─ shuowen_ui.exe
├─ sherpa-onnx-c-api.dll
├─ onnxruntime.dll
├─ assets\shuowen.ico
└─ models\                     内置默认 960ms 档（安装包提供）
```

- 模型读取顺序：可写根优先，安装目录兜底；下载永远写可写根。
- 写文件：`*.tmp` → `MoveFileEx(REPLACE_EXISTING)`；历史 2 s 防抖。
- 设置 schema 使用 PascalCase 字段以贴近 Vibe_XASR，便于用户迁移；未知字段读入后原样保留。

## 8. 关键行为移植

| 行为 | 规则 |
|---|---|
| 采集 | WASAPI 共享模式，请求 16 kHz mono float；`AUTOCONVERTPCM` 由系统重采样；30 ms 缓冲 |
| preroll | 环形保留 300 ms，语音起始时先灌入 ASR |
| PTT | 按住期间整段音频都送 ASR，不因 VAD 静音吞字；松开后 1.5 s 零填充 + `InputFinished` |
| OnCall | VAD 检测起始/结束；结束且存在完整分段时 finalize |
| 逐字长按 | 超过 4 s 后，VAD 静音且分段完成时句中提交，避免整段积压 |
| partial 节流 | 150 ms 查询一次；文本不变不广播 |
| CJK | 相邻 CJK/CJK 标点之间的空格删除；ASCII 标点前空格删除 |
| 结尾标点 | final 缺少标点时补 `。` 或 `.` |
| Type 差分 | 按 Unicode 码点求公共前缀，退格数 = 旧串剩余码点数，再插入新串剩余部分 |
| 插入 | 先释放 Ctrl/Alt/Shift/Win，再逐 UTF-16 code unit 发 `KEYEVENTF_UNICODE` |
| 热键 | 排除触发器自身贡献的修饰位；忽略注入事件；可吞掉可打印触发键 |
| 静音 | 丢弃帧，电平归零 |
| 繁体 | `LCMapStringEx(LCMAP_TRADITIONAL_CHINESE)` 在 final 后处理 |

## 9. 构建与打包

```powershell
# 依赖（本机已有；也可从 Vibe_XASR 的还原目录取）
#   engine\third_party\sherpa\c-api.h   （已随仓库）
#   sherpa-onnx-c-api.dll / onnxruntime.dll
#   models\chunk-960ms-model\* / silero_vad.onnx

cmd /c "scripts\_build_engine.bat"
cd ui; flutter build windows --release; cd ..
powershell -ExecutionPolicy Bypass -File scripts\package.ps1

# 产物
dist\
├─ shuowen_engine.exe
├─ shuowen_ui.exe + Flutter DLL/数据
├─ sherpa-onnx-c-api.dll / onnxruntime.dll
└─ models\chunk-960ms-model\*（可选）
```

- 引擎：CMake + MSVC x64（`/std:c++20 /permissive- /EHsc /utf-8`）。
- UI：`flutter build windows --release`。
- 安装：`installer/install.ps1`（per-user，无需管理员）；打包时会复制到 `dist\installer`。

## 10. 测试

| 层 | 方式 | 覆盖 |
|---|---|---|
| 引擎 | `shuowen_selftest.exe` | JSON 往返、CJK 去空格/标点、码点差分、设置/历史原子写、历史上限、路径解析、VAD 体积校验 |
| 引擎 | 手动 + 日志 | 麦克风采集、热键、三模式、模型分档、断线重连 |
| UI | `flutter analyze` + `flutter test` | L10n 完整性、格式化、协议编解码 |
| 集成 | 人工验收 | 记事本/浏览器上屏、逐字差分、OnCall 分段、设置/历史跨重启 |

## 11. 风险与缓解

| 风险 | 缓解 |
|---|---|
| sherpa C API 与预编译 DLL 版本漂移 | 只用已验证导出的函数；配置结构体零初始化、只填公共前缀；集中在 `sherpa_api` |
| 无 VAD 模型 | 能量 VAD 回退；OnCall 仍可用，提示下载 Silero |
| Flutter 透明窗口在 Windows 上的兼容性 | 不依赖逐像素透明，使用圆角窗口区域 + 纯色卡片 |
| 命名管道在 Dart 中没有稳定的异步 API | runner 内嵌 C++ 客户端，管道线程 + PostMessage + EventChannel；不使用 Dart isolate/FFI |
| 全局钩子被杀软误报 | 代码签名（发布时）；文档说明；不做键盘记录，只匹配触发键 |
| UIPI：无法向提权窗口注入 | 与 C# 版同限制；文档说明“目标需非提权” |
| 模型体积大 | 默认内置 960 ms；其余按需下载；支持三源 |

## 12. 从 Vibe_XASR 的映射

| Vibe_XASR | ShuoWen |
|---|---|
| `TrayApp.cs`（1.6k 行，引擎+UI+托盘） | 拆成 `app.cpp` + `tray.cpp` + `launcher.cpp` |
| `DictationEngine.cs` | `asr/dictation.cpp`（逻辑一比一） |
| `StreamingAsr.cs` | `asr/sherpa_engine.cpp` |
| `Vad.cs` | `asr/sherpa_engine.cpp`（Silero + 能量回退） |
| `MicCapture.cs` | `audio/wasapi_capture.cpp` |
| `GlobalHotkey.cs` | `input/hotkey.cpp` |
| `TextInserter.cs` | `input/inserter.cpp` |
| `Settings.cs` / `HistoryStore.cs` | `store/settings.cpp` / `store/history.cpp` |
| `SettingsForm.cs` 等 WinForms/WPF | `ui/lib/windows/*.dart` |
| `OverlayForm.cs` | `hud_window.dart` + `oncall_window.dart` |
| `TrayPopupForm.cs` | `popup_window.dart` |
| `L10n.cs` / `Theme.cs` | `l10n.dart` / `theme.dart` |

## 13. 里程碑

| 里程碑 | 判据 |
|---|---|
| M0 骨架 | 引擎 selftest 通过；Flutter 空窗口能连上管道 |
| M1 听写闭环 | 按住热键说话 → 记事本出字（Paste） |
| M2 三模式 | Type 差分、OnCall 分段、preroll、提示音 |
| M3 管理面 | 设置/历史/模型下载/托盘/引导 |
| M4 打包 | `dist\` 绿色包可运行；NSIS 可选 |

## 14. 实现中确认的坑（2026-10-22）

1. **命名管道同步读写死锁**：服务端用阻塞 `ReadFile` 等请求时，writer 线程的
   `WriteFile` 会排在同一个同步句柄后面，响应要等客户端断开才返回。`PipeServer`
   已改为 `PeekNamedPipe` 轮询，无数据时休眠 10 ms。
2. **Flutter 事件线程**：管道读线程不能直接调用 `EventSink::Success`；
   `PostPlatformThreadTask` 在被引擎拉起的 renderer 里会 abort。现在用
   `PostMessageW(WM_APP+0x51)` 把行交给窗口线程，sink 未注册时先入 pending 队列。
3. **HUD 抢焦点**：`window_manager 0.5.2` 的 `show(inactive: true)` 仍然调用
   `SetForegroundWindow`，导致最终文本被 SendInput 到 HUD 而不是目标应用。
   HUD 现在从窗口创建起带 `WS_EX_NOACTIVATE`，显示走原生
   `ShowWindow(SW_SHOWNOACTIVATE)`；设置/历史仍用正常激活路径。
4. **Silero VAD 版本**：sherpa-onnx 1.13.x 只支持 V4 图。官方 release 的未版本化
   `silero_vad.onnx` 是 V5（约 643 KB），加载会报
   `Unsupported silero vad model` 并退出；应使用 `silero_vad_v4.onnx`（约 1.8 MB）。
   引擎在调用 C API 前按体积拒绝已知不兼容文件，回退能量 VAD。
5. **窗口插件作用对象**：必须先 `SetChildContent` 再 `RegisterPlugins`，否则
   window_manager 的 `GetAncestor(view, GA_ROOT)` 会作用到 FLUTTERVIEW 子窗口。
6. **HUD 多显示器**：`SPI_GETWORKAREA` 只返回主屏。HUD 显示时改用
   `GetForegroundWindow` + `MonitorFromWindow` + `GetMonitorInfo`，定位到目标应用所在屏。
7. **VAD 只校验存在不够**：残留的 V5 文件会让下载器误判“已安装”。现在按
   `kMinSileroVadBytes` 判断可用性；下载始终写可写模型根，损坏的 bundled 副本不会挡住修复。
8. **面板进程常驻**：设置/历史关闭即退出 renderer；托盘面板失焦后延迟退出；HUD/OnCall 常驻。
9. **PowerShell 5.1 编码**：含中文的 `.ps1` 必须保存为 UTF-8 with BOM，否则
   `powershell.exe` 会按 ANSI 解码并产生语法错误。
