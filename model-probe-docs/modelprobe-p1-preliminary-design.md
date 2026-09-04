# ModelProbe P1 初步设计（v1.3）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-01（v1.3 修订 2026-09-01）
> 状态：待评审（用户 / GPT）
> 前置：P0 Go 后端已验证（`probe-go/main.go`——协议/真实网络全通，2026-09-01）/ 职责确认已收敛（2026-09-01 会话）
> 文档目录：本 demo 文档独立存 `model-probe-docs/`，不与框架 `docs/`（phaseN-*）混放
> v1.1：并入「QSS 观感 90%」视觉范围（用户 2026-09-01 拍板）——TextBox 圆角/恒显边框、Button hover 绘制、Panel 圆角/边框 4 个小能力入 P1；滚动条挂账
> v1.2：TextBox 新增**只读模式**（JSON 预览防误改——用户 2026-09-01 提问确认）
> v1.3：§5 补**大列表/搜索机制**（用户 2026-09-01 提问确认——查询后全量显示 + 搜索过滤 + 多选滚动）

---

## 1. 概述

用 ECDI 复现 `model-probe-gui`（PySide6 版「模型探测工具」）：输入 BaseURL + API Key → 查询 `/models` 列出可用模型 → 勾选 → 生成 JSON（预览 + 手动复制）→ 测试所选模型（`/chat/completions`）。GUI 呈现 = ECDI；HTTP/JSON = `probe.exe` Go 子进程（TSV 行协议 + stdin/stdout 管道）。视觉目标 = **还原 PySide6 版 QSS 观感的 90%**。

## 2. 职责确认（2026-09-01 会话收敛，本版记录）

| 决策点 | 结论 |
|---|---|
| 持久化 | **不做**（不存 base/key、无历史下拉、无 keyring）→ 连带消灭 ComboBox 需求 |
| Key 掩码 | **要做**（TextBox 新增 echo mode，`•` 打点 + 显示切换按钮） |
| 导出 | **JSON 预览（TextBox 只读）+ Ctrl+A/C 手动复制**（TextBox 剪贴板能力已有；无文件对话框） |
| 滚动列表 | **Panel 裁切 + 滚轮**（`MouseWheelEvent`；不造 ScrollView 控件） |
| 后端部署 | 固定目录 `networkbackend/` + 固定文件名；**存在则复用、不删除**；P1 调试期手动放置，P2 再资源嵌入+释放检测 |
| 默认占位 | base = `https://api.longcat.chat/openai/v1`（仅初始输入文本，不保存） |
| 主题 | 暗色（延续 Showcase 暗色风） |
| **视觉目标** | **QSS 观感 90%**（用户拍板）：颜色体系全还原 + 4 个形态小能力入 P1（TextBox 圆角/恒显边框、Button hover、Panel 圆角/边框） |
| **滚动条** | **挂账不做**（无滚动条控件；功能滚动用滚轮，GUI 后续阶段再考虑） |

## 3. 架构总览

```
ECDI GUI（ModelProbePage : Panel，demo 层）           probe.exe（Go 子进程）
├─ TextBox base              WriteLine("FETCH base key") ──→ stdin
├─ TextBox key（掩码）         ←── stdout ── OK/FETCH | MODEL×N | DONE/ERR（TSV 行）
├─ 模型列表（Panel 裁切+滚轮）          stdin EOF（CloseInput）= 退出信号
├─ JSON 预览（TextBox，Consolas 等宽）
└─ 轮询时钟：Application::OnTimer 覆写（timerId = 100）
```

分工原则（沿用框架分层）：**ECDI 只做呈现与交互；网络/JSON/并发归子进程**；进程管道是 ECDI Platform 能力（Win32 唯一归属），其余全部 demo 组装。

## 4. 框架新增能力（P1 动框架处共 6 项）

### 4.1 Platform 层：子进程 + 管道（ChildProcess）

**归属**：Platform 抽象 + Win32 实现（7.1 平台解耦原则——`CreateProcess`/匿名管道/`PeekNamedPipe` 只进 Win32 实现）。

