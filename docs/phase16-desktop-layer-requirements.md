# Phase 16 桌面驻留层（`WindowLayer::Desktop`）需求确认（v1.0）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-19
> 状态：**待评审**（D0–D10 全部给倾向待拍板）
> 前置：`phase12-windowchrome-detailed-design.md` v1.5（**R10 语义 + `D-DESK-1` 契约**）· `desktopnest-roadmap.md` v1.7（**§5 G-1 登记** · **§7.1 E 路线取证** · §7.2 R-3）· `.workbuddy/spike/desktop_spike.cpp`（**路线 E 实测源码**，`--auto` 可无人值守复现）
> 一句话：把**已取证但未落地**的 `WindowLayer::Desktop` 真正实现进 `Win32PlatformWindow`——让「桌面驻留」从一句契约承诺变成可运行的能力。
> 一句话补充：本阶段**不新增任何公共 API**（`WindowLayer` 枚举与 `SetWindowLayer` 已于 Phase 12 就位）；真正的工作量在**平台实现层**与**窗口样式的一次性取舍**。

---

## 1. 背景与现状勘察

### 1.1 立项由来

`desktopnest-roadmap.md` v1.7 §5 把 G-1 登记为**唯一阻断项**：

> **G-1** ★ **`WindowLayer::Desktop` 尚未实现——当前降级执行 `Bottom`**〔`Win32PlatformWindow.cpp:863-868`（`if (layer == Desktop)` 只记 Warning）→ L882 走 `SetWindowPos(…, HWND_BOTTOM, …)`；**该 Warning 文案仍写 "spike pending"**——而 spike 已于 2026-09-15 六条判据全过 ⇒ **文案过期 + 能力未交付**〕**影响 M1 / M4**（硬前置）

⇒ 本阶段的定性是「**兑现既有承诺**」，而非「探索新能力」：

| 维度 | 状态 |
|---|---|
| 语义契约 | ✅ **已定稿**（Phase 12 `WindowLayer.h` 头注释 + `D-DESK-1`） |
| 公共 API | ✅ **已就位**（`WindowLayer::Desktop` 枚举值 + `Window::SetWindowLayer` + `PlatformWindow::SetWindowLayer`） |
| 实现路线 | ✅ **已取证**（2026-09-15 spike，路线 E 六条判据 ①–⑥ 全过） |
| 平台实现 | ❌ **缺失**（降级执行 `Bottom`，仅记一条过期的 Warning） |

**⇒ 这是全项目唯一「已取证但未落地」的能力**。用户取舍原则（2026-09-19 明确）：「**已取证但未落地**」优先于任何新功能——拖延会让 spike 结论随 Windows 版本失效（spike 环境 = Win11 build 26220 / 25H2 系列）。

### 1.2 现状勘察（全部带行号，2026-09-19 复核）

