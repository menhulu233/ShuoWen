# ShuoWen 测试与回归保障设计

- **日期**: 2026-10-22
- **状态**: 已批准，待实施
- **范围**: 本地一键测试体系（不含 CI）
- **前置**: `2026-10-22-shuowen-architecture-design.md` 的 13.2「覆盖缺口」

---

## 1. 背景与目标

v1 已交付，但架构文档明确记录了三处覆盖缺口：听写热路径没有自动化用例、UI 只有 5 个单测、模型下载未做端到端验证。此外 2026-10-22 修复的 HUD 黑窗口问题（`window_manager` 只设 `WS_EX_LAYERED` 不设层属性）暴露了原生窗口行为缺少回归锁。

本设计的目标：

1. 一条命令（`scripts\test.ps1`）验证引擎核心逻辑、听写状态机、真实识别链路、IPC 协议、HUD 窗口行为、UI 客户端状态机。
2. 测试失败时能定位到具体层与用例名。
3. 依赖模型/语音素材的用例在资源缺失时打印 `SKIP` 而不是失败。
4. 无新增第三方依赖，复用现有 `Check()` harness 与 PowerShell 脚本模式。

非目标：

- GitHub Actions / 任何 CI（用户决定以后再说）。
- Catch2 / GoogleTest 等测试框架。
- 虚拟声卡 + 注入热键的真端到端。
- 性能基准、代码覆盖率工具。
- 模型下载的端到端验证（依赖外网 CDN，另行手动验收）。

## 2. 测试分层

### L1 单元（无模型、无设备）

在现有 `engine/src/selftest_main.cpp`（44 项断言）基础上新增：

| 用例 | 断言 |
|---|---|
| `OwnModBit` | Left/Right Ctrl→1、Alt→2、Shift→4、Win→8、普通键→0 |
| `HotkeyMatches` | 绑定右 Ctrl 时，仅右 Ctrl 按下匹配；左 Ctrl 不匹配；Ctrl+Shift 不匹配绑定 Ctrl；自身修饰位从有效修饰中排除 |
| `CapturedMods` | 捕获右 Ctrl 组合时排除 Ctrl 位，捕获 Ctrl+Alt 时只保留 Alt 位 |

现有用例（JSON、CJK 去空格/标点、码点差分、Type 繁体输出、模型路径、设置未知字段保留、历史增删改清与上限、HotkeyName）保持通过。

### L2 组件（fake ASR，确定性）

新增 `AsrBackend` 抽象接口与测试用 `FakeAsr`，覆盖 `Dictation` 状态机：

| 用例 | 场景与断言 |
|---|---|
| Hold 基本流 | `BeginHold` → `PushFrame`×N → `EndHold`；partial 回调至少 1 次、final 恰好 1 次，final 文本经 `NormalizeCjk` + `EnsureFinalPunct` |
| partial 节流 | 150 ms 内连续推帧，partial 回调次数不超过节流窗口数；文本不变时不重复广播 |
| preroll | 语音开始前的 300 ms 帧在起始后被送入 backend（FakeAsr 记录 `Accept` 帧数） |
| Cancel | `BeginHold` → 推帧 → `Cancel`：无 final 回调，backend 流被重置 |
| Pause | `SetPaused(true)` 期间推帧：backend 收不到帧，level 回调为 0 |
| OnCall 分段 | 模拟 VAD 段结束两次：产生两次 final，文本不串联 |
| final 后重置 | 每次 final 后 FakeAsr 收到 `ResetStream`；下一轮 hold 的 partial 不继承上一轮文本 |

时间相关行为使用真实 `steady_clock`，用例总时长控制在 2 秒内，不引入虚拟时钟。Type 模式的 `PlanTypeOutput` 在 `App` 层（`app.cpp:756`），已由 L1 的 Type 输出用例覆盖，L2 不重复。

### L3 集成（真实模型 + WAV 回放）

`SherpaEngine::Load(960ms 模型, Silero/能量 VAD)` → `Dictation` → 按约实时节奏 `PushFrame` 推 WAV → `EndHold` → final。

| 用例 | 素材 | 断言 |
|---|---|---|
| 中文识别 | `zh_greeting.wav`（"今天天气不错"） | partial 非空；final 与期望文本的字符级编辑距离 ≤ max(2, 期望长度的 30%)；CJK 结尾补 `。` |
| 英文识别 | `en_hello.wav`（"hello world"） | final 含 `hello`（大小写无关）；ASCII 结尾补 `.` |

资源查找顺序（任一缺失 → 打印 `SKIP` 并注明缺失项，不计失败）：