```
ECDI/Platform/ChildProcess.h                        抽象接口 + 静态工厂
ECDI/src/Platform/Win32/Win32ChildProcess.{h,cpp}   Win32 实现
```

| 方法 | 语义 |
|---|---|
| `static std::unique_ptr<ChildProcess> Create()` | 工厂（demo/测试零 Win32 类型） |
| `bool Start(const std::string& path)` | CreateProcess + 匿名管道（stdin/stdout 重定向） |
| `bool WriteLine(const std::string& line)` | 写一行（UTF-8 字节 + `\n` 补全） |
| `std::string ReadAvailable()` | **非阻塞**读 stdout 全部可用字节（PeekNamedPipe；无数据返回空串） |
| `void CloseInput()` | 关 stdin 句柄 → 子进程读到 EOF 自退 |
| `bool WaitForExit(unsigned int ms)` | 等退出（超时返回 false） |
| `void Terminate()` | 兜底强杀（TerminateProcess） |
| `bool IsRunning()` | 存活查询 |

- 路径 UTF-8 → `UTF8ToWide`（Core/String.h 既有）
- **测试接缝**：接口注入——demo/测试可传 fake（RecordingBackend 式命令断言）
- **附加能力**：`ECDI::Platform::GetExecutableDirectory()`（Win32 `GetModuleFileNameW`）——P1 定位 `<exe_dir>/networkbackend/probe.exe`，P2 释放也复用；demo 保持零 Win32

### 4.2 控件层：TextBox（echo mode + 只读 + 圆角 + 恒显边框）

**echo 原则：显示层打点、数据层不变**——`m_text`/`GetText()` 永远真实值，仅绘制时替换掩码串。

```
enum class EchoMode{ Normal, Password };
void SetEchoMode(EchoMode mode);   EchoMode GetEchoMode() const;
```

| echo 子项 | 行为 |
|---|---|
| 绘制 | `OnPaint` 文本/选区/光标均按掩码串（U+2022 `•`，每码点一个）测量与绘制——所见即所得 |
| 编辑 | Insert/Delete/移动全在真实文本上（码点索引不变） |
| IME | 组合期显示原文（组合串不打点）；提交后打点 |
| 剪贴板 | Ctrl+C 复制**真实值**（标准密码框行为） |
| 显示切换 | demo 按钮调 `SetEchoMode(Password ↔ Normal)`（框架不做 toggle 控件，YAGNI） |

**只读模式（JSON 预览防误改——用户 2026-09-01 确认）**——原则同 echo：状态只禁编辑、不禁选择/复制。

```
void SetReadOnly(bool ro);   bool IsReadOnly() const;
```

| 只读子项 | 行为 |
|---|---|
| 编辑门禁 | `InsertCodepoint/InsertText/DeleteBackward/DeleteForward/Undo/Redo` 全部 no-op（编辑键/字符输入/IME 提交自然被拦） |
| IME | `UpdateComposition/CommitComposition` no-op（只读框不参与组合） |
| 导航/选区 | 方向键/Home/End/Shift+方向/Ctrl+A **照常**（只读 ≠ 不可选） |
| 复制 | Ctrl+C **照常**（复制真实文本——预览场景即 JSON 原文） |
| 视觉 | 无差异（同 Qt QPlainTextEdit 只读语义——可显示光标，仅不可编辑） |

**形态（QSS 90%：输入框圆角 6 + 1px 恒显边框）**——`TextBoxStyle` 新增字段（增量，不破坏现有语义）：

```
TextBoxStyle 新增：
  StyleField<float> cornerRadius;   // 0 = 直角（默认，现状不变）；>0 → OnPaint 背景改 DrawRoundedRect
  StyleField<float> borderWidth;    // 0 = 无恒显边框（默认）；>0 → 画 1px 描边环
  StyleField<Color> borderColor;    // 恒显边框色（QSS #2a3140；与 border 焦点点线框色并存）
```