| # | 事实 | 出处 | 直接后果 |
|---|---|---|---|
| **K1** | **`Desktop` 档降级执行 `Bottom`**：`if (layer == WindowLayer::Desktop)` 只记一条 Warning，随后走通用分支 `SetWindowPos(m_hwnd, HWND_BOTTOM, …)` | `Win32PlatformWindow.cpp:863-870`（Warning）· `:880-885`（通用 `HWND_BOTTOM` 分支） | 核心卖点（Win+D 后仍可见）**不成立** |
| **K2** | **Warning 文案已过期**：文案仍写 `"not yet validated (spike pending)"` | `Win32PlatformWindow.cpp:868` | 与事实矛盾（spike 已结项）⇒ 必须随实现一并修正 |
| **K3** | **`WM_WINDOWPOSCHANGING` 是全档位共用的维护点**：`if (m_windowLayer != WindowLayer::Normal) wp->hwndInsertAfter = HWND_BOTTOM;` | `Win32PlatformWindow.cpp:405-424`（`:410` 判据 · `:416` 赋值） | **`Desktop` 与 `Bottom` 当前走同一分支**——两者目标 z 序位置**不同**（`Bottom` = `HWND_BOTTOM`；`Desktop` = **紧贴桌面窗口正上方**）⇒ 必须在此处分流 |
| **K4** | **窗口样式为 `WS_OVERLAPPEDWINDOW`，零扩展样式**：`CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW, …)` | `Win32PlatformWindow.cpp:63-77`（`:64` 第一参 = 0 扩展样式 · `:67` 主样式） | ⚠️ **本阶段头号议题**——spike 的 E 路线用 `WS_POPUP \| WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`（`desktop_spike.cpp:1053-1054`），与框架现状**三处不同**。详见 §1.3 |
| **K5** | **`SetWindowLayer` 是配置期 API**：`if (m_shown)` ⇒ Warning + 忽略 | `Win32PlatformWindow.cpp:846-853` | 样式取舍**必须在 `Show()` 之前完成** ⇒ 与「`CreateWindowExW` 在构造期一次性定样式」（`:63-77`）形成**时序紧张**——见 §1.3 F-2 |
| **K6** | **`m_windowLayer` 语义状态不随实现路径降级**（`D-DESK-1`） | `Win32PlatformWindow.h:116` 注释 · `Win32PlatformWindow.cpp:861` | 实现落地后本不变量**继续成立**（无需改动）；但 Phase 12 预留的「未来 `GetWindowLayer()` 返回 `Desktop`」约定**仍无消费者** |
| **K7** | **spike 的维护是「双层」的**：前台事件钩子（`EVENT_SYSTEM_FOREGROUND` 命中 `Progman`/`WorkerW`）+ 轮询位置校验 | `desktop_spike.cpp:605-616`（钩子）· `:1178-1187`（tick 校验，节流 250ms）· `:583-588`（`IsDirectlyAboveDesktop` 判据） | 框架侧**无心跳**（`Run()` 是纯 `GetMessageW` 阻塞循环，`Win32PlatformApplication.cpp:28-46`）⇒ 轮询兜底需要机制——见 §1.3 F-3 |
| **K8** | **桌面窗口句柄会失效**：`Progman` 被 explorer **销毁重建**（换壁纸 / 重启资源管理器） | `desktop_spike.cpp:555-580`（`RefreshDesktopHwnd` 全套防御 + 状态变化才打印） | **这是本阶段最容易写错的分支**：句柄失效时 `GetWindow(旧句柄, GW_HWNDPREV)` 返回 `NULL` ⇒ `SetWindowPos(hwnd, NULL, …)` 落到 `HWND_BOTTOM`——**比不重插更糟**（`desktop_spike.cpp:592-594` 已显式防御） |
| **K9** | **`SetWinEventHook` 需 `hInstance`**：spike 传 `nullptr`（`WINEVENT_OUTOFCONTEXT` 下合法） | `desktop_spike.cpp:626-628`（E 路线）· `:1042`（诊断钩子） | 框架侧有 `WindowClass::GetInstance()`（`Win32WindowClass.h:46`）可用——用 `nullptr` 亦可，但须核实四工具链一致性 |
| **K10** | **`WM_WINDOWPOSCHANGING` 只改 `hwndInsertAfter`**，不碰 `x/y/cx/cy/flags` | `Win32PlatformWindow.cpp:408-418` | ✅ 既有做法正确——**实现必须沿用**（否则会干扰最大化 / 还原几何） |
| **K11** | **框架无「窗口销毁后自动脱钩钩子」机制** | `Win32PlatformWindow::Release()`（`:187-197`）只 `DestroyWindow`；`~Win32PlatformWindow`（`:92-96`）调 `Release()` | 钩子是**每窗口**资源 ⇒ 必须在 `Release()` / 析构路径**显式 `UnhookWinEvent`**（否则句柄泄漏 + 回调打到已销毁对象） |
| **K12** | **测试设施无窗口级 z 序观测**：`RecordingBackend` 只记渲染命令 | `src/Tests/` 现有 24 文件（21 含用例） | 桌面档的**自动测试上限很低**（z 序是真机行为）——测试策略见 §6 |
| **K13** | **`Bottom` 档已有跨工具链验证的 z 序用例**（Phase 12） | `WindowChromeTests.cpp`（R10 相关用例）· `phase12-windowchrome-detailed-design.md` §5.3 T 系列 | 本阶段**不得回归** `Bottom` 语义（零回归底线） |
| **K14** | **`WS_EX_NOACTIVATE` 从未在框架内使用** | 全库 grep：`WS_EX_` 仅出现在 `Win32PlatformWindow.cpp` 的 DWM 相关处，无 `WS_EX_NOACTIVATE` / `WS_EX_TOOLWINDOW` | 若桌面档需要这两个样式，属**首次引入**——须评估对现有窗口行为（激活链 / 焦点 / Alt+Tab）的影响面 |

### 1.3 三条「会改形态」的事实

**F-1（头号）：窗口样式三处差异——桌面档不能直接复用现有窗口**

spike 的 E 路线窗口与框架窗口（`K4` / K14）逐项对照：

| 项 | spike（E 路线） | 框架现状（`CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW, …)`） | 差异性质 |
|---|---|---|---|
| 主样式 | `WS_POPUP` | `WS_OVERLAPPEDWINDOW` | ⚠️ **结构性**——`WS_OVERLAPPEDWINDOW` 含 `WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MINIMIZEBOX` / `WS_MAXIMIZEBOX` |
| 扩展样式 | `WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE` | `0` | ⚠️ `WS_EX_NOACTIVATE` 缺失 ⇒ 点击桌面档窗口**会抢焦点**（`WS_EX_TOOLWINDOW` 还影响 Alt+Tab 与任务栏） |
| 创建时机 vs 样式决定 | spike 进程启动即定（`desktop_spike.cpp:1052-1054`） | **构造期 `CreateWindowExW` 一次性定**（`K4`），而 `SetWindowLayer` 在**构造之后**才可能被调用（`K5`） | ⚠️ **时序紧张**——样式在构造时已固化，层级却在此后才设定 |