- 模型目录：`SHUOWEN_MODELS_DIR` → `<selftest 所在目录>/models` → `<selftest 所在目录>/../../dist/models` → `<selftest 所在目录>/../../../dist/models`（覆盖 `dist` 与 `engine/build/bin` 两种运行位置）。
- 素材目录：`SHUOWEN_TEST_WAV_DIR` → `<selftest 所在目录>/tests/fixtures` → 源码树 `engine/tests/fixtures`（由 `test.ps1` 显式设置环境变量，源码树路径优先）。
- sherpa DLL：与 selftest 同目录存在 `sherpa-onnx-c-api.dll` 时才会尝试加载。

### L4 IPC 进程级（扩展 `scripts/_ipc_smoke.ps1`）

保留现有行为（能连则复用、否则自起自停；复用时不关闭用户引擎）。新增：

| 用例 | 断言 |
|---|---|
| 非法命令 | `{"cmd":"nope"}` → `ok=false` 且 `error.code` 非空 |
| 参数校验 | `settings.patch` 传错类型 → `ok=false` 且不改变设置 |
| settings 往返 | `settings.patch` 修改 `HudStaySeconds` → `settings.get` 读到新值 → 恢复原值 |
| 历史结构 | `history.list` 返回 `entries` 数组与 `lifetimeChars` 数值 |
| 广播事件 | `settings.patch` 后能读到 `{"event":"settings"}` 或 `state` 广播（跳过其他事件行） |

### L5 原生窗口回归（新增 `scripts/_hud_window_smoke.ps1`）

对引擎拉起的 HUD 窗口（标题 `ShuoWen HUD`）执行：

| 断言 | 方法 |
|---|---|
| 点击穿透样式 | `GWL_EXSTYLE` 含 `WS_EX_LAYERED (0x80000)` 与 `WS_EX_TRANSPARENT (0x20)` |
| 不抢焦点样式 | `GWL_EXSTYLE` 含 `WS_EX_NOACTIVATE (0x8000000)` |
| 层属性已设置（黑窗口回归锁） | `GetLayeredWindowAttributes` 成功且 `alpha=255`、`flags=LWA_ALPHA` |
| 尺寸 | `GetWindowRect` 为 500×104 逻辑像素（按 `GetDpiForWindow` 换算为物理像素，允许 ±2 px） |
| 圆角 | `GetWindowRgn` 返回非空 |
| 实际渲染 | 显示窗口后截图像素统计：品牌渐变（accentA/accentB ±40）像素 > 50 |

显示使用 `SetWindowPos(HWND_TOPMOST, SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)`，与引擎 `ShowHud()` 一致。生命周期遵循"能复用则复用"：已有引擎运行时直接测它拉起的 `ShuoWen HUD`，结束后只隐藏窗口、不杀进程；没有引擎时自起引擎，结束后结束引擎进程，并按窗口所属 PID 结束它拉起的 HUD 子进程。像素断言失败时把截图保存到 `%TEMP%\shuowen_hud_smoke.png` 便于排查。

### L6 UI 单元（`ui/test/`）

新增 `engine_client_test.dart`，用 fake `PipeClient` 注入：

| 用例 | 断言 |
|---|---|
| hello 快照 | `settings/engine/models/hud/oncall/recent/lifetimeChars` 字段就位，`connected=true` |
| 事件更新 | `partial` → `partial`/`level` 更新；`state` → `engine` 合并；`toast` → `toastSerial` 自增；`hud` → `hud` 替换 |
| 请求响应 | `send` 的 id 与响应匹配；`ok=false` 抛 `EngineException` |
| 断线 | 管道发出 `__shuowen_closed__` 后 `connected=false`、`connectionError` 非空；重连依赖真实计时器，不做断言，测试结束 `dispose()` 取消计时器 |

现有 5 个测试（L10n 回退、四语言 key、占位符、热键标签、字数估算）保持通过。

## 3. 代码改造

1. **`engine/src/asr/asr_backend.h`（新增）**
   抽出 `Dictation` 用到的接口：`ready/Accept/Partial/Finalize/ResetStream/VadAccept/VadSpeech/VadHasSegment/VadPopSegment/VadFlush/VadReset`。
   `SherpaEngine : public AsrBackend`；`Dictation` 构造函数改收 `AsrBackend&`。纯依赖倒置，运行行为不变。

2. **`engine/src/input/hotkey.{h,cpp}`**
   将 `Matches`、`CurrentMods` 中与 Win32 状态无关的判定提取为自由函数（`OwnModBit`、`HotkeyMatches`、`CapturedMods`），类内改为调用它们。行为不变。

3. **`ui/lib/core/ipc/plugin_client.dart` + `engine_client.dart`**
   新增抽象 `PipeClient`（`connect/send/lines/dispose`），`PluginPipeClient implements PipeClient`；`EngineClient` 构造函数增加可选 `PipeClient` 注入，默认仍在 `start()` 中 `??= PluginPipeClient()`。