- 描边环实现：**双矩形法**——先画全尺寸圆角矩形（borderColor），再内缩 borderWidth 画背景圆角矩形（半径同步减）→ 1px 实线环；无需新增 RenderCommand（Phase 8 能力现成）。限制：背景半透明时环内透出外层色——demo 全实色背景，可接受（限制记 §7 或详设）
- 焦点点线框（`DrawFocusRect`）**保持不动**（现有测试/语义零回归）

### 4.3 控件层：Button hover 绘制

**现状核查（2026-09-01）**：`pressedBackground` **已被 9.6 S1 消费**（`m_pressed → target → AnimationManager 动画 → m_displayedBackground → OnPaint`）；`hover` **未消费**（HoverTracker/`OnMouseEnter/Leave` 基建在，Button 未接管）；`SetEnabled` 功能态已有（HitTest 已排除 disabled，视觉灰化无）。

**P1 补 hover（最小扩展，复用 S1 管道）**：

```
ButtonStyle 新增：
  StyleField<Color> hoverBackground;   // hover 背景色（QSS hover #4f9cf7；默认 = 不区分，行为不变）

Button 新增：
  bool m_hovered;                       // OnMouseEnter/Leave 设置 + Invalidate
  目标色三态选择：m_pressed ? pressedBackground : (m_hovered ? hoverBackground : background)
```

- 三态过渡复用 9.6 S1 动画（200ms EaseOut——hover 平滑变亮，视觉更优）
- disabled 视觉灰化：**demo 层处理**（busy 时 `SetEnabled(false)` + `SetStyle` 灰底灰字；框架不做 disabled 绘制——YAGNI）

### 4.4 控件层：Panel 圆角 + 边框

**现状**：`PanelStyle` 仅 `background`，OnPaint 直角 DrawRect。

**P1 扩展（QSS 90%：列表容器圆角 8 + 1px 边框）**——`PanelStyle` 新增：

```
PanelStyle 新增：
  StyleField<float> cornerRadius;   // 0 = 直角（默认）；>0 → OnPaint 改 DrawRoundedRect
  StyleField<float> borderWidth;    // 0 = 无边框（默认）；>0 → 双矩形描边环（同 4.2）
  StyleField<Color> borderColor;    // 边框色
```

- 与 Button 同构（Button 已消费 cornerRadius；Panel 对齐该模式）
- 行分隔线（列表 `#1e2530`）不入框架——demo 的 ModelProbePage 自绘 `OnPaint` 里 `DrawLine`

## 5. Demo 组装（不入框架）

```
ECDI/src/Demo/ModelProbe.h / ModelProbe.cpp     （Showcase 同目录先例）
class ModelProbePage : public Panel              状态型控件（持 ChildProcess + 模型列表 + 选中集 + 行缓冲）
```