**⇒ 这是本阶段最重要的结构判断**：Phase 12 曾以「**保留 `WS_OVERLAPPEDWINDOW`**」为红线（换取 Alt+Space / Aero Snap / 最小化动画等系统红利，见 `phase12-windowchrome-requirements.md` §1 与详设 §5.4 手测矩阵）。若桌面档必须换 `WS_POPUP`，则该红利在桌面档上**明确放弃**——这是一个需要用户拍板的**范围取舍**，不是实现细节。

⚠️ **同时须澄清一个常识性误判**：`WS_EX_NOACTIVATE` 的取舍**不等于**「桌面档不可交互」。spike 用它只是因为探针程序不需要交互；而 DesktopNest 的框**需要**被点击（成员列表 / 收缩展开按钮）⇒ `WS_EX_NOACTIVATE` **不应盲目照搬**。这条判定直接影响 F-1 的结论——见 D2。

**F-2：样式决定时机与配置期契约的错位**

`SetWindowLayer` 是配置期 API（`K5`：`Show()` 之后 Warning + 忽略），而 `CreateWindowExW` 在**构造期**（`K4`）。若桌面档需改主样式，则存在两条路径（详见 D3）：

- **路径 A**：构造期不改样式，`SetWindowLayer(Desktop)` 时用 `SetWindowLongPtrW(GWL_STYLE / GWL_EXSTYLE)` 改 + `SetWindowPos(…, SWP_FRAMECHANGED)` 刷新 —— 但**未 `Show()` 的窗口改样式是安全的**（配置期语义恰好允许）
- **路径 B**：构造期就按「可能要桌面档」预置 —— 与 `WindowLayer` 的「配置期设定」语义冲突（构造时还不知道用户选哪个档）

**F-3：维护机制缺「心跳」**

spike 的双层维护（`K7`）中，前台钩子解决了「桌面层被抬升」的主要场景（实测 12 秒内自动重插 3 次），轮询只是**兜底**。但框架的 `Run()` 是纯阻塞 `GetMessageW`（`Win32PlatformApplication.cpp:28-46`）——**没有 tick**。

⇒ 三个候选（详见 D4）：
1. **只装前台钩子**（spike 的主要机制，实测已足够）——零新机制，但钩子漏触发时无兜底
2. **窗口级 `SetTimer`**（框架已有：`PlatformWindow::StartTimer`，`WindowMessageHandler.cpp:243` 翻译 `WM_TIMER` → `TimerEvent`）——但 `TimerEvent` 是**给 Widget 的事件**，平台层自用会污染事件语义
3. **平台层自建 `SetTimer` 且不走翻译器**（在 `Win32PlatformWindow::HandleMessage` 内消化，符合 R9 三步惯例第 ③ 步）——机制干净，但新增一个平台内部定时器

---

## 2. 技术路线（一段话定方向）

**分两半，边界清晰：**

- **「贴上去」= 平台实现**：`WM_WINDOWPOSCHANGING` 里把 `hwndInsertAfter` 从 `HWND_BOTTOM` 改成 `GetWindow(progman, GW_HWNDPREV)`（紧贴桌面窗口正上方）⇒ **公共 API 零变更**，兑现 `WindowLayer.h` 的「平台如何达成该层级是实现的自由」。这正是 spike 对框架意义的结论（`desktopnest-roadmap.md` §7.1 末段）。
- **「维持住」= 双层机制**：前台事件钩子（主）+ 位置校验（兜底）+ 桌面句柄重建自适应（`K8` 防御）。

**四个硬骨头**（对应 D1–D4）：

1. **窗口样式**（F-1）——桌面档能否沿用 `WS_OVERLAPPEDWINDOW`？最小必需集是什么？
2. **`WS_EX_NOACTIVATE` 的取舍**（F-1 澄清）——桌面常驻应用需要交互，不能照搬 spike。
3. **样式改动时机**（F-2）——构造期 vs `SetWindowLayer` 时刻。
4. **维护机制的兜底**（F-3）——钩子够不够，要不要心跳。

---

## 3. 需求条目

### 3.1 实现主体 · R1–R4

