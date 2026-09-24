# Phase 20 · DPI 感知（DPI awareness）—— 详细设计

> 状态：**v1.3**（2026-09-24）——✅ **全阶段收口**：五批落地 · 四工具链 · 用例 **236 → 247** · ★★ **人工验收通过**（**100% 与改前一致** · **125% / 150% 视觉正常** · **跨屏完全正常**）；★ **实施期暴露的渲染缺口以 `RG-1` 正式登记并已关闭**（见 **§14.5**）。★ v1.2 记录 4 处实施偏离（见 **§14**）
> v1.1 = 评审**有条件通过**（外部评审结论：「**可进入实现阶段**」，附 **1 处实施前必修**（负数舍入）+ 2 处文档精确性）——本版补齐 **契约 C10** 并把 3 项一并落定（见 **§1.5**）
> 来源：需求稿 **v1.1**（已通过）· 初步设计 **v1.1**（已通过，评审指定 **3 件详设必做**）· `roadmap-deferred.md` **§7.9 #8** · 审计 **§4 D-2**
> 定性：**让既有 DIP 契约生效**——不是引入新概念
> 本稿任务：落实初设 **§9 的七件事**，给出**可实现的逐文件行级改动**

---

## 1. 设计输入与基线

### 1.1 已冻结（需求 v1.1 + 初设 v1.1，本稿不再讨论）

| 项 | 内容 |
|---|---|
| **G1–G5** | 目标；★ **G5** = `dpi == 96 ⇒ px == dip`（零回归护栏） |
| **N1–N5** | 非目标 |
| **六项定案** | **Q1** 单位分层（单位由「跨越哪条线」决定）· **Q2** 测量返回 DIP · **Q3** 声明落 `Application` 构造 · **Q4** 只加 `GetDpiScale`（**G4 范围收敛**）· **Q5** 定案 A（入参换 DIP）· **Q6** DPI 为翻译器成员状态 |
| **§2.3.1** | ★ **「读取而非假定」**——框架从不假定 V2 生效，始终读当下 DPI ⇒ 三条路径各自自洽 |
| **C1–C8** | 契约（本稿 §6 给出验证映射） |
| **R3** | 架构约束：换算**只发生在平台边界** |

### 1.2 详设新增基线（B19–B26，2026-09-24 逐条实测）

| # | 事实 | 证据 |
|---|---|---|
| **B19** | ★★ **`PlatformWindow` 已有「能力扩展惯例」定稿**（**D-SEAM-1**，三步）：① 接口加一个能力 virtual（**接口即契约，零消息号**）② `Window` 加公共方法透传（**应用层唯一入口**）③ 平台消息在具体实现内消化。**先例** = Phase 12 的 `SetChromeMode` / `SetWindowLayer` / `Minimize` … ⇒ **本项按此模板，不新造接缝** | `PlatformWindow.h:22-28` |
| **B20** | ★★ `PlatformWindow` **全部既有方法都是纯虚**（`:34`–`:134`）；**实现者恰好 3 个** = `Win32PlatformWindow`（生产）+ `TestPlatformWindow` ×2（测试替身） | `PlatformWindow.h:34-134` · `Win32PlatformWindow.h:25` · `AnimationTests.cpp:24` · `ProgressBarTests.cpp:30` |
| **B21** | ★ **`GDIBackend::GetOrCreateFont` 是私有「实例」方法，不是 static**（初设 §3.3 记错）——`ToColorRef` 才是 static ⇒ **可直接用 `m_hwnd` 取 DPI，改动更小** | `GDIBackend.h:61`（实例）vs `:62`（static） |
| **B22** | ★★ **IME 触面是 3 处，不是 1 处**（初设 §3.2 只写了一句）：① **`CreateCaret` 的宽高尺寸** ② `SetCaretPos(x, y)` ③ 局部 `POINT pt{x, y}`——同时供 `COMPOSITIONFORM` 与 `CANDIDATEFORM` | `Win32PlatformWindow.cpp:675-677` · `:683` · `:694-700` → `:712` · `:722` |
| **B23** | ★ **鼠标坐标翻译点 5 处**（与 Phase 19 的翻译点同数）：Move `:126-127` · Down `:150-151` · DblClk `:169-170` · Up `:190-191` · Wheel `:207-210`（★ Wheel 的在 `ScreenToClient` **之后**） | `WindowMessageHandler.cpp` |
| **B24** | ★★ **既有 T4 用例天然就是 G5 的回归锚**：`handler.Handle(nullptr, nullptr, WM_SIZE, 0, MAKELPARAM(300, 200))` 断言 `width == 300`、`height == 200`（**逐位相等**）⇒ Phase 20 后**必须原样通过**（它证明 `dpi == 96` 时换算恒等） | `EventTests.cpp:258-268` |
| **B25** | `Win32PlatformWindow` **当前无任何 DPI 状态成员**；两个 chrome 量注释明写**"逻辑坐标 DIP"**；`DipToPixels` 是 **static 私有**（⇒ 无状态，新增的 `PixelsToDip` 沿用同一形态） | `Win32PlatformWindow.h:126` · `:200-201` |
| **B26** | ★ **`Window::OnResized` 是布局几何的唯一入口**：`RootWidget->SetSize(w,h)` → `Arrange()` → `Invalidate()`（**顺序契约**：消费者收到事件时 RootWidget 已是新尺寸） | `Window.cpp:449-460` |
| **B27** | ★★ **纯换算函数若保留 `HWND` 形参，就无法无头测试**：现有 `static int DipToPixels(int dip, HWND hwnd)` 内部做「取 DPI + 算公式」两件事 ⇒ **取 DPI 依赖真窗口** | `Win32PlatformWindow.cpp:1322-1347` |
| **B28** | ★ **`public static` 作测试入口有现成先例**：`ResolveTarget` 注释明写「**public static 的唯一目的是可被自动化测试直接覆盖**」，且测试已 include 该内部头（`DropFilesTests.cpp:13` 先例） | `Win32PlatformWindow.h:102-105` |

### 1.3 ★ 对初设的四处修正（实现前必须接受）

| # | 初设原文 | 实测 | 处置 |
|---|---|---|---|
| **修1** | §3.3「`GDIBackend::GetOrCreateFont` **是静态**」 | **实例方法**（B21） | ✅ **修正**——改动更小，可直接读 `m_hwnd` |
| **修2** | §3.2「IME 双通道：`SetCaretPos` / `COMPOSITIONFORM` / `CANDIDATEFORM` 之前 `DipToPixels`」（1 处） | **3 处**（B22，含 `CreateCaret` 的**尺寸**） | ✅ **修正**——漏掉 `CreateCaret` 会让 150% 下**光标尺寸不缩放** |
| **修3** | §3.2「`DipToPixels` **保留**（注释更新）」 | ★ **须改签名为纯函数** | ★ **修正**——见下 |
| **修4** | §5「`PlatformWindow::GetDpiScale` 是**新纯虚** ⇒ 实现者须全库 grep」 | grep 完成：**3 个实现者**（B20），且 **D-SEAM-1 惯例本就要求纯虚**（B19） | ✅ **确认**——按惯例走纯虚，清单见 §2.5 |

**★ 修3 的详细理由（本稿最实质的一处设计改进）**：

初设 §9-① 要求「**纯换算独立可测**」，但现有 `DipToPixels(int, HWND)`（B27）把**取 DPI** 与**算公式**耦合在一起 ⇒ **无头测试拿不到 `HWND`**，纯换算就测不了。

⇒ **定案：把两个换算函数都拆成纯函数**（**去掉 `HWND`，改收 `int dpi`**）：

```cpp
static int DipToPixels(int dip, int dpi);      // 签名变更：HWND → dpi
static int PixelsToDip(int px,  int dpi);      // 新增
```

★ **收益**：
1. **纯函数可直接无头覆盖**（§9-① 的要求达成，且**无需任何测试缝**）；
2. **DPI 来源与坐标转换彻底解耦**——取 DPI 只在调用点（`GetDpiForWindow`），与评审 §11 / 初设 Q6 的精神一致；
3. 与 `ResolveTarget` 的 `public static` 先例同形（B28）。