- **窗口策略（开放决策点 1）**：独立窗口与 Showcase 并存（推荐）或替换——见 §9
- **布局**（纵向，暗色，对齐 QSS）：标题/副标题 → base TextBox（圆角+边框）→ key TextBox（掩码+圆角+边框）+ 显示/隐藏按钮 → 查询/测试按钮行（busy 禁用）→ 统计 + 搜索 + 全选/清空行 → 模型列表（Panel 圆角边框容器 + 裁切 + 滚轮滚动 + 行分隔线；行 = CheckBox + id Label + meta Label）→ 导出格式 Radio×3（仅 ID / 对象数组 / 配置格式）→ 生成 JSON 按钮 → 预览 TextBox（Consolas 等宽，圆角+边框，**只读**）
- **大列表/搜索机制（对齐原版：查询后全量显示 + 搜索过滤 + 多选滚动）**：模型行**全量建为子控件**（几十~一两百行，demo 规模可行——每帧全量绘制经 PushClip 裁切，GDI 量级可承受）；列表容器 `OnMouseWheel` 调 Y 偏移 + PushClip 裁切 = 滚动；搜索框 `SetOnTextChanged` → 逐行 `SetVisible(子串命中)`——框架 Paint/HitTest 均跳过 invisible（Widget.cpp:108/201 实证，Showcase 切页同机制）；**性能兜底** = 视口裁剪（滚动时不可见行 `SetVisible(false)`，仅当全量绘制卡顿才启用）；行分隔线页面自绘 `DrawLine`；多选/全选/清空同 Showcase Selection 页（CheckBox `SetOnCheckedChanged` + 共享选中集）
- **轮询时钟**：demo Application 覆写 `OnTimer`——`timerId == 100`（避开框架保留段 1–15：1=光标、2=动画）→ `m_page->PollProbe()`；其余 timerId 转发基类（光标闪烁/动画原路由不变）
- **状态机**：`Idle → Querying → Done/Failed`（busy 期 `SetEnabled(false)` 查询/测试按钮 + 灰化）；TEST 同构
- **TSV 解析 / JSON 生成**：demo 级 `std::string` 工具（不引 JSON 库；手写拼接 + `"`/`\` 转义）
- **退出关停**：窗口关闭 → `CloseInput()` → `WaitForExit(1000)` → 超时 `Terminate()` 兜底；**不删除后端文件**（用户决策）

## 6. 主流程时序

**查询**：点击查询 → busy → `StartTimer(100, 50ms)` → `WriteLine("FETCH <base> <key>")` → 每次 tick `ReadAvailable()` 追加行缓冲 → 完整行分发：
`OK/FETCH/<base>/<count>` 记状态；`MODEL/<id>/<meta>` 追加模型行；`DONE/FETCH` → `StopTimer` + busy=false + 刷新列表；`ERR/FETCH/<msg>` → 状态行显示 + `StopTimer`。

**测试**：`WriteLine("TEST <base> <key> <id>...")` → `TEST/<id>/OK|FAIL/<msg>` 逐条收集 → `DONE/TEST` → 汇总写预览。

## 7. 测试策略（Phase 7.2 测试框架承载）

| 对象 | 用例 | 方式 |
|---|---|---|
| `Win32ChildProcess` | 集成：spawn `cmd.exe /c echo hello` → 读回 `hello`；`CloseInput` 后 EOF 自退；超时 `Terminate` | 真实子进程（不依赖 probe.exe 产物） |
| TextBox echo | 掩码绘制断言（RecordingBackend 记录文本为 `•••`）、`GetText()` 返回真实值、编辑/光标语义不变、IME 组合期显示原文 | TextBoxTests 扩展 |
| TextBox 形态 | `cornerRadius>0` → 命令流 `DrawRoundedRect`；`borderWidth>0` → 描边双矩形（先 borderColor 后 background）；默认 0 时命令流不变（零回归） | TextBoxTests 扩展 |
| TextBox 只读 | 编辑操作 no-op（文本/光标不变）、方向/选区/复制照常、IME 组合拒绝、只读下 SetText 仍可程序写入（预览刷新用） | TextBoxTests 扩展 |
| Button hover | `OnMouseEnter` → 目标色切 `hoverBackground`（无窗口测试：呈现值即时到位）；Leave → 还原；hover/pressed 优先级（按下优先） | ButtonTests 扩展 |
| Panel 形态 | `cornerRadius>0` → `DrawRoundedRect`；`borderWidth>0` → 描边双矩形；默认 0 命令流不变 | PanelTests 扩展 |
| `ModelProbePage` | 注入 fake ChildProcess：FETCH 命令发出、MODEL 行入列表、DONE 收尾、ERR 显示、busy 防重入 | fake 命令断言 |
| TSV 解析 / JSON 生成 | 纯函数单测（含转义/空 meta） | 单测 |

## 8. 影响面与授权清单（原子授权）

| 区 | 文件 | 动作 |
|---|---|---|
| 框架 | `ECDI/Platform/ChildProcess.h` + `ECDI/src/Platform/Win32/Win32ChildProcess.{h,cpp}` + `ECDI/Platform/ExecutablePath.h` | 新增 |
| 框架 | `ECDI/include/ECDI/Widget/TextBox.h` / `ECDI/src/Widget/TextBox.cpp`（EchoMode + 只读 + 形态） | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/TextBoxStyle.h`（cornerRadius/borderWidth/borderColor 字段 + Override） | 修改 |
| 框架 | `ECDI/include/ECDI/Widget/Button.h` / `ECDI/src/Widget/Button.cpp`（hover） | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/ButtonStyle.h`（hoverBackground 字段 + Override） | 修改 |
| 框架 | `ECDI/include/ECDI/Theme/PanelStyle.h` + `ECDI/include/ECDI/Widget/Panel.h` / `ECDI/src/Widget/Panel.cpp`（圆角/边框） | 修改 |
| 框架 | `ECDI/src/Theme/DefaultTheme.cpp`（新字段默认值） | 修改 |
| 框架 | 测试文件（ChildProcessTests / TextBoxTests / ButtonTests / PanelTests 扩展 / 解析单测） | 新增/修改 |
| demo | `ECDI/src/Demo/ModelProbe.{h,cpp}` | 新增 |
| 入口 | `ECDI/main.cpp`（新窗口 + OnTimer 覆写 + 启动后端） | **单独授权** |
| 工程 | `ECDI/ECDI.vcxproj`（登记新文件） | 修改 |
| 文档 | `model-probe-docs/`（本目录） | 新增 |

## 9. 开放决策点（评审时定）

1. **窗口策略**：独立窗口与 Showcase 并存（推荐——工具完整形态）vs 替换 Showcase。
2. **GetExecutableDirectory**：新增平台查询（推荐——P2 嵌入也要用）vs P1 先用相对路径 `networkbackend\probe.exe`（依赖 CWD，脆弱）。
3. **文档目录名**：`model-probe-docs/`（与 `probe-go/`、`model-probe-gui/` 同前缀并列）是否满意。
4. ~~滚动条~~：**已定挂账**（用户 2026-09-01 拍板——GUI 后续阶段再考虑）。

## 10. 修订记录

- v1.0（2026-09-01）初步设计初稿：职责确认收敛结果 + 架构总览 + 框架两处新增能力（ChildProcess / TextBox echo）+ demo 组装 + 测试策略 + 授权清单。前置 P0 已验证（协议骨架 + 真实网络全通，LongCat 实测）。
- v1.1（2026-09-01）并入视觉范围（用户拍板「QSS 观感 90%，滚动条挂账」）：§2 增视觉目标/滚动条决策；§4 扩展为 6 项能力——TextBox 增形态（cornerRadius/borderWidth/borderColor，双矩形描边环，焦点点线框不动）、**Button hover 绘制**（hoverBackground 新字段 + 三态目标色复用 9.6 S1；pressedBackground 已消费核实）、**Panel 圆角/边框**（同构扩展）；§5 布局对齐 QSS（圆角输入框/ghost 按钮/列表容器圆角）；§7/§8 测试与授权清单同步扩展。
- v1.2（2026-09-01）TextBox 新增**只读模式**（用户提问确认——JSON 预览防误改）：`SetReadOnly/IsReadOnly`；编辑门禁 = 五个编辑操作 + Undo/Redo + IME 组合 no-op；导航/选区/复制照常；视觉无差异；只读下 `SetText` 仍可程序写入（预览刷新）。§2 导出行、§5 预览标注、§7 测试、§8 授权同步。
- v1.3（2026-09-01）§5 补**大列表/搜索机制**（用户提问确认——查询后全量显示 + 搜索过滤 + 多选滚动）：全量建行 + PushClip 裁切（demo 规模可行）；搜索 = 逐行 `SetVisible(子串命中)`（Paint/HitTest 均跳过 invisible——Widget.cpp:108/201 实证）；性能兜底 = 视口裁剪（可选，仅卡顿时启用）；多选/全选/清空复用 Showcase Selection 页机制。