**R1 · `Desktop` 档真实生效**：`Window::SetWindowLayer(WindowLayer::Desktop)` 后，窗口的 z 序位置为「**紧贴桌面窗口正上方**」——即同时满足契约两侧：**被应用窗口覆盖**（应用窗口在它之上）与 **Win+D 后仍可见**（桌面层被抬升时它跟随）。**验收判据以 §1.1 的 ①–⑥ 为准**（spike 已定的六条人工判据）。

**R2 · `Bottom` 语义零回归**：`WindowLayer::Bottom` 与 `Normal` 的行为**逐位不变**（`K3` 的分支改造不得影响这两档）。⚠️ 现有的 `WM_WINDOWPOSCHANGING` 通用分支（`:410` 判据 `!= Normal`）**必须按档位分流**，而不是把 `Desktop` 塞进同一分支。

**R3 · 与 `Window` 解耦的维护生命周期**（对应 K8 / K11）：
- 桌面窗口句柄（`Progman`）失效 / 重建时**必须自适应**——刷新句柄后重插，**不得**因旧句柄失效而落到 `HWND_BOTTOM`；
- 窗口销毁（`Release()` / 析构）时**必须脱钩**全部平台钩子（无句柄泄漏、无回调打到已销毁对象）。

**R4 · 文案与注释同步**：`K2` 的过期文案（`"not yet validated (spike pending)"`）必须随实现一并修正——**不得留下与事实矛盾的日志**。`WindowLayer.h` 头注释中「spike 未通过前此档位不承诺可用」的措辞同步复核（`WindowLayer.h:25-26`）。

### 3.2 交互与样式 · R5–R7

**R5 · 桌面档的窗口样式**：桌面档的**最小必需样式集**——见 **D1** / **D2**。核心问题是「`WS_OVERLAPPEDWINDOW` 能否保留」，以及「扩展样式是否需要调整」。

**R6 · 交互能力**：桌面档窗口**必须保持框架既有的全部交互能力**——鼠标事件 / 键盘事件 / 焦点 / Widget 树命中**不得**因层级档位而降级。⚠️ 这是对 F-1 澄清的直接需求化：`WS_EX_NOACTIVATE` **不得**被理解为「桌面档的默认值」。

**R7 · 视觉无异常**：桌面档窗口**不得**出现「挖洞」/ 残影 / 重绘异常（spike 判据 ⑦）。特别是与 Phase 12 的 DWM 增强（阴影 / Win11 圆角，`ApplyDwmEnhancements`）的兼容性——样式变更后**须复验**。

### 3.3 覆盖范围 · R8–R10

**R8 · 与 `ChromeMode` 的正交性**：`WindowLayer` 与 `ChromeMode` 是**两个正交维度**（Phase 12 初设已定：`Normal` / `Borderless` × `Normal` / `Bottom` / `Desktop`）。需求上：**`Desktop` 档在两种 `ChromeMode` 下都应可用**——除非实现层有硬冲突（理论上 `Borderless` + `Desktop` 是最自然的组合）。⇒ 若确认存在冲突，须在初设显式记账并降级（而非静默）。

**R9 · 多窗口支持**：多个窗口可各自独立设置层级档位；一个窗口切档**不得**影响其它窗口的 z 序维护（每个窗口独立持有自己的钩子 / 句柄缓存——`K11` 的生命周期归属）。

**R10 · 与 `Hide()` / `Release()` 的关系**（Phase 14 引入的显示控制）：`Hide()` 后再 `Show()`，桌面档的维护**应能恢复**；`Release()` 后钩子**必须**已脱钩（`R3` 的下半）。

### 3.4 测试与验收 · R11–R12

**R11 · 自动测试**：可自动断言的部分**必须有用例**（`K12`：能力有限，需精确划定）；不可自动断言的部分**必须**给出**可复现的手测步骤**（spike 的 `--auto` 模式已证明「Win+D 可无人值守复现」）。⇒ 分层见 §6。

**R12 · 跨工具链**：四工具链（MSVC / Clang / ClangCL / MinGW）构建通过；**`FRAMEWORK_ASSERT` 生效的构建**下零断言（沿用项目验证纪律）。

---

## 4. 决策点（D0–D10——全部给倾向待拍板）