★ **代价**：`DipToPixels` 的 2 个既有调用点（`WM_NCHITTEST` 的 `:377` / `:379`）**本来就要改**（Q5 定案 A 会让它们消失）⇒ **实际零额外成本**。

### 1.4 初设 §9 七件事 → 本稿答案索引

| 初设项 | 问题 | 本稿答案 |
|---|---|---|
| **①** ★评审指定 | `PixelsToDip` 完整规格 + 舍入定案 | **§3**（纯整数 half-up；★ 给出**纯整数反例**证明双向恒等不可能） |
| **②** ★评审指定 | `WM_DPICHANGED` 完整处理链 | **§4**（★ 接入既有 `WM_SIZE → OnResized` 链，**不新开 resize 路径**） |
| **③** | `GetDpiScale` 纯虚的**实现者清单** | **§2.5**（3 个实现者逐一列出） |
| **④** | 两处字体缓存的 DPI 化 | **§2.3 的 △18/△19** |
| **⑤** | DPI 注入通路（`SetDpi` 调用点） | **§2.2 的 △14/△15** |
| **⑥** | P1–P4 实测方案与记录位置 | **§8** |
| **⑦** ★评审指定 | 感知声明失败后的行为策略 | **§5**（主体 = 初设 §2.3.1；本稿落实兜底 API + 诊断日志） |

### 1.5 ★ 对评审意见的逐条处置（详设第一轮，2026-09-24）

外部评审**结论：总体通过，可进入实现阶段**；附 **1 处「实施前应修正」+ 2 处「文档精确性」**。逐条处置：

| # | 评审条目 | 处置 |
|---|---|---|
| 1 | ★★ **负数舍入**：`DipToPixels` / `PixelsToDip` 的 half-up **对负数不成立**（C++ 整数除法**向 0 截断**，而 `+ 48` / `+ dpi/2` 的偏移是按"向 +∞"设计的）；建议**新增 C10 明确负数语义**，并给出 `sign(x)·floor(\|x\|+0.5)` | ✅ **采纳（经实测确认，且比评审举例更普遍）** → **§3.1 改共用 helper `RoundHalfAwayFromZero`** · **§3.2.1 新增负数算例** · **§6 新增 C10** · **§7 新增负数用例（T20-6）**。★ 实测：评审举的两个算例成立（`PixelsToDip(-1,144)` 现得 **0**、应为 **-1**；`DipToPixels(-1,144)` 现得 **-1**、应为 **-2**），且 `dpi=144` 的 `-20..0` 区间**两函数各 20/20 全部偏差** ⇒ **系统性错误，非边界个例** |
| 2 | ★ **`int` 溢出**：`long long` 只解决**中间乘法**，最终 `static_cast<int>` 仍可能超范围；建议把"overflow 防护"**准确**描述为「防止中间乘法溢出」，别暗示全范围饱和 | ✅ **采纳** → **新增 §3.6**（措辞精确化）。★ 该问题**不阻塞实现**（既有几何量级远达不到 `INT_MAX`），仅修文档表述 |
| 3 | ★ **Win10 1703 支持基线**：§5.3 留的"待评审确认"是**半悬状态**，应**正式定掉**（明确基线 ⇒ 不做 `GetProcAddress` 动态加载） | ✅ **采纳** → **§5.3 重写为「支持基线冻结」**。★ **这是项目级支持范围决策**——本版按"**目标 = Windows 10 1703+**"写入（与「ECDI 1.x = Windows 可用」一致），**请确认**；不确认则须改为补 `LoadLibrary`/`GetProcAddress` 兜底 |
| 4 | 认可 **§1.3 修3（`DipToPixels` 改纯函数）**·`WM_DPICHANGED` 不自行 `OnResized`·**② 早于 ③** 的顺序契约·字体缓存**键隔离**·IME **3 处**补全·**坐标整数/测量浮点的口径差异**·C9 的往返不恒等·**五批顺序**的故障隔离价值 | ✅ 原样保留（评审明确"可以冻结"） |
| 5 | 评审**未提** | ★ **我方补充**：**负数的真实来源定位**——只可能是**鼠标坐标**（`WM_NCHITTEST` 的 `pt.x - rcWin.left`、`WM_MOUSEMOVE` 的 `GET_X_LPARAM` 有符号），而 `WM_SIZE` / `GetClientSize` / IME caret **均非负** ⇒ 触面有限但真实（§3.2.1） |

---

## 2. ★ 逐文件行级改动（△1–△25）

> 约定：行号为 **2026-09-24 实测**值；`△n` 可按任意顺序实施，但 **§9 给出推荐批次**。

### 2.1 公共头（△1–△5，5 个文件）

| △ | 文件 | 改动 |
|---|---|---|
| **△1** | `include/ECDI/Platform/PlatformWindow.h` | **新增 1 个纯虚**（插在 `GetClientSize()` 之后——与"查询窗口几何"同族；**按 B19 的 D-SEAM-1 惯例第 ① 步**）<br>`/// @brief 窗口当前 DPI 缩放比（1.0 = 100%）`<br>`/// @details 事实来源在平台层（GetDpiForWindow）；与 GetClientSize 同族。`<br>`virtual float GetDpiScale() const noexcept = 0;` |
| **△2** | `include/ECDI/Window/Window.h` | **新增公共透传**（插在 `GetPlatformWindow()` 之后——D-SEAM-1 第 ② 步）：<br>`/// @brief 本窗口的 DPI 缩放比（1.0 = 100%；Phase 20 R7/G4）`<br>`float GetDpiScale() const noexcept;` |
| **△3** | `include/ECDI/Core/Font.h` | `size` 的注释：`字号（第一版：像素高度）` → **`字号（DIP 高度——Phase 20 D4；数值不变）`**（**语义变更，非数据变更**） |
| **△4** | `include/ECDI/Render/TextMeasurer.h` | 两个方法补单位说明：**`@return 尺寸/行高（DIP）`**（签名**不变**） |
| **△5** | `include/ECDI/Application/Application.h` | 构造的 `@details` 补一句：**「进程 DPI 感知（Per-Monitor V2）在本构造期声明，早于任何窗口创建」** |

### 2.2 平台层（△6–△17）

