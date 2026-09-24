# Phase 20 · DPI 感知（DPI awareness）—— 初步设计

> 状态：**v1.0**（2026-09-24）——**待评审**
> 来源：需求稿 **v1.1**（外部评审第一轮通过 2026-09-24：「需求边界成立，Q1–Q6 已正确归类为初设决策」）· `roadmap-deferred.md` **§7.9 #8** · 审计 `docs/framework-defect-audit.md` **§4 D-2**
> 定性：**让既有 DIP 契约生效**——不是引入新概念（契约早在 Phase 13 立下，因未声明感知而从未被检验）
> 本稿任务：回答需求 §8 的 **Q1–Q6**，并把 **D0–D8 / 路线 B** 从"倾向"转为**定案**

---

## 1. 设计输入与基线

### 1.1 需求阶段已定（本稿不再讨论）

| 项 | 内容 |
|---|---|
| **G1** | 进程 DPI 感知 = **Per-Monitor V2** |
| **G2** | 既有 DIP 契约**贯彻到全部平台边界**（把 K1 的「换算点唯一」从 NCHITTEST 扩展到全部边界） |
| **G3** | 跨屏一致性（含 `WM_DPICHANGED`） |
| **G4** | 补查询能力——★ **本稿对其范围做一次收敛**（见 §2.4） |
| **G5** | ★ **零回归护栏**：`dpi == 96 ⇒ px == dip` ⇒ 100% 下**逐位等价** |
| **N1–N5** | 非目标：矢量字体缩放 / 响应式布局 / 非 Windows DPI 抽象 / 重构 `ScrollBar`-`TextBox` / 每屏自定义缩放 |
| **路线 B** | 声明 V2 + DIP 贯通（已否决 A「保持 unaware」与 C「只在 chrome 生效」） |
| **§3.2 连锁** | **感知声明与 DIP 语义贯通必须同批**——单独声明 = 150% 屏上窗口/字体缩小 1/3（纯破坏性） |
| **§4.1 双空间共存** | **同一个 `GetClientRect`：平台侧要 DIP、后端要物理**；**不得合并成一个尺寸访问器** |
| **R3** | ★★ **架构约束**（地位高于 G1）：换算**只发生在平台边界**；框架内部不感知 DPI |
| **D0** | 一批做完（测试与收口可分步） |