| # | 决策 | 选项 | **倾向** | 理由 |
|---|---|---|---|---|
| **D0** | 范围与形态 | **A** 独立 Phase（走五阶段 + 设计文档可评审）/ **B** 并入 DesktopNest M1 一并做 | **A** | 用户 2026-09-19 立项即选独立 Phase（roadmap §9 第 9 项的两选项之一是 B，本次明确取 A）。理由：这是**唯一「已取证但未落地」**的能力，留下**可评审的契约**对简历叙事与后续维护都有价值；并入 M1 会让「框架能力」与「应用逻辑」混在一份文档里，平台细节无处安放 |
| **D1** | 窗口主样式 | **A** 保留 `WS_OVERLAPPEDWINDOW` / **B** 改为 `WS_POPUP` / **C** 分档位：`Normal`/`Bottom` 保持现状，`Desktop` 用 `WS_POPUP` | **C**（但须初设验证 A 是否可行） | `K4` 的 `WS_OVERLAPPEDWINDOW` 带来系统红利（Alt+Space / Aero Snap / 最小化动画），**但 `WS_CAPTION` 存在意味着有系统标题栏**——桌面档窗口**贴着桌面**却带系统标题栏在语义上别扭（而 `Borderless` 档恰好能盖掉这层）。⚠️ 倾向 **C + 初设实测**：先验证「`WS_OVERLAPPEDWINDOW` + `Borderless` 能否满足 spike 的可见性判据」——**若能，则 A 更优**（零样式变更）。spike 全程用 `WS_POPUP` 是**探针程序的简化**，不构成「必须 `WS_POPUP`」的证据 |
| **D2** | `WS_EX_NOACTIVATE` | **A** 桌面档加此样式（照搬 spike）/ **B** **不加**（保持可激活）/ **C** 加 `WS_EX_TOOLWINDOW` 但不加 `WS_EX_NOACTIVATE` | **B 或 C**（初设定，见下） | ⚠️ **spike 用 `WS_EX_NOACTIVATE` 只是因为探针不需要交互**（`desktop_spike.cpp:17` 明写「窗口用 `WS_EX_NOACTIVATE`，不抢焦点，故热键为全局轮询」）——这是**探针的便利**，不是 E 路线的**必要条件**。DesktopNest 的框需要点击（R6）⇒ **A 明确不可取**。`WS_EX_TOOLWINDOW` 的取舍取决于「是否希望桌面框出现在任务栏 / Alt+Tab」——倾向**不加**（桌面常驻物不该占任务栏位），但须与 R6 一并验证不损害交互 |
| **D3** | 样式改动时机 | **A** 构造期按 `WindowLayer` 预置（需把层级提前到构造参数）/ **B** `SetWindowLayer(Desktop)` 时用 `SetWindowLongPtrW` 改 + `SWP_FRAMECHANGED` / **C** 不改样式（D1 选 A 时无需改） | **B**（D1 选 C 时）/ **C**（D1 选 A 时） | `K5`：`SetWindowLayer` 是配置期 API ⇒ **改动发生在 `Show()` 之前，系统尚未显示窗口，改样式是安全的**。A 需要把 `WindowLayer` 提升为构造参数——**破坏 `SetWindowLayer` 的 API 形态**，且与 Phase 12 已定稿的「配置期四件套统一生命周期」不一致（详设 §9.1） |
| **D4** | 维护机制兜底（F-3） | **A** 只装前台钩子 / **B** 钩子 + 窗口级 `SetTimer` 且走 `TimerEvent` 翻译器 / **C** 钩子 + 平台内部 `SetTimer`（不进翻译器） | **A（首选）+ C（若初设认为需要兜底）** | spike 实测前台钩子已足够（12 秒内自动重插 3 次，`desktopnest-roadmap.md` §7.1）；轮询只是保险。⚠️ **B 明确不可取**——`TimerEvent` 是「派发给焦点 Widget」的框架事件（`Application::OnTimer` → `FindFocusedWidget`），平台层自用会**污染事件语义**（违反 Event 原则：Event 只表示「已发生的事实」，不应承载平台内部维护）。若需要兜底，C 符合 R9 三步惯例第 ③ 步（消息在平台实现内消化） |
| **D5** | 钩子安装形式 | **A** 每窗口一个 `SetWinEventHook` / **B** 应用级共享一个钩子 + 窗口注册表 | **A** | `K9`：`WINEVENT_OUTOFCONTEXT` 下 `hInstance` 可传 `nullptr`。每窗口一个钩子实现最简、生命周期归属清晰（`K11`：`Release()` 时脱钩本窗口的）。B 引入「谁管理注册表」的额外归属问题，但可减少钩子数量——**多窗口场景下 A 的钩子数 = 窗口数**，可接受（YAGNI：无大规模多窗口消费者） |
| **D6** | 桌面窗口句柄缓存 | **A** 每窗口缓存 `g_desktopHwnd`（spike 做法）/ **B** 进程级共享缓存 / **C** 不缓存（每次重查 `FindWindowW(L"Progman", nullptr)`） | **C（最简）** | `FindWindowW` 是廉价的（`desktop_spike.cpp:1180-1183` 做 250ms 节流只是为避免日志刷屏）。**不缓存 ⇒ 天然免疫 `K8` 的失效问题**（无需 `RefreshDesktopHwnd` 那套状态检测）。⚠️ 但须初设确认调用频率（若每个 `WM_WINDOWPOSCHANGING` 都调，需评估开销） |
| **D7** | 桌面窗口定位方式 | **A** `FindWindowW(L"Progman", nullptr)`（按类名）/ **B** `GetShellWindow()` / **C** 枚举顶层窗口按类名匹配 | **A** | spike 全程用 A 且实测通过（`desktop_spike.cpp:560` / `:619` / `:642`）。B 是官方 API 且更「正规」（MSDN：返回 shell 的桌面窗口），**但未在 spike 环境实测**——须初设核实两者是否恒等（`GetShellWindow()` 的返回值定义与 `Progman` 的对应关系）。⇒ 倾向 **A（已实测）+ 初设补验 B** |
| **D8** | `Bottom` 与 `Desktop` 的代码组织 | **A** `WM_WINDOWPOSCHANGING` 内 `switch` 分流 / **B** 抽两个私有辅助方法各自实现 / **C** 抽象成「目标 z 序位置」计算函数 | **C** | `K3` 的分支改造是核心 diff。C 让判据集中（`HWND TargetInsertAfter() const`）——`Normal` 返回「不改」（`SWP_NOZORDER`）、`Bottom` 返回 `HWND_BOTTOM`、`Desktop` 返回 `GetWindow(progman, GW_HWNDPREV)`。⚠️ 但**桌面句柄无效时的语义**须在 C 里显式处理（`K8`：返回 `NULL` 会落到 `HWND_BOTTOM` ⇒ 应「跳过本次修改」而非返回 `NULL`） |
| **D9** | 自动测试边界 | **A** 只测「不回归」（`Bottom` / `Normal` 语义 + 脱钩配对）/ **B** 在 A 基础上加「z 序目标计算函数」的纯逻辑测试 / **C** 尝试测真实 z 序 | **B** | `K12`：真实 z 序依赖桌面环境（`Progman` 存在、explorer 行为）⇒ **C 在 CI / 自动化中不可靠**。B 通过 D8 的 C 抽象把「目标值计算」变成**可注入纯函数**——句柄有效性 / `Normal` 分支 / `Bottom` 分支都可断言；`Desktop` 的**真实生效**归手测（`R11`：spike 的 `--auto` 已证明可无人值守复现） |
| **D10** | 手测判据复用 | **A** 直接复用 spike 的六条判据（`--auto` + 人工观察）/ **B** 重写一套判据 / **C** 用 ModelProbe 的 `--layer desktop` 通道手测 + 六条判据对照 | **C** | ModelProbe **已有** `--layer desktop` 参数通道（`examples/ModelProbe/main.cpp:247`，当前注释写「spike 未通过 → 降级 Bottom + Warning」——**该注释同样过期，随 R4 一并修正**）。用真消费者手测比独立探针更接近真实场景；六条判据（`--auto` 可复现 ①⑥）作为对照基线 |

