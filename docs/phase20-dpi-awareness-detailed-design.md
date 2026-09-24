# Phase 20 · DPI 感知（DPI awareness）—— 详细设计

> 状态：**v1.0**（2026-09-24）——**待评审**
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
| **△20** | `GDIBackend.h:61` + `.cpp:759-793` | `GetOrCreateFont(font)` → **加 DPI**（`GetDpiForWindow(m_hwnd)`）；`lfHeight = -lround(DipToPixels(font.size, dpi))`；**缓存键加 DPI**（`GDIBackend.h:76` 的 `map<pair<float,string>, HFONT>` → 键含 `int dpi`） |
| **△21** | `GDITextMeasurer.h:30` + `.cpp:20-55` | 同上；★ **基准 DPI = 它自己的测量 DC 的 `LOGPIXELSX`**（初设 §2.2.4：**进出一致** ⇒ 不变更"零 hwnd"设计）；`MeasureText`/`LineHeight` 的返回值**折回 DIP**（`px * 96.0f / dpi`，**保留 float**——此处是测量链路，与 `PixelsToDip` 的整数口径**不同**，见 §3.5） |

### 2.4 应用层（△22）

| △ | 文件 | 改动 |
|---|---|---|
| **△22** | `Application.cpp`（构造） | ★ **评审指定 ③**：构造期调用 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`，**失败容忍**（`Log(Warning)`、不抛、不断言）+ **记录实际感知级别**（规格见 **§5**） |

### 2.5 测试（△23–△25 + `GetDpiScale` 实现者清单）

| △ | 文件 | 改动 |
|---|---|---|
| **△23** | **新建** `src/Tests/DpiTests.cpp` | 承载 T20-1..T20-10（**§7**）。★ **须 include `Platform/Win32/Win32PlatformWindow.h`**（纯换算经 public static 调用——B28 先例） |
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

**公式（对称 half-up）**：

```cpp
// 正向（保留既有公式，仅去掉 HWND——overflow 防护照旧：中间量升 long long）
const long long scaled = static_cast<long long>(dip) * dpi;
return static_cast<int>((scaled + 48) / 96);