### 1.2 代码基线（B1–B18，2026-09-24 逐条实测）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | `Font::size` 是 **float**，默认 `14.0f`；头注释明写语义 =「**像素高度**」（第一版） | `Core/Font.h:12` · `:16` |
| **B2** | `DipToPixels(int dip, HWND)`：DPI 来源 = **`GetDeviceCaps(hdc, LOGPIXELSX)`（窗口 DC）**；公式 **`(dip * dpi + 48) / 96`**（整数 half-up，中间量升 `long long` 防溢出）；★ **DC 获取失败 ⇒ 返回 `dip`（1:1 降级）**；注释已自陈'未声明 PerMonitorV2 ⇒ 虚拟化下 dpi 恒 96'并**预留替换点** | `Win32PlatformWindow.cpp:1322-1347`（公式 `:1343-1345` · 降级 `:1333-1337` · 注释 `:1324-1330`） |
| **B3** | `DipToPixels` **只有 2 个调用点**（均在 `WM_NCHITTEST` 内） | `:377`（`m_resizeInset`）· `:379`（`m_captionHeight`） |
| **B4** | ★ `WM_NCHITTEST` 现状：`pt` 屏幕坐标 → **窗口物理坐标** `x`/`y`（`:372-374`）→ `inset`/`caption` 转物理（`:377`/`:379`）→ **`w`/`h` 取自 `GetWindowRect`（物理）**（`:381-383`）→ **`m_host.IsClientInteractiveAt(x, y)` 收到的是物理 x/y**（`:423`）⇒ 与 `HitTest` 的 DIP 几何错位（= Phase 13 的 **L6/D6**） | `:356-435` |
| **B5** | ★★ **客户区尺寸有 3 个入口**（需求 K13）：① **构造期** `m_platformWindow->GetClientSize()`（内部 `GetClientRect`）→ `RootWidget::SetSize`；② **`WM_SIZE`** → `m_host.OnResized(LOWORD, HIWORD)` → **同步 RootWidget 尺寸 + 重排布局链 + 重绘**；③ **`WM_SIZE`** → `WindowResizedEvent` | ① `Win32PlatformWindow.cpp:266-270` → `Window.cpp:73-78`；② `:521` → `Window.cpp:449-452`；③ `WindowMessageHandler.cpp:106-108` |
| **B6** | ★★ **字体缓存有两份，键均为 `std::map<std::pair<float, std::string>, HFONT>` = `(size, family)`——不含 DPI** | `GDIBackend.h:76` · `GDITextMeasurer.h:32` |
| **B7** | 两处 `GetOrCreateFont` **同逻辑**：`lfHeight = -lround(font.size)`（负值 = 字符高度），`CreateFontIndirectW`；注释自称「未来加字段同步扩展键」 | `GDIBackend.cpp:759-793` · `GDITextMeasurer.cpp:20-55` |
| **B8** | ★★ `GDITextMeasurer` 的设计声明：**「纯测量零 hwnd：`GetDC(NULL)` 临时屏幕 DC（帧无关——测量不依赖窗口）」** | `GDITextMeasurer.h:16` · 实现 `GDITextMeasurer.cpp:62` · `:95` |
| **B9** | ★ `TextMeasurer` 接口**不带 hwnd / DPI 参数**：`Size MeasureText(const Font&, const std::string&)` · `float LineHeight(const Font&)` | `TextMeasurer.h:19` · `:22` |
| **B10** | 渲染后端**已在物理像素上工作**：`BeginPaint` 取帧 DC、`GetClientRect` 定缓冲尺寸、DIB 按客户区（物理）重建 | `GDIBackend.cpp:252` · `:259` · `:276-278` |
| **B11** | `Application` 是**默认构造**；`Create(title, w, h, RenderServices = CreateDefaultRenderServices())` 是**成员函数**（Phase 18 起透出注入形参） | `Application.h:46` · `:63-64` |
| **B12** | `Application::Create` 实现：`new Window(...)` → `m_windows.emplace_back` → 派发 `WindowCreatedEvent` | `Application.cpp:58-75` |
| **B13** | ★ `Win32PlatformWindow` 构造内 **`CreateWindowExW(..., width, height, ...)` 直传、零换算** | `:114-128`（直传在 `:121-122`） |
| **B14** | `WindowResizedEvent` 宽度取 **`LOWORD/HIWORD(lParam)`**；`WM_NCHITTEST` 的 caption 判定带 **`caption > 0`** 门控 | `WindowMessageHandler.cpp:106-108` · `Win32PlatformWindow.cpp:417` |
| **B15** | `WindowMessageHandler` **构造只收 `PlatformWindowHost&`**；`Handle(Window*, HWND, UINT, WPARAM, LPARAM)`——★ **DPI 无来源** | `WindowMessageHandler.h:32` · `:36-42` |
| **B16** | ★★ 无窗口翻译测试的装置**已在**：`FakeHost : PlatformWindowHost`（`GetWindow()` 返回 `nullptr`、`OnResized(int,int)` 空实现）+ `handler.Handle(nullptr, nullptr, msg, wParam, lParam)` 直驱（含既有 `WM_SIZE` 用例） | `EventTests.cpp:73-85` · `:258-259` |
| **B17** | ★ 测量的**消费者面** = `GetTextMeasurer()` 共 **7 处**（`TextBox` 6 · `TextWidget` 1），其返回值**直接进布局与光标计算** | `TextBox.cpp:382` · `:430` · `:494` · `:586` · `:609` · `:1186` · `TextWidget.cpp:131` |
| **B18** | IME 双通道用**框架坐标**：`SetCaretPos(geometry.rect.x, …)` + `COMPOSITIONFORM` / `CANDIDATEFORM` | `Win32PlatformWindow.cpp:683` · `:708-714` · `:718` |

### 1.3 需求 Q1–Q6 → 本稿答案索引

| Q | 问题 | 答案 |
|---|---|---|
| **Q1** | DIP 语义的边界画在哪 | **§2.1**（三层单位表 + 可判规则 + 唯一例外） |
| **Q2** | ★ 文本测量的单位 | **§2.2**（**返回 DIP**；含自洽性论证、三条代价、测量基准 DPI 的处置） |
| **Q3** | ★ 感知声明的落点 | **§2.3**（**运行时 API @ `Application` 构造**为主 + manifest 片段附带） |
| **Q4** | ★ 查询 API 的形状 | **§2.4**（**只加 `Window::GetDpiScale()`**；屏幕枚举记账——**G4 范围收敛**） |
| **Q5** | `WM_NCHITTEST` 的换算方向 | **§2.5**（**定案 A：入参换成 DIP**；判据 = `HitTest` 入参语义恒为 DIP） |
| **Q6** | ★ 测试如何伪造 DPI | **§2.6**（**DPI 是翻译器的正常成员状态**，非测试缝；两条路都要验） |

---

## 2. ★ 核心定案

### 2.1 Q1：DIP 语义的边界

**定案：一个量的单位由「它跨越哪条线」决定，而不是由它的类型决定。**

| 层 | 单位 | 覆盖的量 |
|---|---|---|
| **公共 API** | **DIP** | `Application::Create(title, w, h)` 的 w/h · `Widget::SetSize` / `Layout` 的几何 · 事件坐标（`GetMouseX/Y`、`WindowResizedEvent`）· **`Font::size`**（D4）· **`TextMeasurer` 的返回值**（Q2） |
| **平台边界内部** | **物理** | `CreateWindowExW` 的实参（B13）· `GetClientRect` 的返回值（B5 ①）· `SetCaretPos` / `COMPOSITIONFORM` 的坐标（B18）· `ScreenToClient` 的输出 |
| **渲染后端内部** | **物理** | DIB 尺寸 · `lfHeight` · 字形栅格化（B10） |