---

## 5. 非目标（YAGNI 圈定）

- **`GetWindowLayer()` 查询 API**——Phase 12 `D-DESK-1` 已明确「YAGNI：无消费者」（`phase12-windowchrome-detailed-design.md` §1.1 表第 9 行）。本阶段**不新增**（虽然实现落地后该 API 的语义不再有「降级」歧义，但**仍无消费者**）
- **运行期切层**——`SetWindowLayer` 保持配置期契约（`K5`）。`D3` 的路径 B 只依赖「配置期内改样式」，**不**松开运行期判据
- **桌面窗口树（`Progman` 子树 / `WorkerW`）路线**——spike 已判死两条（A / D：`WS_EX_NOREDIRECTIONBITMAP` ⇒ 子树不参与合成；reparent 被 explorer 重建杀死）。**不重新探索**
- **`WS_EX_TOPMOST` 路线**——spike 实测能活过 Win+D，但恒在应用窗口**之上**，违反契约另一侧（`desktopnest-roadmap.md` §7.1）。**不可替代 E**
- **跨 DPI 空间的坐标换算修复**（spike 实测的 ×0.8 现象，`desktopnest-roadmap.md` §7.1 末段）——属 **G-4**（DPI 感知），不在本阶段
- **R-3（框内图标不可用）的任何缓解**——已定论为**结构性**（§7.2 四方案全不可行），只能靠应用层位置 / 面积缓解
- **桌面层级的多显示器差异**——不同显示器上的桌面窗口是否同一 `Progman`，未勘察（见 §7 待勘察项）
- **托盘 / 拖入的进一步集成**——Phase 14 已交付，本阶段零改动

---

## 6. 测试 / 验证方向

**分层原则**（`K12`：真实 z 序不可自动化；`R11`：可自动的部分必须自动）：

