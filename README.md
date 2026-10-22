# 说文 ShuoWen

一个 100% 本地的 Windows 语音输入法：按住热键说话，识别结果直接落到光标处。

架构参考微信输入法 WeType 的「薄 UI + 独立引擎」思路，但只保留适合听写产品的最小组合：

```
┌──────────────────────────── shuowen_engine.exe (C++20) ────────────────────────────┐
│ WH_KEYBOARD_LL 热键 · WASAPI 16k 采集 · VAD · sherpa-onnx 流式 ASR · SendInput 上屏 │
│ Shell_NotifyIcon 托盘 · 命名管道 \\.\pipe\shuowen.engine (NDJSON) · JSON 持久化      │
└───────────────┬─────────────────────────────────────────────────────────────────────┘
                │ 命名管道
      ┌─────────▼──────────┐   ┌──────────────────┐   ┌──────────────────────────┐
      │ shuowen_ui.exe     │   │ shuowen_ui.exe   │   │ shuowen_ui.exe           │
      │ --window=settings  │   │ --window=hud     │   │ --window=oncall/popup    │
      └────────────────────┘   └──────────────────┘   └──────────────────────────┘
```

- 打字热路径全程在 C++ 引擎里，Flutter 只负责设置、历史、HUD、OnCall 和托盘面板。
- UI 按窗口启动独立进程，单个窗口崩溃不会带走引擎；窗口关闭后引擎继续听写。
- 不注入 TSF/COM，不做候选词 IME；这是听写工具，不是拼音输入法。

设计细节见 `docs/superpowers/specs/2026-10-22-shuowen-architecture-design.md`。

## 当前状态

- 引擎：热键、WASAPI、VAD、sherpa-onnx 流式识别、三种听写模式、SendInput、托盘、命名管道、设置/历史、模型下载均已实现。
- UI：设置、历史、HUD、OnCall、托盘弹出、首次引导、四语言（中/英/日/韩）。
- 已验证：`shuowen_selftest.exe` 全绿、`flutter analyze` 无问题、右 Ctrl 长按可识别并上屏、partial/final/level 事件、历史追加、剪贴板回写、Silero VAD 加载。
- HUD 通过原生 `ShowWindow(SW_SHOWNOACTIVATE)` 显示，不会抢走目标应用的焦点；每次显示都会定位到前台应用所在显示器。
- 设置/历史/托盘面板关闭后退出对应渲染进程，按需再启动；HUD 与 OnCall 保持常驻。

## 构建

环境要求：

- Windows 10/11 x64
- Visual Studio 2022（Desktop development with C++、CMake、Ninja）
- Flutter stable（当前验证版本 Dart 3.13 / Flutter 3.x）
- sherpa-onnx Windows x64 运行库：`sherpa-onnx-c-api.dll`、`onnxruntime.dll`

构建引擎：

```powershell
cmd /c "scripts\_build_engine.bat"
```

脚本里的 VS/CMake 路径是本机路径；换机器时改成对应安装位置，或把 `cmake`、`ninja` 放进 `PATH`。

构建 UI：

```powershell
cd ui
flutter build windows --release
```

打包绿色目录（会构建引擎和 UI，并把运行库、默认模型、VAD 一起放进 `dist\`）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

`dist\installer\` 会随包生成，接收方可以直接运行其中的 `install.ps1`。

如果模型放在别处：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 `
  -ModelsRoot "D:\path\to\X-ASR-zh-en\deployment\models" `
  -SherpaDir "D:\path\to\runtimes\win-x64\native"
```

## 运行

```powershell
Start-Process .\dist\shuowen_engine.exe -WorkingDirectory .\dist
```

- 默认热键：**按住右 Ctrl** 说话，松手上屏。
- 托盘图标：打开面板、设置、历史、切换 Paste/Type/OnCall、静音、暂停、退出。
- 设置里可以换热键、麦克风、模型档位、触发方式（Hold/Latch/Toggle）和提示音。
- 首次启动（`Welcomed=false`）会自动打开设置页的引导。

## 安装与卸载