★ **唯一例外（须显式声明）**：`PlatformWindow::GetClientSize()` 的**返回值是 DIP**，但其**实现内部**的 `GetClientRect` 是物理——**"接口返回值"与"实现内部"分属两层**。这正是需求 §4.1「双空间共存」的操作含义：**两者不得合并成一个尺寸访问器**。

**可判规则（评审时可直接用）**：
1. `Window` / `Widget` / `Layout` / 事件里见到的尺寸与坐标**恒为 DIP**；
2. `Renderer` / `RenderingBackend` 里见到的**恒为物理像素**；
3. 两者只在**平台边界**那一条线上互换（R3）。

⇒ ★ **本阶段不新增任何"物理量"公共 API**——`PixelsToDip` 是平台层内部函数（匿名 namespace），不进公共头。

### 2.2 Q2：文本测量的单位 ★

**定案：`TextMeasurer::MeasureText` / `LineHeight` 一律返回 DIP。**

#### 2.2.1 定案后的测量链路

```text
MeasureText(font /*size = DIP*/, text)
  → 取「基准 DPI」= 本实现测量 DC 的 LOGPIXELSX
  → 物理高度 px = DipToPixels(font.size, dpi)
  → HFONT{ lfHeight = -px }
  → GDI 测得宽/高（物理像素）
  → 折回 DIP：dip = px * 96 / dpi
  → 返回 Size（DIP）
```

#### 2.2.2 ★ 自洽性论证（为什么"返回 DIP"不会与渲染打架）

设窗口位于 **150%** 屏、主屏为 **100%**：

| 环节 | 字体物理高度 | 结果 |
|---|---|---|
| **测量**（屏幕 DC，100%） | `lfHeight = -14` | 测宽 `W1` px ⇒ 返回 **`W1` DIP** |
| **渲染**（窗口 DC，150%） | `lfHeight = -21` | 绘制宽 `W2 ≈ 1.5 · W1` px ⇒ 折算 **`W1` DIP** |

⇒ **两端折算回 DIP 后一致**（字形等比缩放前提下）。这条论证是 Q2 选 DIP 的**关键前提**：测量链路**自己进、自己出**用同一个 DPI，因此**返回值对基准 DPI 的选择近似不敏感**。

#### 2.2.3 代价（须显式记账）

| # | 代价 | 程度 |
|---|---|---|
| 1 | 返回浮点 | **无新增**——接口本就是 `float`（B9）；现状亦然 |
| 2 | ★ **亚像素舍入**：GDI 按物理像素栅格化，折回 DIP 后有 ≤0.5px 级差异；150% 下相对误差约 0.33% | **可接受**——与现状同量级（现状亦有整数舍入） |
| 3 | ★★ **测量基准 DPI 与渲染 DPI 不保证相同**（B8：`GDITextMeasurer` 用屏幕 DC，而渲染用窗口 DC）⇒ **跨屏时两者有亚像素级偏差** | **已知近似**，处置见下 |

#### 2.2.4 「测量基准 DPI」的处置（本稿定案）

| 案 | 判定 |
|---|---|
| **① 不改 `TextMeasurer` 接口**（B9：加 DPI/hwnd 参数 = **公共 API 破坏** + B17 的 7 处消费者要动） | ✅ **采纳**——`GDITextMeasurer` 维持 B8 的「零 hwnd + 屏幕 DC」设计，基准 DPI 由**它自己的测量 DC** 决定 ⇒ **进出一致**（§2.2.2） |
| ② `GDITextMeasurer` 改为持有可注入的 DPI 基准 | ⏸️ **记账**（O1）——触发条件：出现**跨屏布局错位的真实目视报告** |

★ **G5 保证**：`dpi == 96` 时 `DipToPixels` 恒等、折回亦恒等 ⇒ **100% 下测量值与现状逐位相同**。

### 2.3 Q3：感知声明的落点 ★

**定案：主路线 = 运行时 API，落点 = `Application` 构造函数；manifest 片段附带提供。**