| 层 | 内容 | 判据 | 自动化 |
|---|---|---|---|
| **L1 逻辑（自动）** | `Desktop` 的目标 z 序位置计算（`D8` 的 C 抽象，经测试缝注入句柄） | `Normal` ⇒ 不修改 · `Bottom` ⇒ `HWND_BOTTOM` · `Desktop` + 有效句柄 ⇒ `GetWindow(progman, GW_HWNDPREV)` · `Desktop` + **无效句柄** ⇒ **跳过修改**（`K8` 核心分支） | ✅ |
| **L1 生命周期（自动）** | 钩子安装 / 脱钩配对（`R3` / K11） | `SetWindowLayer(Desktop)` ⇒ 钩子 +1；`Release()` / 析构 ⇒ 钩子 -1；`Hide()` → `Show()` ⇒ 不重复安装 | ✅（经内部测试缝计数，参照 Phase 14 托盘 `NotifyShell` 函数指针先例） |
| **L2 零回归（自动）** | `Bottom` / `Normal` 档语义（`R2`） | 既有 z 序用例全过（`K13`） | ✅ |
| **L2 构建（自动）** | 四工具链 + `_DEBUG`（`R12`） | 构建通过、`ecdi_tests` 全过、零断言 | ✅ |
| **L3 真机（手测）** | spike 六条判据 ①–⑥ + 视觉判据 ⑦ | ① Win+D 后仍可见 · ② 十次切换稳定 · ③ 框外图标可点 · ④ 换壁纸后仍可见 · ⑤ explorer 重启后仍可见 · ⑥ 被应用窗口覆盖 · ⑦ 视觉无异常（无挖洞 / 残影） | ❌ 手测（`D10`：ModelProbe `--layer desktop` + 六条判据对照） |
| **L3 真机（手测）** | `R6` 交互不降级（F-1 澄清的直接验证） | 桌面档下鼠标点击 / 键盘输入 / 焦点 / Widget 命中全部正常；**点击框不抢焦点**（若 `D2` 选 C） | ❌ 手测 |
| **L3 真机（手测）** | `R7` DWM 兼容 | 样式变更后系统阴影 / Win11 圆角仍正常（`ApplyDwmEnhancements` 复验） | ❌ 手测 |
| **L3 真机（手测）** | `R8` `ChromeMode` 正交性 | `Normal` / `Borderless` 两档 × `Desktop` 均可（或记录冲突） | ❌ 手测 |
| **L3 真机（手测）** | 多窗口（`R9`） | 两个窗口分别设 `Desktop` / `Bottom`，各自维护不互扰 | ❌ 手测 |

> 📌 **`--auto` 的价值**：spike 的 `desktop_spike.cpp` 支持 `--auto`（自动注入 Win+D + 采样），已证明**判据 ①⑥ 可无人值守取证**（`desktopnest-roadmap.md` §7.1）。⇒ 若本阶段产出同类探针，应保留该模式以便后续 Windows 版本变更时**快速复验路线是否仍成立**——这是「spike 结论会随系统版本失效」这一风险的唯一对冲手段。

---

## 7. 影响面

| 类别 | 项 |
|---|---|
| **新增 Public** | **零**——`WindowLayer` / `Window::SetWindowLayer` / `PlatformWindow::SetWindowLayer` 全部已就位（§1.1） |
| 修改 Public | **零**（倾向）——除非 `D1` 需要在 `WindowLayer.h` 补充样式语义注释（属注释级改动） |
| 修改 Internal | `Win32PlatformWindow.h`（`D8` 的辅助方法声明 + 可能的钩子 / 定时器成员）· `Win32PlatformWindow.cpp`（`WM_WINDOWPOSCHANGING` 分流 + 钩子安装/脱钩 + 样式改动（若 `D1` 选 C）+ `K2` 文案修正 + `Release()` 脱钩）· 可能的 `Win32PlatformApplication`（若 `D4` 选 C 且定时器归应用级） |
| 修改 Demo | `examples/ModelProbe/main.cpp:247` 过期注释（`R4`）——⚠️ **AI 不得动 `main.cpp`**，须用户授权 |
| 测试 | `src/Tests/` 新增用例（`D9` 的 L1 两层）+ `RunAllTests.h` / `RunAllTests.cpp` 手工接线；⚠️ **测试替身同步**（若 `PlatformWindow` 加纯虚——**本阶段倾向零新增纯虚**，见 `phase14` §7 的风险先例） |
| 文档 | 新增本文件 + 初设 + 详设；`docs/README.md` 索引；`desktopnest-roadmap.md` **§5 G-1 标 ✅** + §9 第 9 项收口 + §10 修订记录；可能需 `roadmap-deferred.md` 记账（若发现新的延期项） |
| 明确不动 | 渲染四层（Widget → PaintContext → CommandBuffer → Renderer）· `WindowLayer` 枚举语义 · Phase 12 的四消息拦截骨架（NCCALCSIZE / NCHITTEST / NCACTIVATE / WINDOWPOSCHANGING **的 `Bottom` 部分**）· Phase 13 命中委托 · Phase 14 托盘 / 拖入 · 布局与尺寸体系 · 公共 API 面（净增 0 头） |