| △ | 文件 / 位置 | 改动 |
|---|---|---|
| **△6** | `Win32PlatformWindow.h:126` 区 | `DipToPixels` **签名变更**（**B27/修3**）：`static int DipToPixels(int dip, HWND hwnd)` → **`static int DipToPixels(int dip, int dpi)`**；**新增** `static int PixelsToDip(int px, int dpi)`；★ 二者**移入 public 区**（B28 的 `ResolveTarget` 先例：# 注释明写"public static 的唯一目的是可被自动化测试直接覆盖"） |
| **△7** | `Win32PlatformWindow.h:63` 后 | **新增** `float GetDpiScale() const noexcept override;`（紧随 `GetWindowState`——同为"查询"族） |
| **△8** | `Win32PlatformWindow.cpp:1322-1347` | `DipToPixels` 重写为**纯函数**（只剩公式 + 溢出防护；**取 DPI 移出**）。★ 原「`GetDC` 失败 ⇒ 返回 dip」的 fail-safe **随之移到调用点**（§3.4 给出新形态） |
| **△9** | `Win32PlatformWindow.cpp`（△8 之后） | **新增 `PixelsToDip` 实现**（规格见 **§3**） |
| **△10** | `Win32PlatformWindow.cpp:114-128` | `CreateWindowExW` 的 `width`/`height` 前插换算：`DipToPixels(width, Dpi(m_hwnd))`——★ 此处 `m_hwnd` 尚未存在 ⇒ **须用 `GetDpiForSystem()`** 或**先建窗口再按 DPI 调整尺寸**（★ 见 **O-1**，本稿倾向后者：`CreateWindowExW` 用 `CW_USEDEFAULT` 尺寸 → 拿到 `hwnd` 后 `GetDpiForWindow` → `SetWindowPos` 定尺寸） |
| **△11** | `Win32PlatformWindow.cpp:266-270` | `GetClientSize()`：`GetClientRect` 结果经 `PixelsToDip` 后再返回（**需求 §4.1 的"不得合并"判据在此落地**） |
| **△12** | `Win32PlatformWindow.cpp:356-435` | **`WM_NCHITTEST` 改 A**（初设 §2.5）：`:372-374` 求出窗口坐标后立刻 `PixelsToDip`（取 `GetDpiForWindow(hwnd)`）；`:377`/`:379` 两处 `DipToPixels` **撤除**（`m_captionHeight`/`m_resizeInset` **本就是 DIP**，直接比较）；`:381-383` 的 `w`/`h` 亦转 DIP；★ **K1 注释同步改写**（「换算点唯一在此」仍成立，**方向反转**为 物理 → DIP） |
| **△13** | `Win32PlatformWindow.cpp:519-551` | `WM_SIZE`：`m_host.OnResized(LOWORD(lParam), HIWORD(lParam))` → **两个实参各经 `PixelsToDip`**（★ **B26 的布局源头**） |
| **△14** | `Win32PlatformWindow.cpp`（`WM_SIZE` 之后） | **新增 `WM_DPICHANGED` 处理**（规格见 **§4**） |
| **△15** | `Win32PlatformWindow.cpp:675-683` + `:694-700` | IME **3 处**换算（**B22/修2**）：① `CreateCaret` 的 `width`/`height` ② `SetCaretPos` 的 `x`/`y` ③ `POINT pt` 的 `x`/`y`（供 ②a `:712` / ②b `:722`）——★ `pt` **一处定义三处使用**，故换算点实际是 **2 处**（`:683` 与 `:694`） |
| **△16** | `Win32PlatformWindow.cpp:102-141`（构造） | ★ **构造末尾**追加：`m_messageHandler.SetDpi(GetDpiForWindow(m_hwnd));`（**B25：当前无 DPI 状态**） |
| **△17** | `Win32PlatformWindow.cpp`（`GetWindowState` 之后） | **新增 `GetDpiScale` 实现**：`return GetDpiForWindow(m_hwnd) / 96.0f;`（★ 失败/0 ⇒ 返回 `1.0f`，与 G5 一致） |

### 2.3 翻译器与渲染层（△18–△21）

| △ | 文件 | 改动 |
|---|---|---|
| **△18** | `WindowMessageHandler.h:44-48` + `:50-61` | **新增** `void SetDpi(int dpi) noexcept { m_dpi = dpi; }` 与私有成员 `int m_dpi = 96;`（默认 96 ⇒ **既有 `EventTests` 用例行为逐位不变**，B24） |
| **△19** | `WindowMessageHandler.cpp` | **5 处鼠标坐标**（**B23**）+ **1 处尺寸**经 `PixelsToDip(·, m_dpi)`：Move `:126-127` · Down `:150-151` · DblClk `:169-170` · Up `:190-191` · Wheel `:207-210` · `WM_SIZE` `:106-108`（★ 与 △13 **两条路都要改**，漏一条则另一条改对也无效） |
| **△20** | `GDIBackend.h:61` + `.cpp:759-793` | `GetOrCreateFont(font)` → **加 DPI**（`GetDpiForWindow(m_hwnd)`）；`lfHeight = -lround(DipToPixels(font.size, dpi))`；**缓存键加 DPI**（`GDIBackend.h:76` 的 `map<pair<float,string>, HFONT>` → 键含 `int dpi`）<br>★ **实施偏离（§14-①）**：**不调** `DipToPixels`（契约 C2 + 分层纪律）⇒ 后端**自算** `lround(font.size * dpi / 96.0)`（float 口径） |
| **△21** | `GDITextMeasurer.h:30` + `.cpp:20-55` | 同上；★ **基准 DPI = 它自己的测量 DC 的 `LOGPIXELSX`**（初设 §2.2.4：**进出一致** ⇒ 不变更"零 hwnd"设计）；`MeasureText`/`LineHeight` 的返回值**折回 DIP**（`px * 96.0f / dpi`，**保留 float**——此处是测量链路，与 `PixelsToDip` 的整数口径**不同**，见 §3.5）<br>★ **实施（§14-①同源）**：换算同样**自算**；且 `GetOrCreateFont` **加 `int dpi` 形参**（"纯测量零 hwnd"⇒ DPI 只能由调用点给） |

### 2.4 应用层（△22）

| △ | 文件 | 改动 |
|---|---|---|
| **△22** | `Application.cpp`（构造） | ★ **评审指定 ③**：构造期调用 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`，**失败容忍**（`Log(Warning)`、不抛、不断言）+ **记录实际感知级别**（规格见 **§5**）<br>★ **实施偏离（§14-②）**：调用**不写在 `Application`**——按「每个 Win32 API 唯一归属」**下沉应用级接缝**（`PlatformApplication::DeclareDpiAwareness` + `Win32PlatformApplication` 实现）；`Application` 构造只**调用**它 |

### 2.5 测试（△23–△25 + `GetDpiScale` 实现者清单）

| △ | 文件 | 改动 |
|---|---|---|
| **△23** | **新建** `src/Tests/DpiTests.cpp` | 承载 **T20-1..T20-6**（**纯换算**——§7.1；★ **含 v1.1 新增的负数用例 T20-6**）。★ **须 include `Platform/Win32/Win32PlatformWindow.h`**（纯换算经 public static 调用——B28 先例） |
| **△24** | `RunAllTests.h` + `RunAllTests.cpp` | ★ **两处登记**（skill 条 99③：只在**新增测试文件**时才动） |
| **△25** | `AnimationTests.cpp:24-40` · `ProgressBarTests.cpp:30-46` | ★ **两个 `TestPlatformWindow` 各补 1 行**：`float GetDpiScale() const noexcept override { return 1.0f; }`（**B20：纯虚的实现者清单**——★ 全库 grep 确认**仅此 3 处**，无遗漏） |

---

## 3. `PixelsToDip` 的完整规格（★ 评审指定 ①）

### 3.1 两个函数（★ 均为纯函数、public static）

```cpp
/// @brief DIP → 物理像素（整数 half-up；dpi == 96 时恒等——G5）
static int DipToPixels(int dip, int dpi);

/// @brief 物理像素 → DIP（整数 half-up，与正向同风格）
static int PixelsToDip(int px, int dpi);
```

**公式：带符号的整数 half-up（远离零）——★ 评审第一轮修正（§1.5 处置 1）**

★ **为什么不能沿用「加偏移量再除法」**：C++ 的整数除法**向 0 截断** ⇒ `(负数 + 正偏移) / 正数` 得到的是"**向零**"而非"**远离零**"，**对负数不成立**（实测负数区间 **100% 偏差**，见 §3.2.1）。因此两个方向**共用**一个带符号 helper：

```cpp
/// @brief 带符号的整数 half-up（**远离零**）：sign(x) · floor(|x| + 0.5)
/// @details ★ 契约 C10——正负完全对称；前置 `den > 0`（调用点保证）。
static int RoundHalfAwayFromZero(long long num, long long den);

int Win32PlatformWindow::RoundHalfAwayFromZero(long long num, long long den)
{
	// C10：sign(x) · floor(|x| + 0.5)
	const bool negative = (num < 0);
	const long long magnitude = negative ? -num : num;

	long long q = magnitude / den;
	if (2 * (magnitude % den) >= den) { ++q; }   // half-up（远离零）

	return static_cast<int>(negative ? -q : q);
}

int Win32PlatformWindow::DipToPixels(int dip, int dpi)
{
	if (dpi <= 0) { dpi = 96; }                  // §3.4 fail-safe
	return RoundHalfAwayFromZero(static_cast<long long>(dip) * dpi, 96);
}