| 案 | 内容 | 判定 |
|---|---|---|
| **① 运行时 API** | `Application::Application()` 内调用 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` | ✅ **主路线** |
| **② manifest / CMake 注入** | 提供 `.manifest` 片段供应用嵌入 | ✅ **附带提供**（不强制） |

**为什么落在 `Application` 构造**（而不是 `Create`）：
1. R1 要求「声明时机**早于任何窗口创建**」——窗口只在 `Application::Create` 内建（B12/B13）；
2. `Application` 是用户**第一个构造**的框架对象（B11 默认构造）⇒ 构造函数比任何 `Create` 都早；
3. **零应用改动**——不需要用户在 `main` 里手工调用平台 API（教学型框架：命名即语义，用户不该知道 `SetProcessDpiAwarenessContext` 这个名字）。

★ **为何两案并存**：运行时 API 覆盖常态；manifest 覆盖两种例外——「应用要自己控制感知级别」与「需要在 `Application` 构造之前就声明」（例如应用在构造前已自建窗口）。

**失败容忍（定案，见契约 C6）**：
- 返回 `FALSE` ⇒ 两种成因：**本进程已被声明过**（多 `Application` 实例 / 应用自己先设了）或 **系统不支持**；
- ⇒ **记 `Log(Warning)` 并继续**，**不得抛异常、不得 `FRAMEWORK_ASSERT`**（依据条 31：契约不得建立在未证实的平台行为上；沿用决策 30「失败 ⇒ 局部跳过」风格）。

### 2.4 Q4：查询 API 的形状（★ 含 G4 的范围收敛）

**定案：只加 `Window::GetDpiScale()`；屏幕枚举不提供，记账。**

| 项 | 决定 | 理由 |
|---|---|---|
| **`Window::GetDpiScale() -> float`** | ✅ **加** | 返回 `1.0` / `1.25` / `1.5` / `2.0`。★ **不用 `GetDpi()`（int，96/144）**——那是**平台词汇**，会把平台概念带进公共 API；`Scale` 直述"DIP 缩放比"，与公共 API 的 DIP 语义同族（「命名即语义」） |
| 屏幕枚举（`EnumerateMonitors` / `GetMonitorWorkArea` / `GetMonitorName`） | ❌ **不加** | ★ **采纳评审 YAGNI 护栏**：先有真实使用场景；否则本阶段会从 DPI 膨胀成 **Display 子系统** |
| 记账 | 落 `roadmap-deferred.md`（并入 **#8** 备注，**不新开号**——避免三份账） | — |

★★ **G4 的范围收敛（须评审确认）**：需求 §2.1 的 G4 原文是「**屏幕 / DPI** 查询 API」。本稿把它收敛为「**仅 DPI 查询**」——**屏幕信息部分明确不做**。这是**需求 → 初设的正常收敛**（需求写目标、初设定范围），但**因为缩小了需求原文的范围，必须显式标记**，不得静默滑过。

### 2.5 Q5：`WM_NCHITTEST` 的换算方向

**定案：A——把入参换成 DIP。**

```text
现状（B4）                                 定案后
  pt（屏幕坐标）                             pt（屏幕坐标）
  → x, y（窗口物理坐标）        :372-374     → x, y（窗口物理坐标）       不动
                                             → ★ x = PixelsToDip(x), y = …   ← 换算一次，方向反转
  → inset = DipToPixels(...)    :377         → inset / caption 直接用 DIP 常量（不再换算）
  → caption = DipToPixels(...)  :379
  → w / h 取自 GetWindowRect    :381-383     → w / h 亦转 DIP
  → 全部按物理比较                            → 全部按 DIP 比较
  → IsClientInteractiveAt(x,y)  :423         → IsClientInteractiveAt(x,y)   ← 收到 DIP
                                                   ⇒ 与 HitTest 的 DIP 几何一致（闭合 L6/D6）