先打绿色包，再按当前用户安装（不需要管理员权限）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
powershell -ExecutionPolicy Bypass -File installer\install.ps1
```

- 安装到 `%LOCALAPPDATA%\Programs\ShuoWen`
- 创建开始菜单快捷方式，默认写入 `HKCU\...\Run` 开机自启
- `-NoAutostart` 关闭自启并持久化 `LaunchAtLogin=false`，`-DesktopShortcut` 额外创建桌面快捷方式，`-NoLaunch` 只安装不启动
- 注册到 Windows“应用和功能”，可从系统设置直接卸载
- 用户数据始终保存在 `%APPDATA%\ShuoWen`，升级和卸载都不会删除

卸载：

```powershell
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
```

## 数据与模型

用户数据目录：`%APPDATA%\ShuoWen\`

```
settings.json          设置（保留未知字段）
history.json           历史记录与累计字数（最多保留 10000 条，累计字数不因裁剪回退）
logs\engine.log        引擎日志
models\                可写模型根（优先于安装目录内置模型）
```

模型查找顺序：`%APPDATA%\ShuoWen\models` → `<exe 目录>\models`。

默认内置：

- `models\chunk-960ms-model\`：`encoder-960ms.onnx`、`decoder-960ms.onnx`、`joiner-960ms.onnx`、`tokens.txt`
- `models\silero_vad.onnx`：Silero VAD **V4**（1,807,522 字节）

注意：sherpa-onnx 1.13.x 只支持 V4 图。官方 release 里未带版本号的 `silero_vad.onnx` 是 V5，加载时会报 `Unsupported silero vad model` 并退出；下载器已改为 `silero_vad_v4.onnx`。缺失 VAD 时引擎会自动退回能量 VAD，仍可听写。

ASR 模型四档（160/480/960/1920 ms）可通过设置页下载；VAD 也可单独下载。

## 验证

一键跑全量回归（引擎自测 + UI 分析与单测 + IPC 冒烟 + HUD 窗口回归）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1
```

- `-Only engine,ui,ipc,window` 只跑指定层，`-Build` 先执行 `scripts\package.ps1` 完整构建再测。
- 引擎自测包含两条**真实识别回放**：用 TTS 生成的语音素材（`engine\tests\fixtures`）驱动真实模型，断言识别文本；缺模型或素材时自动打印 `SKIP`，不影响其余用例。
- HUD 窗口回归会检查点击穿透样式与层属性（`WS_EX_LAYERED` + 不透明 alpha）、尺寸、圆角区域，并抓取窗口像素确认真的渲染出了界面——这一条锁住了「HUD 显示成黑窗口」的回归。

单独跑某一层：

```powershell
.\dist\shuowen_selftest.exe
cd ui; flutter analyze; flutter test
powershell -ExecutionPolicy Bypass -File .\scripts\_ipc_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\_hud_window_smoke.ps1
```

`scripts\_ipc_smoke.ps1` 会自动拉起 `dist\shuowen_engine.exe`、完成一次命名管道握手并取回引擎快照；引擎已在运行时则复用它，且不会替用户关闭引擎。

手动验收：

1. 启动引擎，托盘出现，日志出现 `hotkey: installed`、`asr: ready`、`mic: capture started`。
2. 在记事本/浏览器输入框按住右 Ctrl 说一句话，HUD 出现并显示实时文字。
3. 松手后文字落到光标处，HUD 显示「已插入」，历史里出现该条记录。
4. 打开设置/历史/托盘面板，确认窗口尺寸、置顶、圆角、主题正常。

## 已知限制 / 下一步

- 每个 UI 窗口是一个独立 Flutter 进程，单个进程约 150–260 MB；设置/历史/托盘面板关闭后会退出，HUD 与 OnCall 仍常驻。后续可合并为单进程多窗口。
- `SendInput` 对管理员权限（UIPI）窗口无效，这是 Windows 的限制。
- 没有 TSF/IME 候选词注入，也没有云端 ASR/润色；这是 v1 的非目标。
- 锁屏状态下无法做自动化上屏验证；HUD 的 no-activate 行为需要在解锁后的桌面上复测。
- 当前安装方式是绿色目录 + PowerShell 每用户安装脚本，未接 NSIS/签名/自动更新。

## 目录

```
engine\        C++20 引擎（CMake/Ninja，sherpa-onnx C API）
ui\            Flutter Windows 渲染层
scripts\       构建、打包、IPC 冒烟脚本
installer\     每用户安装与卸载脚本
docs\          架构设计
dist\          本地绿色包（不提交）
```