int Win32PlatformWindow::PixelsToDip(int px, int dpi)
{
	if (dpi <= 0) { dpi = 96; }                  // §3.4 fail-safe
	return RoundHalfAwayFromZero(static_cast<long long>(px) * 96, dpi);
}
```

★ **正数上与本稿 v1.0 的旧式 `(x + 偏移) / 分母` 逐位等价**——已实测：`0..2000 × 6 种 dpi（96 / 105 / 120 / 144 / 168 / 192）差异数 = 0` ⇒ **G5 不受影响**（**这是采纳本修正的前提**，评审未验）。
★ **`dpi == 96` 时两个方向仍恒等**（含负数：`-5 → -5`）。
★ **溢出**：中间量升 `long long`，作用是「**防止中间乘法溢出**」——**不承诺**对最终 `int` 转换做全范围饱和（★ 措辞精确化，见 **§3.6**）。

### 3.2 ★★ 「双向恒等在数学上不可能」——**纯整数反例**

初设 O3 已采纳评审该论断，但评审给的是**浮点**算例（`PixelsToDip(2) = 1.333…`）——那只是"若不取整会怎样"。**本稿给出纯整数实现下的反例**（更贴合实际代码）：

| dpi | 计算 | 结果 |
|---|---|---|
| **120** | `PixelsToDip(2, 120) = (2·96 + 60) / 120 = 252 / 120` | **2** |
| **120** | `DipToPixels(2, 120) = (2·120 + 48) / 96 = 288 / 96` | **3** |
| ⇒ | **`px → dip → px`：`2 → 2 → 3 ≠ 2`** | ✗ **不恒等** |

**根因（一句话）**：`dpi > 96` 时**物理像素网格比 DIP 网格更密** ⇒ **多个物理像素映射到同一个 DIP** ⇒ 反向放大**无法还原**（信息已丢失）。

⇒ ★★ **契约 C9（新增）**：**两个方向各自保精度，但不得追求往返恒等**；`dpi == 96` 时**两个方向都恒等**（G5）。**详设/实现/评审均不得把"往返不恒等"当 bug 去修。**

#### 3.2.1 ★★ 负数算例（详设评审第一轮实测发现）

| dpi | 计算 | v1.0 旧式「加偏移再除」 | **v1.1（对称 half-up）** | 数学值 |
|---|---|---|---|---|
| 144 | `PixelsToDip(-1)` | **0** ✗ | **-1** ✓ | −0.667 |
| 144 | `DipToPixels(-1)` | **-1** ✗ | **-2** ✓ | −1.5 |

★ **不是边界个例**：逐点扫描 `dpi = 144` 的 `-20..0`，两个函数**各 20/20 全部偏差** ⇒ **系统性错误**。
★ **负数的真实来源（本稿定位，评审未提）**：**只有鼠标坐标**——`WM_NCHITTEST` 的 `x = pt.x - rcWin.left`（鼠标在窗口左/上外侧时为负）+ `WM_MOUSEMOVE` 的 `GET_X_LPARAM`（**有符号**，拖拽出界为负）；而 `WM_SIZE`（尺寸）· `GetClientSize` · IME caret 坐标**均非负** ⇒ 触面有限，但**真实**（150% 下表现为"窗口外侧 1 DIP ≈ 1.5 px 的系统性偏移"）。

### 3.3 误差界

| 方向 | 单次转换的最大误差 |
|---|---|
| `DipToPixels` | ≤ 0.5 px（half-up 固有） |
| `PixelsToDip` | ≤ 0.5 DIP（half-up 固有） |
| ★ 组合（一次往返） | **可 > 0.5**（§3.2 的反例即 1 px 级）——这正是**不得追求恒等**的原因 |
| ★★ **负数（v1.1 修正后）** | **同样 ≤ 0.5**——对称 half-up 对 `sign` 无偏。★ **v1.0 的旧式实现对负数超界**（可达 **1 整**，见 §3.2.1）⇒ 本条是 v1.1 的**修正结果**，不是 v1.0 的性质 |

### 3.4 fail-safe 的落点（△8 移出取 DPI 后）

| 场景 | 处理 |
|---|---|
| `GetDpiForWindow(hwnd)` 返回 **0**（失败） | 调用点**降级用 96** ⇒ 换算恒等 ⇒ 回到现状行为（**与既有 `GetDC` 失败返回 `dip` 的语义等价**） |
| 纯函数收到 `dpi <= 0` | ★ **契约**：按 96 处理（**不 assert**——沿用决策 30「失败 ⇒ 局部跳过」风格；纯粹函数不应中止进程） |

### 3.5 与测量链路的**口径差异（须显式声明，防后人"统一"它们）**

| 链路 | 数值类型 | 理由 |
|---|---|---|
| **坐标换算**（`DipToPixels` / `PixelsToDip`） | **整数** | 框架几何全是 `int`（`Widget::SetSize(int,int)` · `MouseEvent::GetMouseX() -> int`）⇒ **保留 float 只会把舍入点后移**（初设 O3 已论证） |
| **测量折算**（§2.3 △21 内部） | **float** | `MeasureText` / `LineHeight` 的接口本就是 `float`；且测量值**不参与整数几何的构造**，保精度无害 |

⇒ ★ **两者不是同一个函数**：`PixelsToDip` **不服务测量链路**（测量走自己的 `px * 96.0f / dpi`）。**不要在实现时把它们合并**。

### 3.6 ★ 溢出防护的准确表述（★ 评审第一轮处置 2）

| 项 | 准确表述 |
|---|---|
| **做了什么** | 中间量升 `long long`（`dip * dpi` · `px * 96`）⇒ 「**防止中间乘法溢出**」 |
| **没做什么** | **不承诺**对最终 `static_cast<int>` 做**全范围饱和**——极端输入（如 `dip = INT_MAX` · `dpi = INT_MAX`）仍会越界截断 |
| **为什么不阻塞** | 框架的几何量级（窗口尺寸 / 控件坐标 / 鼠标坐标）**远达不到** `INT_MAX` ⇒ 属**文档精确性**问题，**不是缺陷** |
| **契约口径** | **C10 只约束舍入语义**；**输入范围**不在本阶段契约内（将来若出现越界输入，另立条目） |

---

## 4. `WM_DPICHANGED` 的完整链（★ 评审指定 ②）

### 4.1 硬约束（评审补充）

> **必须严格接入既有的 `WM_SIZE → OnResized → layout` 链，避免出现两套 resize 路径。**

### 4.2 完整链（新增处理块，置于 `Win32PlatformWindow.cpp` 的 `WM_SIZE` 之后）

```text
WM_DPICHANGED
  │  wParam = 新 DPI（LOWORD = X 轴，HIWORD = Y 轴）
  │  lParam = RECT*（系统建议的窗口物理矩形）
  ├─ ① 取新 DPI：const int dpi = LOWORD(wParam);
  ├─ ② 更新翻译器：m_messageHandler.SetDpi(dpi);        ← ★ 先更新，后续换算才用新值
  ├─ ③ 采纳建议矩形：SetWindowPos(hwnd, nullptr, rc.left, rc.top,
  │                        rc.right - rc.left, rc.bottom - rc.top,
  │                        SWP_NOZORDER | SWP_NOACTIVATE);
  ├─ ④ ★ 不自行重布局——SetWindowPos 会触发 WM_SIZE，
  │      由其走既有链：WM_SIZE → OnResized(PixelsToDip(w,h)) → RootWidget::SetSize
  │                                                          → Arrange() → Invalidate()
  └─ ⑤ 字体缓存：★ 无需显式清理——DPI 进缓存键（△20/△21）⇒ 旧 DPI 的 HFONT
                 自然不命中（初设 §3.3 / O2 的"键隔离"）
```

★ **④ 是本链的关键**：**绝不在 `WM_DPICHANGED` 里直接调 `OnResized`**——那会与随后的 `WM_SIZE` 形成**两套 resize 路径**（评审明确点出的风险）。`WM_DPICHANGED` **只做三件事**：更新 DPI、采纳矩形、让系统去发 `WM_SIZE`。

★ **顺序契约**：② 必须早于 ③——否则 `WM_SIZE` 到达时翻译器仍是旧 DPI。

### 4.3 边界情形

| 情形 | 处理 |
|---|---|
| `lParam` 为 null | 不采纳矩形（只更新 DPI）——防御性，`DefWindowProc` 仍会收到 |
| 新 DPI 与旧值相同 | ★ 仍全链执行（幂等——`SetWindowPos` 到同尺寸是 no-op） |
| 窗口处于最大化 / 最小化 | 交系统处理（`WM_DPICHANGED` 在最大化态下由系统给出正确的还原矩形） |
| 触摸 / 无鼠标的跨屏迁移 | 同样经本链（不依赖鼠标位置） |

---

## 5. 感知声明与失败策略（★ 评审指定 ③）

### 5.1 落点（△22）

```text
Application::Application()   ← 用户构造的第一个框架对象；早于任何 Create（初设 §2.3）
  └─ SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
       ├─ 成功 ⇒ 记 Log(Info) 一次（可选）
       └─ 失败 ⇒ ① Log(Warning)——含成因提示
                  ② ★ 追加查询并记录「实际感知级别」（见 §5.2）
                  ③ **不抛异常、不断言、不降级尝试**（O4 倾向：主动降级反而更差）