```

★ **判据（真因，不是「更符合 R3」）**：评审第一轮给出 A 的理由是「更符合 R3」——**该理由不成立**：**B 同样发生在平台边界内**，R3 区分不了 A 与 B。真判据 = **`HitTest` 的入参语义必须恒为 DIP**（既有坐标语义不变量：`HitTest` 加回父偏移、与会话级「鼠标事件坐标为 DIP」同向）——选 B 会让 `HitTest` 在某次调用里收**物理坐标** ⇒ **同一个函数出现两种单位**。

★ **连带效应（须同步处置）**：
1. `DipToPixels` 在该函数的 **2 个调用点（B3）消失**；
2. K1 的注释「**换算点唯一在此——公共 API 语义恒为 DIP**」**仍成立但方向反转**（由「DIP → 物理」变为「物理 → DIP」）⇒ **注释须同步改写**（否则公共头/实现里留下一句被本阶段证伪的描述——条 80）。

### 2.6 Q6：测试如何注入非 96 的 DPI

**定案：DPI 成为 `WindowMessageHandler` 的「正常成员状态」，而不是「测试缝」。**

| 项 | 内容 |
|---|---|
| **机制** | `WindowMessageHandler` 新增私有成员 `int m_dpi = 96;` + `void SetDpi(int dpi)`；由 `Win32PlatformWindow` 在**hwnd 就绪后**（构造末尾）与 **`WM_DPICHANGED`** 时写入（B15 现状：构造只收 host ⇒ 必须补这条通路） |
| **为什么不是"为测试加洞"** | 判据（skill 条 91②）：**换平台时还要改吗？**——`WindowMessageHandler` 是 **Win32 专有件**，换平台即整体替换 ⇒ 这是「**平台翻译器持有当前窗口 DPI**」的**正常职责**，不是挂在公共 API 上的测试缝（对照 `Win32PlatformWindow::GetHwndForTests()` 那种真正该被质疑的缝） |
| **测试两条路（都要验）** | ① **纯换算**独立可测：`DipToPixels` / `PixelsToDip` 在 `dpi = 96 / 120 / 144 / 192` 下的取值与公式逐位一致（A1）；② **真翻译路径**：`handler.SetDpi(144)` → `Handle(nullptr, nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(120, 90))` → 断言事件坐标 = **(80, 60)**（A2） |
| **沿用先例** | Phase 19 的 `FakeHost` + `Handle()` 路径（B16）——**基础设施已在，零新建** |

⚠️ **禁止的实现方式**：翻译器内部直接 `GetDpiForWindow(hwnd)`——B16 的测试形态传 **`hwnd = nullptr`** ⇒ DPI 无从伪造 ⇒ **A2 无法自动化**，只剩人工目视。这正是评审 §11 指出的风险点。

---

## 3. 接口与实现改动分解

### 3.1 公共头改动（逐文件）

**公共头数 92 → 92**（只改既有头，**无新头**）；**公共 API 净增 +1**（`Window::GetDpiScale`）。

| 文件 | 改动 | 性质 |
|---|---|---|
| `Core/Font.h` | `size` 的语义注释：`像素高度` → **`DIP 高度`**（**数值不变**） | ★ **公共语义变更**（D4） |
| `Window/Window.h` | **新增** `float GetDpiScale() const noexcept;` | ★ 新增 API |
| `Platform/PlatformWindow.h` | **新增** `virtual float GetDpiScale() const = 0;` | ★ 纯虚 ⇒ **实现者清单须全库 grep**（含测试替身，见 §5） |
| `Render/TextMeasurer.h` | 注释：返回值单位 = **DIP**（签名不变） | 语义澄清 |
| `Application/Application.h` | 构造的 `@details` 补「**进程 DPI 感知在构造期声明**」 | 语义澄清 |

★ **`Font::size` 语义变更的零破坏性**：数值不变（仍是 `14.0f`）+ 100% 下换算恒等（G5）⇒ **100% 环境行为逐位不变**；150% 下字体**变大**（`14 DIP → 21 px`）——**这正是 D4 的目的**（让 DIP 契约生效），不是回归。

### 3.2 平台层改动（`src/Platform/Win32/`）

| 改动 | 内容 |
|---|---|
| **DPI 工具对** | `DipToPixels` **保留**（B2，注释更新：来源换 `GetDpiForWindow`、方向语义不变）；**新增 `PixelsToDip(int px, int dpi)`**（平台层内部，匿名 namespace 或私有静态）——公式 `px * 96 / dpi`，**须定义舍入行为**（检视 `DipToPixels` 的 half-up：反向是否对称，留详设） |
| **DPI 来源替换** | `GetDeviceCaps(hdc, LOGPIXELSX)` → **`GetDpiForWindow(hwnd)`**（B2 注释预留的替换点，`:1328`） |
| **窗口创建** | `CreateWindowExW` 的 `width`/`height` 前插 `DipToPixels`（B13，`:121-122`） |
| **`WM_NCHITTEST`** | §2.5 的定案 A（`:372-374` 之后插入 `PixelsToDip`；`:377`/`:379` 两处 `DipToPixels` 撤除；`:381-383` 的 w/h 转 DIP） |
| **`WM_SIZE`** | ★ **`OnResized`**（`:521`）与翻译器（`WindowMessageHandler.cpp:106-108`）**都要** `PixelsToDip`——B5 的**② 与 ③ 两条路**（★ 漏改 ② 则 ③ 改对也无效） |
| **鼠标事件** | `WM_MOUSEMOVE` / 按键 / 滚轮坐标经 `PixelsToDip`（含 `ScreenToClient` 之后） |
| **IME 双通道** | `SetCaretPos` / `COMPOSITIONFORM` / `CANDIDATEFORM` 之前 `DipToPixels`（B18） |
| **`WM_DPICHANGED`** | **新增处理**：采纳系统建议矩形（`lParam` 是 `RECT*`）+ 触发重布局 + **清字体缓存**（D3；缓存两处，见 §3.3） |
| **`GetClientSize`** | 实现内部 `GetClientRect` 之后加 `PixelsToDip`，**返回值变 DIP**（B5 ①；★ §4.1 的"不得合并"判据在此落地） |
| **`GetDpiScale`** | 实现：`GetDpiForWindow(m_hwnd) / 96.0f` |
| **感知声明** | `Application` 构造期调用 `SetProcessDpiAwarenessContext`（§2.3） |

### 3.3 渲染层改动（★ 两处字体缓存）

| 文件 | 改动 |
|---|---|
| `GDIBackend`（`src/Render/GDIBackend.cpp` + `.h`） | `GetOrCreateFont(font)` → **需要 DPI**；`lfHeight = -lround(font.size)` → **`-lround(DipToPixels(font.size, dpi))`**；**缓存键加 DPI**（B6/`GDIBackend.h:76`）。★ 该类的 `GetOrCreateFont` 是 **静态**（`GDIBackend.h:61` 声明为 `HFONT GetOrCreateFont(const Font&)`，需改为实例方法或加 DPI 形参） |
| `GDITextMeasurer`（`src/Render/GDITextMeasurer.cpp` + `.h`） | 同上：`lfHeight` 按自己的测量 DC 的 DPI 换算（§2.2.4）；**缓存键加 DPI**（B6/`GDITextMeasurer.h:32`） |
| **字体缓存失效钩子** | `WM_DPICHANGED` 需要**清空两处缓存** ⇒ 需接缝（O2：加 `ClearFontCache()` 到两实现？还是让 DPI 进键后**自然不命中**——后者零新增 API，**倾向后者**） |

★ **为什么不新增公共 API**：DPI 进缓存键后，跨屏复用**自然不命中**（旧 DPI 的 HFONT 不会被取到）⇒ **不需要显式的失效接口**。D3 的"清字体缓存"由此**自动满足**——这是本稿对需求 D3 的一处**简化**（须评审确认：D3 原文写"清字体缓存"，本稿给出的是**更强的**等价物：**键隔离**）。

### 3.4 尺寸三入口（B5 的落地口径）

| 入口 | 现状 | 定案后 |
|---|---|---|
| ① 构造期 `GetClientSize` | `GetClientRect` 物理 | **实现内 `PixelsToDip`** ⇒ 返回 DIP（§4.1 的判据在此） |
| ② `OnResized(LOWORD, HIWORD)` | 直传 | **`PixelsToDip`**（★ 布局几何源头） |
| ③ `WindowResizedEvent` | 直传 | **`PixelsToDip`** |

### 3.5 待实测（不阻塞设计）

| # | 待确认 | 说明 |
|---|---|---|
| **P1** | `WM_DPICHANGED` 的实际到达时机与 `lParam` 建议矩形 | Phase 16 spike 曾报「未收到」（那是 unaware 态下的预期结果）；V2 下**应当**收到 ⇒ **须实测** |
| **P2** | `GetDC(NULL)` 在 **V2 感知**进程下返回哪个显示器的 DPI | 决定 §2.2.3 代价 3 的实际量级（主屏 DPI vs 窗口所在屏） |
| **P3** | `GetDpiForWindow` 在窗口尚未显示时的返回值是否可靠 | 影响 §2.6「构造末尾写入 DPI」的时机 |

★ 记法纪律：**实测前不得写成"事实"**（沿用 Phase 19 §3.5 的纪律）。

---

## 4. 契约（C1–C8）

| # | 契约 | 可由什么检查 |
|---|---|---|
| **C1** | **单位分层**：公共 API / 框架内部恒 DIP；平台边界内部与渲染后端内部恒物理。**新增的公共 API 不得引入物理量** | 公共头 include 图 + 人工核（§2.1 三条可判规则） |
| **C2** | ★ **换算只在平台边界**（R3）：`PixelsToDip` / `DipToPixels` **不得出现在 `src/Widget/` / `src/Layout/` / `src/EventSystem/` / `src/Render/`（后端内部除外）** | **grep 可检**：`grep -rn "DipToPixels\|PixelsToDip" ECDI/src/ --include=*.cpp` 只应命中 `src/Platform/Win32/` 与 `src/Render/`（渲染侧的物理换算） |
| **C3** | **测量返回值 = DIP**：`TextMeasurer` 的两个方法返回值单位恒为 DIP | `TextMeasurer.h` 注释 + 用例（T20-n） |
| **C4** | ★ **`GetClientSize()` 返回 DIP**，而 `GDIBackend` 内的 `GetClientRect` 保持物理——**两者不得合并成一个访问器** | 人工核 + 代码结构（§4.1） |
| **C5** | ★ **零破坏（精确口径）**：① 不新增纯虚**除非**同步补齐全库实现者（含测试替身）；② 不改任何既存形参的顺序与类型；③ **`dpi == 96` 时全部换算恒等** ⇒ 既有 **236 用例逐位不变**；④ `Font::size` **数值不变** | `git diff` 结构性核查 + 四工具链 236 全绿（须写明断言是否启用） |
| **C6** | ★ **感知声明失败容忍**：`SetProcessDpiAwarenessContext` 返回 `FALSE` ⇒ 仅 `Log(Warning)`，**不抛异常、不断言** | 代码 + 用例（可注入失败？留详设） |
| **C7** | **DPI 来源统一**：平台侧一律 `GetDpiForWindow`；**不得**再出现 `GetDeviceCaps(LOGPIXELSX)` 作为窗口 DPI 来源（测量器的屏幕 DC 除外——它测的是**测量基准**，不是窗口） | grep |
| **C8** | **`WM_DPICHANGED` 后**：几何正确 + 重布局 + 字体缓存**不再复用旧 DPI 的 HFONT**（由 §3.3 的键隔离保证） | 目视（A5）+ 代码结构 |

---

## 5. 影响面

| 面 | 规模 |
|---|---|
| **公共头** | **92 → 92**（改 5 个既有头：`Font.h` / `Window.h` / `PlatformWindow.h` / `TextMeasurer.h` / `Application.h`；**无新头**） |
| **公共 API 净增** | **+1**（`Window::GetDpiScale`）· ★ **`PlatformWindow::GetDpiScale` 是新纯虚** ⇒ **实现者清单须全库 grep**（skill 条 33：含 `src/Tests/` 的 `TestPlatformWindow` 等替身） |
| **语义变更** | **1 处**：`Font::size` 由「像素高度」→「**DIP 高度**」（数值不变，100% 下行为不变） |
| **实现** | `Win32PlatformWindow.cpp`（DPI 来源 / 创建 / NCHITTEST / OnResized / IME / `WM_DPICHANGED` / `GetClientSize` / `GetDpiScale` 实现）· `WindowMessageHandler.{h,cpp}`（DPI 成员 + 鼠标/尺寸坐标换算）· `GDIBackend.{h,cpp}` 与 `GDITextMeasurer.{h,cpp}`（`lfHeight` + 缓存键）· `Application.cpp`（感知声明） |
| **测试** | 扩展既有文件（`EventTests.cpp` 承载翻译路径；**建议**新建 `DpiTests.cpp` 承载纯换算——★ 新建则**须**同步 `RunAllTests.h/.cpp` 两处登记，skill 条 99③） |
| **断言特征串** | **11 → 11**（预期不新增；若新增前提则同步扩特征集，skill 条 50⑧④） |
| **构建** | 零 CMake 改动（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库） |
| **`examples/`** | 目视验收复用 ModelProbe（A5/A6） |

---

## 6. 开放决策点（O 系列）

| # | 决策点 | 倾向 | 说明 |
|---|---|---|---|
| **O1** | `GDITextMeasurer` 是否引入**可注入的 DPI 基准** | **暂不引入**（记账） | §2.2.4；触发 = 跨屏布局错位的真实报告 |
| **O2** | 字体缓存失效：显式 `ClearFontCache()` vs **键隔离** | ★ **键隔离**（零新增 API） | §3.3 |
| **O3** | `PixelsToDip` 的舍入：对称 half-up vs 截断 | 详设定 | 须检视与 `DipToPixels` 的往返一致性（`dip → px → dip` 是否恒等） |
| **O4** | 感知声明的旧系统兜底（`SetProcessDPIAware` / `SetProcessDpiAwareness`） | 详设定 | `SetProcessDpiAwarenessContext` 需 Win10 1703+ |
| **O5** | `GetDpiForWindow` 在无窗口/未显示时的行为 | 详设定（P3 实测后定） | §3.5 |
| **O6** | `WM_DPICHANGED` 的"重布局"由谁触发（`Invalidate` / 显式 `Arrange` 链） | 详设定 | 须与 B5 ② 的既有触发链一致 |
| **O7** | 屏幕枚举（`GetMonitorWorkArea` 等）是否记账 | ✅ **记账**（并入 #8） | §2.4 |

---

## 7. 测试方向（T20-n）

| # | 驱动 | 断言 | 对应 |
|---|---|---|---|
| **T20-1** | 纯换算 `DipToPixels(dip, dpi)` | `dpi = 96 / 120 / 144 / 192` 下取值与公式逐位一致；★ **`dpi = 96` 时恒等**（G5） | **A1 / C5** |
| **T20-2** | 纯换算 `PixelsToDip(px, dpi)` | `dpi = 96` 时恒等；往返 `dip → px → dip` 在给定 dpi 下的行为符合 O3 定案 | **A1** |
| **T20-3** | `GetDpiScale()` | `dpi = 96 ⇒ 1.0`；`144 ⇒ 1.5`（**不依赖真实屏幕**——经 `WindowMessageHandler` 注入或平台替身） | **A4** |
| **T20-4** ★ | `SetDpi(144)` + `WM_MOUSEMOVE`，`MAKELPARAM(120, 90)` | 事件坐标 = **`(80, 60)`**（物理 → DIP） | **A2 / Q6** |
| **T20-5** ★ | `SetDpi(144)` + `WM_SIZE`，`MAKELPARAM(1200, 900)` | `WindowResizedEvent` = **(800, 600)**（★ **`OnResized` 与事件两条路都要断言**——B5 ② 与 ③） | **A2 / C4** |
| **T20-6** | `SetDpi(96)` + 同上两条 | **逐位等于输入**（G5 的自动化锚） | **C5** |
| **T20-7** | `Font::size` 语义（文档级） | 断言 `Font{14.0f}` 在 `dpi = 96` 下的 `lfHeight == -14`（维持现状） | **C5** |
| **T20-8** | 三入口一致性 | 构造期 `GetClientSize` / `OnResized` / `WindowResizedEvent` 在同一 `dpi` 下**换算口径一致** | **C4** |

★ **两条测试纪律（沿用 Phase 19）**：
1. **手工构造事件不得作为验收证据**——A2 必须经 `Handle()` 真翻译路径（B16）；
2. **`FakeHost` 路径的坐标断言有限制**——`hwnd = nullptr` ⇒ `ScreenToClient` 不生效（Phase 19 §7 已记）⇒ **滚轮/含 `ScreenToClient` 的分支只断言状态与增量，不断言坐标**。

---

## 8. 验收（需求 A1–A6 的落地口径）

| # | 需求判据 | 本稿落地 |
|---|---|---|
| **A1** | 换算函数在 `dpi = 96 / 120 / 144 / 192` 下与公式逐位一致；`96` 时恒等 | **T20-1 / T20-2**（纯函数，自动） |
| **A2** | 事件翻译：给定物理坐标 + DPI ⇒ 事件坐标 = 预期 DIP | **T20-4 / T20-5**（`FakeHost` + `Handle()` **真翻译路径**） |
| **A3** | 零回归：100% 下既有 **236** 用例全绿（**须写明断言是否启用**） | 四工具链（MSVC / ClangCL / Clang / MinGW） |
| **A4** | 感知声明生效：运行时查询进程感知级别 = Per-Monitor V2 | **T20-3** + 人工核（`GetProcessDpiAwareness`） |
| **A5** | 视觉：125% / 150% 屏上窗口物理尺寸按比例放大、内容不缩小；拖动 / 双击最大化 / Aero Snap 正常 | **人工**（沿 Phase 16 spike 的目视法）+ 固定 N 拍重插的先例 |
| **A6** | 跨屏：窗口移到另一 DPI 显示器后尺寸与命中正确（K6 错位消失） | **人工**（双屏环境）；★ 须记录 P1 实测结果 |

---

## 9. 交给详细设计的六件事

> 编号用 **①–⑥**（**不使用** `D-` 前缀——`D0–D8` 已被需求占用）。

| # | 事项 | 已有输入 |
|---|---|---|
| **①** | **`PixelsToDip` 的完整规格 + 舍入定案**（含与 `DipToPixels` 的往返一致性证明） | §3.2 · O3 |
| **②** | **`WM_DPICHANGED` 的完整处理链**（建议矩形 → 重布局 → 缓存） | §3.2 · D3 · O6 |
| **③** | **`PlatformWindow::GetDpiScale` 纯虚的「实现者清单」**（全库 grep，**含 `src/Tests/` 替身**） | §5 · skill 条 33 |
| **④** | **两处字体缓存的 DPI 化**（含 `GDIBackend::GetOrCreateFont` 是静态方法这一事实） | §3.3 · B6/B7 |
| **⑤** | **DPI 注入通路**（`WindowMessageHandler::SetDpi` 的调用点：构造末尾 + `WM_DPICHANGED`） | §2.6 · B15 |
| **⑥** | **P1–P3 的实测记录**（时机 / `GetDC(NULL)` 的 DPI / `GetDpiForWindow` 的时序） | §3.5 |

★ 另三条**不得在详设阶段推翻**的定案（评审若认可，请在结论里明确）：**Q2 返回 DIP** · **Q3 落 `Application` 构造** · **Q5 选 A（入参换 DIP）**。

---

## 10. 修订记录

- **v1.0**（2026-09-24）初稿。**输入**：需求稿 v1.1（外部评审第一轮通过）+ 评审指定的四个初设重点（Q2 / Q3 / Q4 / Q6）。**内容**：B1–B18 代码基线（逐条带行号；★ 其中 **B6 两处字体缓存**、**B8 测量器用屏幕 DC**、**B9 测量接口无 DPI 参数**、**B15 翻译器无 DPI 来源** 四条是本稿新勘出的关键事实）· §1.3 Q→答案索引 · **§2 六项定案**（Q1 单位分层 / ★ Q2 返回 DIP + 自洽性论证 + 三条代价 + 测量基准处置 / ★ Q3 落 `Application` 构造 + 失败容忍 / ★ Q4 只加 `GetDpiScale` **并收敛 G4 范围** / ★ Q5 定案 A + 判据纠正 / ★ Q6 DPI 为翻译器正常状态）· §3 改动分解（含 §3.3 **两处字体缓存**、§3.4 **尺寸三入口**、§3.5 三条待实测）· **契约 C1–C8** · §5 影响面 · **O1–O7** · **T20-1..T20-8** · §8 验收 A1–A6 落地 · §9 **交给详细设计的六件事**。**待评审。**