---

## 8. 待勘察项（初设须回答）

| # | 项 | 为什么重要 |
|---|---|---|
| 1 | **`WS_OVERLAPPEDWINDOW` 能否满足 spike 的可见性判据**（`D1` 的关键前提） | 若能，则零样式变更（最保守）；若不能，须确认**最小必需样式集**（`WS_POPUP` 是不是唯一选项？去掉 `WS_CAPTION` 的 `WS_OVERLAPPEDWINDOW` 变体行不行？） |
| 2 | **`GetShellWindow()` 与 `FindWindowW(L"Progman")` 是否恒等**（`D7`） | 决定用官方 API 还是实测 API；两者不等时须选一个并说明 |
| 3 | **`SetWindowLongPtrW` 改样式的安全窗口**（`D3`） | 配置期内（未 `Show()`）改样式的实际行为——是否需要在改后追加 `SetWindowPos(…, SWP_FRAMECHANGED)`；对 `ChromeMode::Borderless` 的实现（NCCALCSIZE 拦截）有无交互 |
| 4 | **`SetWinEventHook` 跨工具链一致性**（`K9`） | `hInstance` = `nullptr` vs `WindowClass::GetInstance()` 在四工具链下的行为差异 |
| 5 | **多显示器下 `Progman` 的唯一性** | §5 已列为非目标，但若「每个显示器一个桌面窗口」成立，则 `D6` / `D7` 的结论需要按显示器维度重做——**至少须确认前提是否成立**（一条 `EnumWindows` 就能查清） |
| 6 | **`SetTimer` 在配置期是否可用**（`D4` 选 C 时） | `StartTimer` 目前是运行期语义（`SetFileDropEnabled` 同组，Phase 14 O-3 拍板）；若平台层自用则不受此限，但须确认 `m_hwnd` 在配置期已就绪（`K4`：构造期已 `CreateWindowExW`，✅ 已就绪） |

---

## 9. 修订记录

- v1.0（2026-09-19）**需求确认初稿**（立项：`desktopnest-roadmap.md` v1.7 §5 G-1，用户 2026-09-19 拍板「G-1 立项」）：
  - §1 背景与现状勘察——立项定性「**兑现既有承诺**」而非探索新能力（语义契约 / 公共 API / 实现路线三者均已就位，缺的只有平台实现）· **K1–K14 现状勘察全部带行号**（2026-09-19 复核）· **三条会改形态的事实**：**F-1 窗口样式三处差异**（`WS_OVERLAPPEDWINDOW` vs spike 的 `WS_POPUP \| WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`，含一条常识性澄清——`WS_EX_NOACTIVATE` 是**探针便利**而非路线必要条件）· **F-2 样式决定时机与配置期契约错位** · **F-3 维护机制缺心跳**。
  - §2 技术路线——「贴上去」= 平台实现（公共 API 零变更）· 「维持住」= 前台钩子 + 位置校验 + 桌面句柄重建自适应；四个硬骨头。
  - §3 需求条目 **R1–R12 四组**：实现主体 R1–R4（含「`Bottom` 语义零回归」与「句柄失效自适应」两条硬要求）· 交互与样式 R5–R7（含**交互能力不得降级**——F-1 澄清的需求化）· 覆盖范围 R8–R10（`ChromeMode` 正交 · 多窗口 · `Hide`/`Release` 关系）· 测试与验收 R11–R12。
  - §4 决策点 **D0–D10 全部给倾向待拍板**（范围形态 / 主样式 / `WS_EX_NOACTIVATE` / 改动时机 / 维护兜底 / 钩子形式 / 句柄缓存 / 定位方式 / 代码组织 / 测试边界 / 手测判据）。⚠️ **D1 / D2 是本阶段的真正取舍点**——其余八项在倾向明确后基本无争议。
  - §5 非目标（`GetWindowLayer` 仍不做 / 运行期切层 / 桌面窗口树路线不重探 / `TOP MOST` 不可替代 / 跨 DPI 归 G-4 / R-3 缓解不做 等 8 项）· §6 测试方向（**L1 自动 / L2 零回归 / L3 手测三层**，含 `--auto` 作为「系统版本变更时的对冲手段」）· §7 影响面（**公共 API 净增 0**——与 Phase 12/14/15 均不同，本阶段纯实现层）· §8 **待勘察项 6 条**。
  - **需求阶段边界自律**（skill 条 6）：本文档不含头文件草案与方法签名——全部归初步设计；`D8` 的「目标 z 序位置计算函数」以概念形如表述，不写签名。