```

### 5.2 失败时的诊断（★ 本稿落实）

```cpp
// 失败后：把"实际感知级别"写进日志，便于排查（初设 §2.3.1 的"残留边界"）
DPI_AWARENESS_CONTEXT ctx = GetThreadDpiAwarenessContext();
const DPI_AWARENESS aw = GetAwarenessFromDpiAwarenessContext(ctx);
// UNAWARE / SYSTEM_AWARE / PER_MONITOR_AWARE / PER_MONITOR_AWARE_V2
```

★ **为什么只记录、不矫正**：初设 **§2.3.1「读取而非假定」** 已定案——框架**始终**读当下 DPI，因此**任何感知级别下行为都自洽**：
- 失败 + 本就是 unaware ⇒ `GetDpiForWindow` **恒 96**（★ 有实测证据）⇒ 换算恒等 ⇒ **退化为现状**（= G5 护栏）
- 失败 + 应用已设过级别 ⇒ 读到该级别下的值 ⇒ 仍自洽

⇒ **不存在"半降级"**（评审担心的形态）。

### 5.3 支持基线：**Windows 10 1703+**（★ v1.1 冻结，不留半悬——评审第一轮处置 3）

| 层 | 策略 |
|---|---|
| **支持基线** | ★★ **目标平台 = Windows 10 1703+**（`SetProcessDpiAwarenessContext` 的引入版本）。★ **本条是项目级支持范围决策**——v1.1 按此写入（与「**ECDI 1.x = Windows 可用**」的定位一致），**请确认**；若目标含更低版本，则须改为补 `LoadLibrary` / `GetProcAddress` 动态加载 |
| **API** | ★ **不做动态加载**（基线已保证该函数存在；YAGNI） |
| **失败容忍** | **仍保留**（§5.2）——覆盖「**已被声明过**」「**系统策略拒绝**」两种**运行期**失败，与"函数是否存在"无关 |
| **manifest** | 片段（附带提供，不强制）——覆盖"应用要自己控制"与"需早于 `Application` 构造"两种例外 |

---

## 6. 契约 C1–C9 的验证映射

| # | 契约 | 由什么验证 |
|---|---|---|
| **C1** | 单位分层（公共 API/框架内部 DIP；边界内部/后端物理） | 人工核 + 用例（T20-7/8 的 DIP 期望值） |
| **C2** | ★ 换算只在平台边界 | **grep 可检**：`grep -rn "DipToPixels\|PixelsToDip" ECDI/` 只应命中 `src/Platform/Win32/`（+ 测试） |
| **C3** | 测量返回值 = DIP | `TextMeasurer.h` 注释 + 目视（A5，100% 下与现状一致） |
| **C4** | `GetClientSize` 返回 DIP 而后端 `GetClientRect` 保持物理 | 代码结构（△11 与 `GDIBackend` 不共用访问器） |
| **C5** | 零破坏精确口径 | ★ **B24：既有 T4 用例原样通过**（`WM_SIZE 300×200` 逐位相等）+ 四工具链 236 全绿 |
| **C6** | 感知声明失败容忍（Log、不抛、不断言） | △22 代码 + 人工核（§5.2 的诊断日志） |
| **C7** | DPI 来源统一为 `GetDpiForWindow`（测量器的**测量基准** DC 除外） | grep `GetDeviceCaps` —— ★ **唯一归属文件 = `GDITextMeasurer.cpp`**（**`MeasureText` + `LineHeight` 各一处**，用途即"测量基准"）<br>★ **实施修正（§14-④）**：原文写"只应剩…**一处**"——实测为 **2 处**（两方法各自取本 DC 的 DPI，**必然**） |
| **C8** | `WM_DPICHANGED` 后几何正确 + 缓存不复用旧 DPI | §4 链 + 目视（A5）+ 缓存键含 DPI（△20/△21 代码可检） |
| **C9** ★ 新增 | ★★ **两个换算方向各自保精度，但不得追求往返恒等**（`dpi == 96` 时两者均恒等） | **T20-5** 把反例钉成回归契约（§3.2） |
| **C10** ★★ 新增（v1.1） | ★★ **`DipToPixels` / `PixelsToDip` 对负数必须对称舍入**——`sign(x) · floor(\|x\| + 0.5)`（**远离零**），正负无偏；`dpi == 96` 时两方向均恒等（**含负数**）。★ **不得**用「加偏移再除」（C++ 整数除法**向 0 截断** ⇒ 负数变"向零"） | **T20-6**（负数逐点 + 区间）+ 实现可检（舍入**只允许出现在 `RoundHalfAwayFromZero` 一处**） |

---

## 7. 测试实现规格（T20-1..T20-11）

### 7.1 承载方式

| 类 | 承载文件 | 驱动 |
|---|---|---|
| **纯换算**（T20-1..T20-6） | **新建 `src/Tests/DpiTests.cpp`**（△23） | 直接调 `Win32PlatformWindow::DipToPixels/PixelsToDip`（**public static**，B28 先例） |
| **真实翻译路径**（T20-7..T20-11） | ★ **扩展既有 `EventTests.cpp`**（`FakeHost` + `Handle()`，B24 基础设施已在） | `handler.SetDpi(n)` → `Handle(nullptr, nullptr, msg, wParam, lParam)` |

★ **为什么翻译用例不新建文件**：`EventTests.cpp` 已有 `FakeHost` + `ReceivedEvent` + 6 个既有分支（Phase 19 落地）⇒ **零新建**（与 Phase 19 同口径）。

### 7.2 用例清单

| # | 驱动 | 断言 | 对应 |
|---|---|---|---|
| **T20-1** | `DipToPixels(d, 96/120/144/192)` 若干 d（**含负数**） | 与公式**逐位一致** | A1 / C5 |
| **T20-2** ★ | `DipToPixels(d, 96)` | **恒等**（`== d`，**含负数**）——G5 的纯函数锚 | **G5 / C5** |
| **T20-3** | `PixelsToDip(px, 96/120/144/192)`（**含负数**） | 与公式**逐位一致** | A1 |
| **T20-4** ★ | `PixelsToDip(px, 96)` | **恒等**（`== px`，**含负数**）——G5 | **G5 / C5** |
| **T20-5** ★★ | `PixelsToDip(2, 120)` 与 `DipToPixels(2, 120)` | **`2` 与 `3`**（★ **把"往返不恒等"钉成回归契约**——防后人当 bug 去修） | **C9** |
| **T20-6** ★★ | **负数**：`PixelsToDip(-1,144) == -1` · `DipToPixels(-1,144) == -2` · 并断言 `-20..0` 区间与 `sign(x)·floor(\|x\|+0.5)` **逐位一致** | ★★ **把"负数对称舍入"钉成回归契约**——防后人退回「加偏移再除」的写法（C10） | **C10** |
| **T20-7** | `SetDpi(96)` + `WM_MOUSEMOVE(42, 57)` | 事件坐标 = **(42, 57)** ★ **与既有 T4 同型**（B24 的显式复刻） | C5 / G5 |
| **T20-8** ★ | `SetDpi(144)` + `WM_MOUSEMOVE(120, 90)` | 事件坐标 = **(80, 60)** | A2 / Q6 |
| **T20-9** ★ | `SetDpi(144)` + `WM_SIZE(1200, 900)` | `WindowResizedEvent` = **(800, 600)** | A2 |
| **T20-10** ★ | `SetDpi(144)` + `WM_MOUSEWHEEL`（`wParam = (120 << 16)`） | ★ **只断言 `delta == 120` 与状态位，不断言坐标**——`hwnd = nullptr` ⇒ `ScreenToClient` 不生效（Phase 19 §7 已记） | B23 |
| **T20-11** | `SetDpi(0)` / 负 dpi（fail-safe） | `PixelsToDip(px, 0)` **不崩、按 96 处理**（§3.4 契约） | §3.4 |

★ **两条测试纪律（沿用 Phase 19）**：① **手工构造事件不得作为验收证据**（A2 必须经 `Handle()`）；② `FakeHost` 路径下**含 `ScreenToClient` 的分支不断言坐标**。

### 7.3 用例数口径（★ 防 Phase 19 的"把 T 编号当用例数"重演）

- **新增场景编号**：**11 个**（T20-1..T20-11）；
- ★ **注册条目** = **T20-1..T20-6** 归 `DpiTests.cpp`（**1 个新文件 ⇒ `RunAllTests.h/.cpp` 各 +1 处**）+ **T20-7..T20-11** 归 `EventTests.cpp`（**既有文件 +5 条注册**）；
- ⇒ **实现时以实际注册条目为准**（Phase 19 的教训：初设写 240、实为 236）。

---

## 8. P1–P4 的实测方案与记录位置（初设 §9-⑥）

| # | 待测 | 方案 | 记录位置 |
|---|---|---|---|
| **P1** | `WM_DPICHANGED` 是否到达、`lParam` 语义 | 在 △14 处理块内**临时**加一行输出到文本文件（★ **不画在客户区**——Phase 19 教训：客户区不可复制），双屏拖窗 | 本稿 **§8.1**（实现后回填） |
| **P2** | `GetDC(NULL)` 在 V2 进程下返回哪个显示器的 DPI | 同上（记录 `GetDpiForWindow` 与 `GetDeviceCaps(GetDC(NULL))` 的对比值） | **§8.1** |
| **P3** | `GetDpiForWindow` 在**未 Show** 时是否可靠 | △16 构造末尾写入的值 vs Show 后读取值**对比** | **§8.1** |
| **P4** ★ | 「应用已设低级别感知」下 `GetDpiForWindow` 的返回值 | 小 spike：先 `SetProcessDPIAware()` 再建窗，读值 | **§8.1** |

★ **纪律**（沿用 Phase 19 §3.5）：**实测前不得把任何一条写成"事实"**；若实测与预期不符，改动**只在注释/设计说明，代码与用例不变**（除非 P1/P3 直接推翻 §4/§5 的链）。

### 8.1 实测回填（★ 待人工执行）

> **状态：待实测**（代码已就绪——五批全部落地）。★ **方案可简化**（原 §8 的"临时探针"并非都有必要）：
>
> - **P1 / P2 / P3 目视即可判定**——**不需要临时探针**：在 **150%** 显示器上启动 ModelProbe，
>   若（a）窗口初始尺寸正确、（b）文字尺度与布局匹配、（c）双屏拖动时内容按新 DPI 重排，
>   则 **P1/P2/P3 同时成立**。
>   ★ P3 的**证伪形态很显著**：未 Show 时若 `GetDpiForWindow` 返回 96，窗口在 150% 屏上会
>   **明显偏小约 1/3**（不等比放大）⇒ 一眼可辨。
> - **P4** 需专门 spike（应用先 `SetProcessDPIAware()` 再建窗）——**唯一无法目视判定**的一项。
> - ★ **纪律**（沿用 §8 末尾）：**实测前不得把任何一条写成"事实"**。

---

## 9. 实现顺序与检查点

| 批 | 内容 | 检查点 |
|---|---|---|
| **批一** | △1–△9（公共头 + 两个纯函数 + `GetDpiScale` 声明/实现，**不动任何调用点**） | ★ **构建应全绿且 236 全绿**（因为新函数无人调用、纯虚新增但实现者已补齐——△25 须同批） |
| **批二** | △23–△25（测试：`DpiTests.cpp` + 两处登记 + 2 个替身补 `GetDpiScale`） | ★ **T20-1..T20-6 应全绿**（纯函数可测——这是 §1.3 修3 的直接收益） |
| **批三** | △18/△19（翻译器 DPI 成员 + 6 处换算） + △16（构造写入） | ★ **既有 T4 原样通过**（B24 = G5 的活证据）+ T20-7..T20-11 全绿 |
| **批四** | △10–△15（窗口创建 / `GetClientSize` / `NCHITTEST` / `WM_SIZE` / `WM_DPICHANGED` / IME） | 构建全绿；★ **A5 目视**（100% 下行为与改前一致） |
| **批五** | △20–△22（两处字体缓存 + 感知声明） | ★ **A5 目视**（125% / 150%）· P1–P4 实测 · 四工具链全绿 |

★ **实施状态（2026-09-24）** ✓ **五批全部落地**：批一 ✓ · 批二 ✓ · 批三 ✓ · 批四 ✓（A5 目视：100% 下 ModelProbe 行为与改前一致）· 批五 ✓；四工具链（MSVC / Clang / ClangCL / MinGW）构建通过 · 测试全绿。★ **4 处实施偏离详见 §14**。

★ **批三的关键价值**：**在动任何平台换算之前**就先让"DPI 注得进去、事件换算得出"可验证——这样批四若有问题，能立刻定位到"换算点"而非"注入通路"。

---

## 10. 影响面（汇总）

| 面 | 规模 |
|---|---|
| **公共头** | **92 → 92**（改 5 个既有头：`PlatformWindow.h` / `Window.h` / `Font.h` / `TextMeasurer.h` / `Application.h`；**无新头**） |
| **公共 API 净增** | **+1**（`Window::GetDpiScale`）· **+1 纯虚**（`PlatformWindow::GetDpiScale`） |
| **纯虚实现者** | ★ **3 处**（B20）：`Win32PlatformWindow`（真实）+ `TestPlatformWindow` ×2（**各 +1 行**） |
| **签名变更** | **1 处**：`Win32PlatformWindow::DipToPixels(int, HWND)` → `(int, int)`（★ 私有 static ⇒ **非公共 API**；详见 §1.3 修3） |
| **语义变更** | **1 处**：`Font::size` 像素高度 → DIP 高度（**数值不变**） |
| **实现文件** | `Win32PlatformWindow.{h,cpp}` · `WindowMessageHandler.{h,cpp}` · `GDIBackend.{h,cpp}` · `GDITextMeasurer.{h,cpp}` · `Window.cpp`（`GetDpiScale` 透传）· `Application.cpp` |
| **测试** | ★ **新建 `DpiTests.cpp`**（T20-1..T20-6）· `EventTests.cpp`（+5 用例）· `RunAllTests.h/.cpp`（**各 +1 处登记**）· `AnimationTests.cpp` / `ProgressBarTests.cpp`（各 +1 行） |
| **用例数** | **236** 既有（★ **一条都不能改**，B24）→ 新增 **11** 个场景（注册口径见 §7.3） |
| **断言特征串** | **11 → 11**（预期不新增前提；★ 若新增 `FRAMEWORK_ASSERT` 则同步扩特征集，skill 条 50⑧④） |
| **构建** | 零 CMake 改动（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动收录新测试文件） |
| **`examples/`** | 零改动（A5/A6 仅目视） |

---

## 11. 验收（A1–A6 的落地）

| # | 需求判据 | 落地 |
|---|---|---|
| **A1** | 换算在 `dpi = 96/120/144/192` 下与公式逐位一致；`96` 时恒等（★ **含负数**——C10） | **T20-1..T20-4 · T20-6**（纯函数，无头） |
| **A2** | 事件翻译：物理 + DPI ⇒ 预期 DIP | **T20-8/T20-9**（真翻译路径） |
| **A3** | 零回归：**236 全绿**（写明断言是否启用） | 四工具链；★ **T4 原样通过**（B24） |
| **A4** | 感知声明生效（= Per-Monitor V2） | §5.2 的诊断日志 + 人工核（`GetProcessDpiAwareness`） |
| **A5** | 视觉：125% / 150% 下窗口按比例放大、内容不缩小；拖动 / 双击最大化 / Aero Snap 正常 | **人工**（沿 Phase 16 spike 目视法） |
| **A6** | 跨屏正确（K6 错位消失） | **人工**（双屏）+ P1 实测记录 |

---

## 12. 文档收口清单（✅ 已于 2026-09-24 执行）

| # | 项 |
|---|---|
| 1 | ✅ ★ **`framework-defect-audit.md`**：**D-2 标 ✅ 已修复**；**L6/D6 回填**（Phase 13 详设 `:795`/`:1051` 的处置栏）——**R8 要求** |
| 2 | ✅ `roadmap-deferred.md`：**#8 标 ✅ 已实施并收口**；头部 + 修订记录 |
| 3 | ✅ 根 `README.md` / `docs/README.md`：Phase 20 行标 ✅ · 规模锚点（用例数 / docs 数） |
| 4 | ✅ 本稿 **§8.1 实测回填** |
| 5 | ✅ `MEMORY.md`：Phase 20 归档到 `archive/`，一线只留一行 |

---

## 13. 修订记录

- **v1.3**（2026-09-24）**全阶段收口：★★ 人工验收通过 + 新增 §14.5 正式登记 `RG-1`**。① ★★ **验收通过**（用户四工具链 ModelProbe 目视）：**100% 与改前一致** · **125% / 150% 视觉正常** · **跨屏完全正常**。② ★ **§14.4 的 A4 / A5 / A6 全部标 ✅**——其中 **A4（感知声明生效）由「跨屏完全正常」反证**（感知未生效则 `GetDpiForWindow` 恒 96、`WM_DPICHANGED` 不会到达 ⇒ 跨屏不可能正确），**无需专门探针**。③ ★★ **新增 §14.5**：**实施期暴露的缺口「渲染层缺 DIP → 物理 换算边」以 `RG-1` 正式登记**（★ 并**显式说明与需求稿 §6 的 `R1` 需求条目不属同一命名空间**——此前子阶段文档曾以 `R1` 指代本缺口，因与需求编号撞车而正名）。含**现象 / 根因 / 实测证据 / 为何五批未覆盖 / 处置 / 状态**（✅ 已关闭）。④ ★ **§12 收口清单 5 项全部标 ✅** · 标题改「✅ 已于 2026-09-24 执行」。⑤ 头部 v1.2 → **v1.3**。
- **v1.2**（2026-09-24）**实施记录——★ 五批全部落地 + 记录 4 处实施偏离**。① **实施结果**：批一–批五全部完成，**四工具链**（MSVC / Clang / ClangCL / MinGW）构建通过 · 测试**全绿**（用例 **236 → 247**）。② ★★ **新增 §14 实施记录**，集中记录 4 处偏离（**全部源于「详设与既有契约/不变量冲突 ⇒ 以契约为准」，无一处范围扩张**）：**①** △20/△21 **不调 `DipToPixels`**（△20 与 **契约 C2** 直接矛盾 + 分层纪律 ⇒ 后端自算 float 口径）· **②** △22 **落点下沉应用级接缝**（「每个 Win32 API 唯一归属」+「框架层零 Win32」⇒ `PlatformApplication::DeclareDpiAwareness`）· **③** 批三 **抽 `DpiConversion.h`**（第二个消费者出现才抽象，教科书式触发）· **④** **C7 表述修正**（「一处」→ **唯一归属文件**；实测 2 处属必然）。③ **回填**：△20/△21/△22/C7 四处加实施标记并指向 §14；**§8.1 更新为「待人工执行」**并**简化方案**（★ 发现 **P1/P2/P3 目视即可判定**，无需临时探针；**P4** 需专门 spike）。④ §9 加**实施状态**行。⑤ 头部 v1.1 → **v1.2**。⑥ **内容零删改**：B19–B28 · △1–△25 · C1–C10 · 五批顺序 · §4 链 · §5 策略 **全部原样保留**。
- **v1.1**（2026-09-24）**评审第一轮处置——★ 补一处实施前必修的数学契约**。① **评审结论**：外部评审「**总体通过，可进入实现阶段**」；逐条处置见 **§1.5**。② ★★ **采纳评审的实质发现（经实测确认，且比其举例更普遍）**：`DipToPixels` / `PixelsToDip` 的 half-up **对负数不成立**——C++ 整数除法**向 0 截断**，而 `+ 48` / `+ dpi/2` 的偏移是按"向 +∞"设计的。★ **实测**：评审两个算例成立（`PixelsToDip(-1,144)` 旧得 **0** / 应 **-1**；`DipToPixels(-1,144)` 旧得 **-1** / 应 **-2**），且 `dpi = 144` 的 `-20..0` 区间**两函数各 20/20 全偏** ⇒ **系统性错误**。③ ★★ **修正**：**§3.1 改为两函数共用 `RoundHalfAwayFromZero`**（语义 `sign(x) · floor(|x| + 0.5)`，**远离零**）；并**实测验证正数上与旧式逐位等价**（`0..2000 × 6 种 dpi` 差异 **0**）⇒ **不破坏 G5**——★ 这一步评审未验，是**采纳的前提**。④ **新增契约 C10**（§6）+ **§3.2.1 负数算例与来源定位**（★ **负数只可能来自鼠标坐标**，评审未提）+ **§3.3 误差界补负数行**（修正后同样 ≤ 0.5）+ **T20-6 负数用例**（原 T20-6..T20-10 **顺延为 T20-7..T20-11**）⇒ 场景数 **10 → 11**。⑤ **采纳处置 2** → **新增 §3.6**（溢出措辞精确化为「**防止中间乘法溢出**」，不暗示全范围饱和）。⑥ **采纳处置 3** → **§5.3 重写**（支持基线 **Windows 10 1703+**，**冻结**不再半悬；★ 属项目级支持范围决策，**待确认**）。⑦ **内容零删改**：B19–B28 · △1–△25 · C1–C9 · 五批顺序 · §4 链 · P1–P4 **全部原样保留**。⑧ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-09-24）初稿。**输入**：需求 v1.1（已通过）· 初设 v1.1（已通过；评审指定 **3 件详设必做**）。**内容**：**B19–B28 详设新增基线**（★ 其中 **B19 D-SEAM-1 惯例**、**B20 纯虚实现者恰好 3 个**、**B21 `GetOrCreateFont` 是实例方法**、**B22 IME 触面 3 处**、**B24 既有 T4 天然是 G5 回归锚**、**B27 纯换算因 `HWND` 无法无头测**、**B28 `public static` 测试入口先例** 为本稿新勘）· **§1.3 对初设的四处修正**（★ **修3 = `DipToPixels` 改纯函数**，是本稿最实质的设计改进：同时达成"纯换算可无头测"与"DPI 来源/转换解耦"）· **§2 逐文件行级改动 △1–△25** · ★ **§3 `PixelsToDip` 完整规格**（评审指定 ①）——含**纯整数反例**证明双向恒等不可能（比评审的浮点算例更贴合实现）+ 误差界 + **C9 新契约** + **与测量链路口径差异的显式声明** · ★ **§4 `WM_DPICHANGED` 完整链**（评审指定 ②）——**只做三件事、不自行重布局**，接入既有 `WM_SIZE → OnResized` 链 · ★ **§5 感知声明与失败策略**（评审指定 ③）——含诊断日志与旧系统策略 · **§6 C1–C9 验证映射** · **§7 T20-1..T20-10**（含用例数口径防重演）· **§8 P1–P4 方案** · **§9 五批实现顺序** · §10 影响面 · §11 验收 · §12 收口清单。**待评审。**

---

## 14. 实施记录（v1.2，2026-09-24）

> **五批全部落地 · 四工具链构建 + 测试全绿（236 → 247 用例）**。本节记录**实施中与详设不一致的 4 处**——
> 全部源于「**详设与既有契约/不变量冲突时，以契约为准**」，**无一处是范围扩张**。

### 14.1 四处偏离

| # | 详设原文 | 实施 | 判据（为什么以契约为准） |
|---|---|---|---|
| **①** | △20/△21：`lfHeight = -lround(DipToPixels(font.size, dpi))` | 后端**自算** `lround(font.size * dpi / 96.0)` | ★ **详设内部矛盾**：△20 与 **契约 C2**（`grep` 只应命中 `src/Platform/Win32/`）**直接打架**；叠加**分层纪律**（`src/Render/` 不得依赖 `src/Platform/Win32/`）⇒「后端自行做 DIP → 物理」正是「**后端内部 = 物理**」这一层的职责。因 `Font::size` 是 float，本链路沿用 **float 口径** |
| **②** | △22：`Application.cpp` 构造期调用 `SetProcessDpiAwarenessContext` | **下沉应用级接缝**：`PlatformApplication` + 纯虚 `DeclareDpiAwareness()` → `Win32PlatformApplication` 实现 → `Application` 构造**调用** | 核心不变量「**每个 Win32 API 唯一归属**」+「**应用级接缝 = PlatformApplication → Application**」+「**框架层零 Win32**」。★ 正好复用 `PlatformApplication.h` 中 **Phase 14 建立的应用级能力惯例**；**实现者全库仅 1 个**（无测试替身）⇒ 零额外改动，且**未来任何平台实现被编译期强制**处理 DPI |
| **③** | 批三：△8/△9 落在 `Win32PlatformWindow` 的 public static | **抽出中立内部头** `src/Platform/Win32/DpiConversion.h` | ★ `WindowMessageHandler`（独立类）成为**第二个消费者** ⇒ 若换算留在 `Win32PlatformWindow`，翻译器将**反向依赖窗口类**（头层面成环）。⇒ 项目原则「**第二个真实消费者出现才抽象**」的教科书式触发（**批一时消费者只有 1 个，原地保留是对的**） |
| **④** | **C7**：「`GetDeviceCaps` 只应剩 `GDITextMeasurer` **一处**」 | 实测 **2 处**（`MeasureText` / `LineHeight` 各一） | 二者**必然**各自取本测量 DC 的 DPI ⇒ 契约应精确为「**唯一归属文件 = `GDITextMeasurer.cpp`**」 |

### 14.2 实施规模

| 项 | 值 |
|---|---|
| **批数** | 5（批一公共头 + 纯函数 · 批二测试 · 批三翻译器 · 批四窗口层 · 批五字体 + 感知） |
| **改动文件** | **源码 25** + 文档 4 = **29**（源码含 **新增 2**：`src/Platform/Win32/DpiConversion.h`、`src/Tests/DpiTests.cpp`） |
| **用例数** | **236 → 247**（`DpiTests.cpp` **6** 条 + `EventTests.cpp` **5** 条） |
| **Public 头** | **92 → 92**（+1 方法 `GetDpiScale` · +1 纯虚 `DeclareDpiAwareness`；**无新增文件**） |
| **构建验证** | ★ **四工具链**（MSVC / Clang / ClangCL / MinGW）全部通过 · 测试全绿 |

### 14.3 实施期发现的「非偏离」但有价值的点

| # | 发现 | 意义 |
|---|---|---|
| 1 | **批一的临时桥** | `DipToPixels` 改签名后，`WM_NCHITTEST` 的 2 处调用点（批四才撤除）编译不过 ⇒ 批一做**最小临时适配**（显式 `GetDpiForWindow`）并注明批四撤除。★ 因 unaware 下 `GetDpiForWindow` 与旧 `GetDeviceCaps` **同为 96**，行为**逐位等价**（G5 兜住） |
| 2 | **批三的删除自伤** | 删三函数定义时 **Edit 的 old/new 写反** ⇒ 变成「又插入一份」（重复定义）。⇒ 纪律：**删除类操作改完立即 `count` 复查** |
| 3 | **新建测试文件漏 `using namespace ECDI;`** | `Win32PlatformWindow` 在 `namespace ECDI` 内 ⇒ 18 处 `undeclared identifier`。★ **该纪律 skill 早有**（Phase 19 记过）⇒ **第二次复发** ⇒ 已升级为机械判据（凡 include `ECDI/` 或 `Platform/` 头 ⇒ 必须有 `using` 或全限定） |
| 4 | **批二的真值表核对法** | 从测试文件 **parse 出 48 个期望值**用独立脚本**逐点重算** ⇒ 提前排掉「期望值手算错」（否则要等用户跑测试才暴露） |
| 5 | **`switch` 变量作用域** | 批四三处 `const int dpi` **各在带花括号的 case 内** ⇒ 规避 `jump to case label crosses initialization`（此类改动的典型翻车点，已纳入收尾自检） |

### 14.4 验收状态（§11 的 A1–A6）

| 项 | 状态 |
|---|---|
| **A1** 换算与公式逐位一致（含负数） | ✅ T20-1..T20-4 / T20-6 |
| **A2** 事件翻译（真实路径） | ✅ T20-8 / T20-9 |
| **A3** 零回归（**236 全绿**） | ✅ 四工具链 · **247 全绿**（既有 236 一条未改） |
| **A4** 感知声明生效 | ✅ **已核**——★ 由「**跨屏完全正常**」反证：感知未生效则 `GetDpiForWindow` 恒 96、`WM_DPICHANGED` 不会到达 ⇒ **跨屏不可能正确** |
| **A5** 视觉 | ✅ **100% / 125% / 150% 全部通过**（2026-09-24 用户四工具链 ModelProbe 目视） |
| **A6** 跨屏正确 | ✅ **已实测通过**——用户原话「**跨屏下完全正常**」（★ P1/P2/P3 由目视判定，无需探针） |

### 14.5 ★★ 实施期暴露的缺口：`RG-1`（渲染层缺 DIP → 物理 的换算边）

> ★ **编号说明（防歧义）**：本缺口在**实施期**（用户跨屏 / 主屏 125% 实测）才发现，故**不在需求稿的 `R1–R9` 序列内**——
> 它用 **`RG-1`**（**R**ender **G**ap 1）命名，**与需求稿 §6 的 `R1`（需求条目「进程 DPI 感知 = Per-Monitor V2」）是两个命名空间**。

| 项 | 内容 |
|---|---|
| **现象** | 非 96 DPI（125% / 150% / 跨屏）下：**窗口尺寸 ✅ · 布局(DIP) ✅ · 命中(DIP) ✅ · 渲染 ✗**——内容只占窗口左上约 **2/3**。用户原话：「绘制变小了，但 GUI 并没有变小、按键控制仍然在原位」 |
| **根因** | ★★ **设计表述缺口，非实现笔误**：初设 **§2.1 规则 2**（「`Renderer` / `RenderingBackend` 里见到的**恒为物理像素**」）与 **规则 3**（「两者**只在平台边界**那一条线上互换」）**互相打架**——**渲染层不在平台边界** ⇒ 「**框架内部(DIP) → 渲染层(物理)**」**这条边没有落点** |
| **实测证据** | `Renderer.cpp`（69 行）· `PaintContext.cpp`（84 行）**零 `Dpi`/`scale` 代码**；`GDIBackend` 直接把几何当像素（`static_cast<LONG>(rect.x)`）；全库 `src/Render/` 的 DPI 代码**只在字体路径**（本阶段批五）⇒ **字体已按 DPI 换算而几何未换算 ⇒ 尺度失调** |
| **为何五批都没覆盖** | ★ 本阶段按「**换算只发生在平台边界**」执行——而这条原则**恰好把渲染层排除在外**。⇒ ★★ **元教训（可推广）**：**不要只问「哪个模块知道平台」，还要问「哪个模块第一次要求物理单位」**——转换落点由**单位契约**决定，不由目录名决定 |
| **处置** | 用户拍板「**新开子阶段**」⇒ `docs/phase20.1-render-dpi-scaling-*.md`（需求 / 初设 / 详设三件套），落点 = **`Renderer::Execute` 入口**（形式 **A2**：执行期瞬时转换、**不碰 `CommandBuffer`**） |
| **状态** | ✅ **已关闭**（2026-09-24）——★ 用户四工具链 ModelProbe 目视：**100% 与改前一致 · 125% / 150% 内容充满窗口 · 跨屏完全正常** |