// 反向（新增，同风格）
const long long scaled = static_cast<long long>(px) * 96;
return static_cast<int>((scaled + dpi / 2) / dpi);
```

★ **`dpi / 2` 说明**：`dpi` 通常为 96 / 120 / 144 / 192（偶数）⇒ 精确 half-up；非常规 DPI（如 105）时 `dpi / 2 = 52` ⇒ **近似 half-up**，误差 < 0.5 px（**可接受，且不影响 G5**）。

### 3.2 ★★ 「双向恒等在数学上不可能」——**纯整数反例**

初设 O3 已采纳评审该论断，但评审给的是**浮点**算例（`PixelsToDip(2) = 1.333…`）——那只是"若不取整会怎样"。**本稿给出纯整数实现下的反例**（更贴合实际代码）：

| dpi | 计算 | 结果 |
|---|---|---|
| **120** | `PixelsToDip(2, 120) = (2·96 + 60) / 120 = 252 / 120` | **2** |
| **120** | `DipToPixels(2, 120) = (2·120 + 48) / 96 = 288 / 96` | **3** |
| ⇒ | **`px → dip → px`：`2 → 2 → 3 ≠ 2`** | ✗ **不恒等** |

**根因（一句话）**：`dpi > 96` 时**物理像素网格比 DIP 网格更密** ⇒ **多个物理像素映射到同一个 DIP** ⇒ 反向放大**无法还原**（信息已丢失）。

⇒ ★★ **契约 C9（新增）**：**两个方向各自保精度，但不得追求往返恒等**；`dpi == 96` 时**两个方向都恒等**（G5）。**详设/实现/评审均不得把"往返不恒等"当 bug 去修。**

### 3.3 误差界

| 方向 | 单次转换的最大误差 |
|---|---|
| `DipToPixels` | ≤ 0.5 px（half-up 固有） |
| `PixelsToDip` | ≤ 0.5 DIP（half-up 固有） |
| ★ 组合（一次往返） | **可 > 0.5**（§3.2 的反例即 1 px 级）——这正是**不得追求恒等**的原因 |

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

### 5.3 旧系统兼容（O4）

| 层 | 策略 |
|---|---|
| **API** | `SetProcessDpiAwarenessContext` 需 **Win10 1703+**；★ **倾向不引入运行时 `GetProcAddress` 动态加载**（本项目工具链与目标系统已在该门槛之上；YAGNI），**失败容忍已覆盖"函数缺失"以外的全部路径**。⇒ ★ **待评审确认**：若不接受，则须补 `LoadLibrary`/`GetProcAddress` 兜底 |
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
| **C7** | DPI 来源统一为 `GetDpiForWindow`（测量器的**测量基准** DC 除外） | grep `GetDeviceCaps` —— 只应剩 `GDITextMeasurer` 一处（且用途已注释为"测量基准"） |
| **C8** | `WM_DPICHANGED` 后几何正确 + 缓存不复用旧 DPI | §4 链 + 目视（A5）+ 缓存键含 DPI（△20/△21 代码可检） |
| **C9** ★ 新增 | ★★ **两个换算方向各自保精度，但不得追求往返恒等**（`dpi == 96` 时两者均恒等） | **T20-5** 把反例钉成回归契约（§3.2） |

---

## 7. 测试实现规格（T20-1..T20-10）

### 7.1 承载方式

| 类 | 承载文件 | 驱动 |
|---|---|---|
| **纯换算**（T20-1..T20-5） | **新建 `src/Tests/DpiTests.cpp`**（△23） | 直接调 `Win32PlatformWindow::DipToPixels/PixelsToDip`（**public static**，B28 先例） |
| **真实翻译路径**（T20-6..T20-10） | ★ **扩展既有 `EventTests.cpp`**（`FakeHost` + `Handle()`，B24 基础设施已在） | `handler.SetDpi(n)` → `Handle(nullptr, nullptr, msg, wParam, lParam)` |

★ **为什么翻译用例不新建文件**：`EventTests.cpp` 已有 `FakeHost` + `ReceivedEvent` + 6 个既有分支（Phase 19 落地）⇒ **零新建**（与 Phase 19 同口径）。

### 7.2 用例清单

| # | 驱动 | 断言 | 对应 |
|---|---|---|---|
| **T20-1** | `DipToPixels(d, 96/120/144/192)` 若干 d | 与公式**逐位一致** | A1 / C5 |
| **T20-2** ★ | `DipToPixels(d, 96)` | **恒等**（`== d`）——G5 的纯函数锚 | **G5 / C5** |
| **T20-3** | `PixelsToDip(px, 96/120/144/192)` | 与公式**逐位一致** | A1 |
| **T20-4** ★ | `PixelsToDip(px, 96)` | **恒等**（`== px`）——G5 | **G5 / C5** |
| **T20-5** ★★ | `PixelsToDip(2, 120)` 与 `DipToPixels(2, 120)` | **`2` 与 `3`**（★ **把"往返不恒等"钉成回归契约**——防后人当 bug 修） | **C9** |
| **T20-6** | `SetDpi(96)` + `WM_MOUSEMOVE(42, 57)` | 事件坐标 = **(42, 57)** ★ **与既有 T4 同型**（B24 的显式复刻） | C5 / G5 |
| **T20-7** ★ | `SetDpi(144)` + `WM_MOUSEMOVE(120, 90)` | 事件坐标 = **(80, 60)** | A2 / Q6 |
| **T20-8** ★ | `SetDpi(144)` + `WM_SIZE(1200, 900)` | `WindowResizedEvent` = **(800, 600)** | A2 |
| **T20-9** ★ | `SetDpi(144)` + `WM_MOUSEWHEEL`（`wParam = (120 << 16)`) | ★ **只断言 `delta == 120` 与状态位，不断言坐标**——`hwnd = nullptr` ⇒ `ScreenToClient` 不生效（Phase 19 §7 已记） | B23 |
| **T20-10** | `SetDpi(0)` / 负值（fail-safe） | `PixelsToDip(px, 0)` **不崩、按 96 处理**（§3.4 契约） | §3.4 |

★ **两条测试纪律（沿用 Phase 19）**：① **手工构造事件不得作为验收证据**（A2 必须经 `Handle()`）；② `FakeHost` 路径下**含 `ScreenToClient` 的分支不断言坐标**。

### 7.3 用例数口径（★ 防 Phase 19 的"把 T 编号当用例数"重演）

- **新增场景编号**：10 个（T20-1..T20-10）；
- ★ **注册条目** = T20-1..T20-5 归 `DpiTests.cpp`（**1 个新文件 ⇒ `RunAllTests.h/.cpp` 各 +1 处**）+ T20-6..T20-10 归 `EventTests.cpp`（**既有文件 +5 条注册**）；
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

### 8.1 实测回填（实现后填写）

> （待实现后回填）

---

## 9. 实现顺序与检查点

| 批 | 内容 | 检查点 |
|---|---|---|
| **批一** | △1–△9（公共头 + 两个纯函数 + `GetDpiScale` 声明/实现，**不动任何调用点**） | ★ **构建应全绿且 236 全绿**（因为新函数无人调用、纯虚新增但实现者已补齐——△25 须同批） |
| **批二** | △23–△25（测试：`DpiTests.cpp` + 两处登记 + 2 个替身补 `GetDpiScale`） | ★ **T20-1..T20-5 应全绿**（纯函数可测——这是 §1.3 修3 的直接收益） |
| **批三** | △18/△19（翻译器 DPI 成员 + 6 处换算） + △16（构造写入） | ★ **既有 T4 原样通过**（B24 = G5 的活证据）+ T20-6..T20-10 全绿 |
| **批四** | △10–△15（窗口创建 / `GetClientSize` / `NCHITTEST` / `WM_SIZE` / `WM_DPICHANGED` / IME） | 构建全绿；★ **A5 目视**（100% 下行为与改前一致） |
| **批五** | △20–△22（两处字体缓存 + 感知声明） | ★ **A5 目视**（125% / 150%）· P1–P4 实测 · 四工具链全绿 |

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
| **测试** | ★ **新建 `DpiTests.cpp`**（T20-1..T20-5）· `EventTests.cpp`（+5 用例）· `RunAllTests.h/.cpp`（**各 +1 处登记**）· `AnimationTests.cpp` / `ProgressBarTests.cpp`（各 +1 行） |
| **用例数** | **236** 既有（★ **一条都不能改**，B24）→ 新增 10 个场景（注册口径见 §7.3） |
| **断言特征串** | **11 → 11**（预期不新增前提；★ 若新增 `FRAMEWORK_ASSERT` 则同步扩特征集，skill 条 50⑧④） |
| **构建** | 零 CMake 改动（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动收录新测试文件） |
| **`examples/`** | 零改动（A5/A6 仅目视） |

---

## 11. 验收（A1–A6 的落地）

| # | 需求判据 | 落地 |
|---|---|---|
| **A1** | 换算在 `dpi = 96/120/144/192` 下与公式逐位一致；`96` 时恒等 | **T20-1..T20-4**（纯函数，无头） |
| **A2** | 事件翻译：物理 + DPI ⇒ 预期 DIP | **T20-7/T20-8**（真翻译路径） |
| **A3** | 零回归：**236 全绿**（写明断言是否启用） | 四工具链；★ **T4 原样通过**（B24） |
| **A4** | 感知声明生效（= Per-Monitor V2） | §5.2 的诊断日志 + 人工核（`GetProcessDpiAwareness`） |
| **A5** | 视觉：125% / 150% 下窗口按比例放大、内容不缩小；拖动 / 双击最大化 / Aero Snap 正常 | **人工**（沿 Phase 16 spike 目视法） |
| **A6** | 跨屏正确（K6 错位消失） | **人工**（双屏）+ P1 实测记录 |

---

## 12. 文档收口清单（实现后执行）

| # | 项 |
|---|---|
| 1 | ★ **`framework-defect-audit.md`**：**D-2 标 ✅ 已修复**；**L6/D6 回填**（Phase 13 详设 `:795`/`:1051` 的处置栏）——**R8 要求** |
| 2 | `roadmap-deferred.md`：**#8 标 ✅ 已实施并收口**；头部 + 修订记录 |
| 3 | 根 `README.md` / `docs/README.md`：Phase 20 行标 ✅ · 规模锚点（用例数 / docs 数） |
| 4 | 本稿 **§8.1 实测回填** |
| 5 | `MEMORY.md`：Phase 20 归档到 `archive/`，一线只留一行 |

---

## 13. 修订记录

- **v1.0**（2026-09-24）初稿。**输入**：需求 v1.1（已通过）· 初设 v1.1（已通过；评审指定 **3 件详设必做**）。**内容**：**B19–B28 详设新增基线**（★ 其中 **B19 D-SEAM-1 惯例**、**B20 纯虚实现者恰好 3 个**、**B21 `GetOrCreateFont` 是实例方法**、**B22 IME 触面 3 处**、**B24 既有 T4 天然是 G5 回归锚**、**B27 纯换算因 `HWND` 无法无头测**、**B28 `public static` 测试入口先例** 为本稿新勘）· **§1.3 对初设的四处修正**（★ **修3 = `DipToPixels` 改纯函数**，是本稿最实质的设计改进：同时达成"纯换算可无头测"与"DPI 来源/转换解耦"）· **§2 逐文件行级改动 △1–△25** · ★ **§3 `PixelsToDip` 完整规格**（评审指定 ①）——含**纯整数反例**证明双向恒等不可能（比评审的浮点算例更贴合实现）+ 误差界 + **C9 新契约** + **与测量链路口径差异的显式声明** · ★ **§4 `WM_DPICHANGED` 完整链**（评审指定 ②）——**只做三件事、不自行重布局**，接入既有 `WM_SIZE → OnResized` 链 · ★ **§5 感知声明与失败策略**（评审指定 ③）——含诊断日志与旧系统策略 · **§6 C1–C9 验证映射** · **§7 T20-1..T20-10**（含用例数口径防重演）· **§8 P1–P4 方案** · **§9 五批实现顺序** · §10 影响面 · §11 验收 · §12 收口清单。**待评审。**