4. **`engine/src/selftest_main.cpp`**
   新增 L1 用例与 L2/L3 用例入口；资源发现与 `SKIP` 输出；ASR 用例失败时打印最终文本与期望文本。

5. **`scripts/_ipc_smoke.ps1`**
   增加 L4 用例，保持"复用或自起自停"语义。

## 4. 测试素材

- `engine/tests/fixtures/zh_greeting.wav`：SAPI `Microsoft Huihui Desktop` 合成"今天天气不错"，16 kHz mono 16-bit PCM，约 1–2 秒。
- `engine/tests/fixtures/en_hello.wav`：SAPI `Microsoft Zira Desktop` 合成"hello world"，格式同上。
- `scripts/make_test_fixtures.ps1`：用 `System.Speech` 重新生成上述两个文件（幂等）；生成的 WAV 提交仓库，测试运行时不依赖本机 TTS。
- 素材总大小控制在 300 KB 以内。

## 5. 运行入口

新增 `scripts/test.ps1`：

```powershell
powershell -File scripts\test.ps1                  # 全量
powershell -File scripts\test.ps1 -Only engine,ui  # 仅指定层
powershell -File scripts\test.ps1 -Build           # 先跑 package.ps1 完整构建再测
```

执行顺序与失败语义：

1. `engine`：设置 `SHUOWEN_MODELS_DIR`/`SHUOWEN_TEST_WAV_DIR` 后运行 `dist\shuowen_selftest.exe`（不存在则回退 `engine\build\bin\shuowen_selftest.exe`）。
2. `ui`：`flutter analyze` + `flutter test`（`ui` 目录）。
3. `ipc`：`scripts\_ipc_smoke.ps1`。
4. `window`：`scripts\_hud_window_smoke.ps1`（需要 `dist\shuowen_engine.exe` 与 `dist\shuowen_ui.exe`）。

每一层输出 `PASS/FAIL/SKIP` 摘要；任意 `FAIL` 使总退出码非 0。`SKIP` 不影响退出码。默认不构建，缺失产物时提示用户加 `-Build`。

## 6. 成功标准

- 本机一条命令全绿，总时长 < 60 秒。
- 现有测试（selftest 44 项、flutter test 5 项）全部保留并通过。
- L3 在无模型/无素材环境下输出 `SKIP` 而不失败。
- L5 能捕获"layered 窗口未设属性"这一具体回归（即本次黑窗口 bug 的复现条件）。
- `git status` 在测试运行后保持干净（临时截图/日志写到 `%TEMP%`）。

## 7. 风险与缓解

| 风险 | 缓解 |
|---|---|
| ASR 输出与期望文本不完全一致 | 字符级编辑距离 ≤30% 容错 + 固定 WAV 素材，不做逐字相等断言 |
| TTS 音色在其他机器上识别差异 | WAV 提交仓库，测试不重新合成；生成脚本仅用于重建 |
| 窗口冒烟受 DPI/多显示器影响 | 只断言样式属性、尺寸（±2 px）与品牌色像素数，不比对完整截图 |
| 测试残留进程/窗口 | 脚本只处理自己拉起的进程，`finally` 中隐藏窗口并在自起时结束进程；复用用户引擎时不动进程 |
| selftest 在 dist 中运行时缺少素材 | 按查找顺序输出 `SKIP` 原因，不记失败 |
| L2 依赖真实时钟导致偶发超时 | 用例时长为毫秒级，节流断言留一倍余量 |

## 8. 文件清单

新增：

- `engine/src/asr/asr_backend.h`
- `engine/tests/fixtures/zh_greeting.wav`
- `engine/tests/fixtures/en_hello.wav`
- `scripts/make_test_fixtures.ps1`
- `scripts/_hud_window_smoke.ps1`
- `scripts/test.ps1`
- `ui/test/engine_client_test.dart`

修改：

- `engine/src/asr/sherpa_engine.h`
- `engine/src/asr/dictation.h` / `dictation.cpp`
- `engine/src/input/hotkey.h` / `hotkey.cpp`
- `engine/src/selftest_main.cpp`
- `scripts/_ipc_smoke.ps1`
- `ui/lib/core/ipc/plugin_client.dart`
- `ui/lib/core/engine_client.dart`
- `README.md`（验证章节加入 `scripts\test.ps1`）
- `docs/superpowers/specs/2026-10-22-shuowen-architecture-design.md`（13.2 覆盖缺口更新为已覆盖项）

不修改：`dist/`（构建产物）、`installer/`、引擎运行行为与 IPC 协议。