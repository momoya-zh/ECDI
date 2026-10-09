# Phase 31 · 窗口几何与行高口径 —— 需求确认（v1.0）

> 来源：两条**独立输入**合并——① **DesktopNest M1 勘察**（2026-10-08）：M1 的 K2 折叠 / K4 拆出级联偏移需要**运行期窗口几何（尺寸 + 位置）**，而框架**无此能力**；② **`ECDI_fake_harness` 能力需求账本 `REQ-05`**（2026-10-08）：GDI / GL 两条链对**同一字体**给出**不同行高**——该账本本轮唯一确认的 **ECDI 侧缺陷**。
> 状态：**v1.0（2026-10-08，待评审）**
> 定位：**能力型 + 缺陷修复型混合 Phase**（沿 Phase 30「打包」先例）——两项范围**互相独立、无依赖**，各自成批、各自验收。
> ★ **本 Phase 不占 M1 编号**：M1 是应用侧里程碑（`desktopnest-m1-*.md`）；本稿是**框架侧**改动——沿用 roadmap v1.18 §9-7 拍板原话「**框架侧若冒出新缺口再单独立 Phase**」。

---

## §1 动机

### 1.1 输入一：M1 的窗口几何依赖（应用侧拉动）

M1 需求稿 **K2**（二态折叠「折叠 = 仅标题条」）与 **K4**（拆出后「无记忆或冲突时**一律级联偏移**」）都要求应用在**运行期**改变窗口的**尺寸**与**位置**。M1 勘察（2026-10-08）确认框架**无此能力**，且这是**明写的契约决定**（见 K5 / K6）。

⇒ 若不做：M1 的 K2 只能降级为「框尺寸不变、仅隐藏内容」、K4 只能「拆出后落回原位」——**观感闭环不成立**（而 M1 的目标恰恰是**观感验证**）。

### 1.2 输入二：harness REQ-05 行高口径不一致（消费侧拉动）

`ECDI_fake_harness` 在 2026-10-08 的受控复现中实测：同一夹具、同一字体，**GDI 行距 14 px vs GL 行距 16 px**（8 段 CJK 探针 `probe-cjk-cjk_many.jsonl`，逐行剖面量化；GDI 为严格 14 px 周期、GL 为均匀 16 px）。

根因经**本会话独立复现** = 两条 `LineHeight` 实现**依据不同**（K1）。★ 该账本已把 `REQ-01` / `REQ-03` / `REQ-04` 三条**归属反转为 harness 侧**，**`REQ-05` 是其唯一保留的 ECDI 侧缺陷**。

### 1.3 为什么合并为一个 Phase

两项的范围、影响面、批次完全独立，合并的理由**不是技术耦合**，而是**排期**：M1 卡在 ② 上，而 ② 的形态（新增 `PlatformWindow` 纯虚 + Win32 消息路径）与 ①（测量链契约）都属**框架侧小改**，各自单独立 Phase 会引入两次完整的评审—实施—收口流程。⇒ 沿 Phase 30「快修小轮打包」先例合并，但**批次与验收分开**（D5）。

---

## §2 现状勘察（K1–K8，全部带行号实测 2026-10-08）

### K1 ★ `LineHeight` 两条链依据不同（缺陷根因）

| 链 | 实现 | 依据 | 语义 |
|---|---|---|---|
| **GDI** | `GDITextMeasurer.cpp:260-296`，依据在 **`:287`** | `TEXTMETRICW.tmHeight` | `tmAscent + tmDescent + **tmInternalLeading**`（Windows 字体映射器按字体 + charset 算出的推荐行距） |
| **FT / GL** | `FontEngine.cpp:384-411`，依据在 **`:401`** | `face->size->metrics.height` | `ascender − descender + **lineGap**`（字体文件自带） |

★ **契约缺口**：`TextMeasurer.h:119-121` 对 `LineHeight` 只有一句约束——

> @brief 字体行高（单行文本垂直居中用——精确值，非字号估算，P7）
> @return 行高——★ **单位恒为 DIP**（同 MeasureText）

**只规定了单位，未规定口径**。两条链的 DPI→DIP 换算**口径本身是一致的**（都是 `× 96 / dpi`），差异纯粹来自**上游取谁**。⇒ 这是**契约缺一句话**，不是任一侧的实现 bug。

### K2 放大效应

`TextWidget.cpp:307-308`：`layout.totalHeight = lineHeight * lines.size()` ⇒ **行数越多，两后端总高差越大**（harness 实测 8 行差 16 px）。同理影响一切「按行高排版」的几何。

### K3 ★ `LineHeight` 生产消费点 **9 处**（实测计数，非推算）

| 文件 | 消费点 |
|---|---|
| `TextWidget.cpp` | **3 处**——`:206`（单行定位）· `:378`（wrap 逐行 y 步进）· `:475`（preferred 高度） |
| `TextBox.cpp` | **6 处**——`:454`（内容高）· `:472` · `:611` · `:633`（光标高）· `:698` · `:1280`（均经定义 `TextBox.cpp:431-436` 的 `GetLineHeight()`） |

⇒ **口径一变，这 9 处全部受影响**（垂直居中 / 光标高 / 滚动范围 / 命中定行）。这是 D1 选 (b) 时**回归面的硬数据**。

### K4 ★ 行内定位的**第二个**不一致（同族隐患，须一并定）

FT 侧的**基线**由 `Ascent` 算出：`FontEngine.cpp:413-436`（`metrics.ascender / 64`），消费点 `GLRenderer.cpp:215`：

```
const float baseline = std::floor(pos.y) + std::floor(m_fontEngine->Ascent(font));
```

而 GDI 侧走 `TextOutW` 的 `TA_LEFT | TA_TOP` 语义（`GDIBackend.cpp:379+`）。★ **`Ascent` 目前只有 FT 侧有**（GDI 侧无对应接口）⇒ **行高统一了、基线口径不统一仍会偏**。这是 D3 的由来。

### K5 ★ 窗口尺寸无运行期 API（**明写的契约决定**）

- `PlatformWindow.h:162` 原话：「★ **不提供「运行期改窗口尺寸」能力**：尺寸来源唯一（构造期记录，契约 C4）」
- 通读 `PlatformWindow.h` 全 **166 行**：**无** `SetSize` / `Resize` / `MoveWindow`
- `Window.h` 公共面 **38 个方法**：**无**尺寸设定器
- 生产代码**唯一**改尺寸处 = `Win32PlatformWindow.cpp:191-193`（`ApplyStartupSize`，且带 `SWP_NOMOVE`；另 `WM_DPICHANGED` 采纳系统建议矩形 `:649-653`）
- 公共面无 HWND 逃生门（`GetHwndForTests` 在 `src/`；`Window.h` 零 Win32 类型）

### K6 ★ 窗口位置**同样**无 API

`Widget::SetPosition`（`Widget.h:83`）是**控件在窗口内**的位置，**不是窗口在屏幕上的位置**；`Window.h` 无 `GetWindowRect` / `SetPosition` / `GetBounds`。

⇒ M1 的 **K4「级联偏移」需要的是窗口屏幕位置**，**同样缺失**。★ 换言之 ② 的范围必须是**尺寸 + 位置 + 读回**三者，不能只做尺寸。

### K7 拖动**不缺**能力（系统路线，无需框架改动）

- `Win32PlatformWindow.cpp:490-504` 的 `WM_NCHITTEST`：标题栏区（`y < caption`）先问 `m_host.IsClientInteractiveAt(x, y)`（`PlatformWindowHost.h:64`）——命中有交互控件则走客户区，否则返回 `HTCAPTION`。注释原话：「**拖动移动 / 双击最大化 / Aero Snap 系统免费获得**」。
- `CaptionBar.cpp` 内 `OnMouseButtonDown` / `IsButtonDown` / `SetCaptureWidget` **零命中**（实测 grep）⇒ `CaptionBar` **自己零拖动代码**，拖动完全由系统完成。
- ★ 附带事实：`MouseEvent::IsButtonDown`（`MouseEvent.h:**59**`，Phase 19）**生产零消费**（全库仅 `EventTests.cpp` 使用）⇒ 「M1 拖动 = 消费 Phase 19 能力」这一说法**不成立**；真实路线是 K7 的 `HTCAPTION`。

### K8 实现者清单（条 33——加纯虚前必查）

| 接口 | 实现者数 | 清单 |
|---|---|---|
| `TextMeasurer` | **7** | 生产 2 = `GDITextMeasurer`（`src/Render/GDITextMeasurer.h:25`）· `FreeTypeTextMeasurer`（`src/Render/FreeTypeTextMeasurer.h:16`）；内部 1 = `RecordingBackend`（`src/Render/RecordingBackend.h:21`，与 `RenderingBackend` 双继承）；测试 4 = `WidgetTests.cpp:402` / `TextBoxTests.cpp:31` 两个 `FakeTextMeasurer` · `CountingMeasurer`（`WidgetTests.cpp:515`）· `FixedWidthMeasurer`（`TextMeasurerTests.cpp:203`） |
| `PlatformWindow` | **3** | 生产 1 = `Win32PlatformWindow`（`src/Platform/Win32/Win32PlatformWindow.h:25`）；测试 2 = `TestPlatformWindow`（`AnimationTests.cpp:24` · `ProgressBarTests.cpp:30`） |

⇒ ② 一旦新增纯虚，**3 个实现者必须同步**（含 2 个测试替身）。

---

## §3 范围

### 3.1 做（倾向）

1. **① 行高口径统一**：在 `TextMeasurer` 契约里**钉死「行高」的口径**，GDI 与 FT 两条链**都按该口径实现** ⇒ 同字体、同 DIP 下两链给出**同一行高**。
2. **② 窗口几何读写**：为 `Window` 提供**运行期**的**尺寸 + 位置 + 读回**能力，沿 `PlatformWindow.h:22-28` 已定稿的**三步惯例**（R9 / `D-SEAM-1`）：① 接口加能力 virtual ② `Window` 加公共方法透传 ③ 平台消息在实现内消化——**不新建接缝类、不新增消息注册机制**。

### 3.2 非目标（本轮明确不做）

| 项 | 去向与依据 |
|---|---|
| **`\n` 硬断行**（`REQ-01`） | ★ **已归属反转 = harness 侧**：`App.cpp:1144-1145` 的 `needWrap` **只看宽度**；而 `SetWordWrap(false)` 的契约明写「**100% Phase 28 原路径**（红线①：单条 `DrawText`，**布局引擎零接触**——C29-5）」（`TextWidget.h:62-63`）⇒ 属**设计内行为**，**不属 ECDI 缺陷** |
| **`PostToUi` 消费**（`REQ-03`） | 能力早已实现（`Application.h:86`，Phase 23，含入队 / 唤醒 / 失败回滚同临界区）+ ECDI 自带 `ApplicationDispatchTests.cpp` 覆盖 ⇒ 缺的是**消费方**，非能力 |
| **按钮使能**（`REQ-04`） | `Widget::SetEnabled` / `IsEnabled` **自 Phase 3 即在**（`Widget.h:150-152`）+ `HitTest` **已按 `IsEnabled` 门控**（`Widget.cpp:124`）⇒ 能力具备；缺的是 harness 的 `PromptBar` 未暴露按钮 |
| **禁用态视觉（灰化）** | ★ **有意设计**（`modelprobe-p1-preliminary-design.md:130`「框架不做 disabled 绘制」；`phase13-captionbar-detailed-design.md:695` 同）⇒ 非缺陷 |
| 用户拖边缘缩放 / 最小尺寸约束 | M1 §9-5 已拍板**不可缩放**；本 Phase 只给**程序化**几何，不碰 resize 热区（`SetResizeInset` 保持现状） |
| 多屏 / DPI **位置持久化**（落盘） | M1 §1.2 列为 M4；本 Phase 只在内存内读写几何 |
| `Ascent` / `Descent` **全量**契约化 | 仅在与行高统一**必须同步**时纳入（D3）；不做泛化字体度量 API |

---

## §4 关键决策点（倾向已给，待评审）

- **D1 ★★ 行高口径（初设第一优先级）**——两条路线：
  - **(b) 主线 = 契约钉死口径**，两条链都按它实现。**治本**：第三个后端（未来 DirectWrite 等）进来不再歧义。**代价**：**会动默认 GDI 路径** ⇒ K3 的 9 个消费点 + 既有测试替身期望（多处 `LineHeight() == 16.0f`）需回归，逐位等价红线**不成立**。
  - **(a) 备选 = 让 FT 对齐 GDI 的 `tmHeight` 语义**。**默认路径零改动**（GL 是 opt-in）。**代价**：`tmInternalLeading` 在 FT 侧**无直接等价量**，只能折算逼近 ⇒ 属「猜一个数去凑另一个后端的值」，且「对齐目标」随系统字体版本 / DPI / charset 解析漂移 ⇒ **治标**。
  - ★★ **初设必答（须实测，不得推定——条 74）**：`tmInternalLeading` 是否存在 FT 侧等价量（候选：`OS/2` 表 `sTypoLineGap` · `usWinAscent + usWinDescent` · `hhea` 的 `lineGap`）——**这是 (a) 可行性的事实前提**；未取证前不得把 (a) 标为可拍板。
- **D2 窗口几何 API 形态**：尺寸与位置**分开**（`SetSize` + `SetPosition` + `GetBounds`）还是**合成**（如单个 `SetBounds(Rect)`）；**倾向初设给三方案比较**，一并定「是否透出几何类型」「DIP 单位口径」「DIP↔物理像素换算落点」（沿 Phase 20 契约：公共 API 恒为 DIP）。
- **D3 `Ascent` / `Descent` 是否升格为契约的一部分**：不升格则行高统一后**基线仍可能偏**（K4）；倾向初设明确「要么一并钉死口径、要么显式记为已知限制」——**不接受静默**。
- **D4 契约落点写法**：`TextMeasurer::LineHeight` 的 `@details` 如何表述口径，使其**可被两个实现者独立照做**（这是 (b) 的实质交付物）。
- **D5 批次与顺序**：① 与 ② 互相独立 ⇒ 倾向**各自成批**；★ **② 可先行**（M1 卡在它上面），① 因涉及 9 消费点回归，**独立批次更易定位**。

---

## §5 验收方向（A1–A4，待初设细化）

- **A1 ★ 两链行高一致（自动，受控对照）**：同一字体 / 同一 DIP 下，GDI 与 FT 的 `LineHeight` 返回值一致（容差按初设定）——`REQ-05` 的**直接闭合判据**。
- **A2 窗口几何往返（自动）**：`Set` → `Get` 往返一致（含 DIP 精度与跨 DPI 换算）；★ **必须含退化输入**（负坐标 / 多显示器边界 / 大于屏幕尺寸）。
- **A3 存量零回归**：既有测试套件全绿；**且逐条核对 K3 的 9 个消费点**行为变化——凡变化必须能解释为「行高口径修正的**预期结果**」，**不得静默漂移**（条 29③d：更正一个数就全库搜旧值）。
- **A4 harness 复跑复现**：`REQ-05` 的读数（GDI 14 px vs GL 16 px）在修复后**复跑一致**——沿 Phase 28 A5「库外探针端到端」先例（跨仓库验证前先核对版本指纹，见 Phase 27 详设 §10.3 教训）。

---

## §6 影响面预算（初估，待初设校准）

| 维度 | 值 |
|---|---|
| 公共头 | **95 → 95**（若 D2 需新公共几何头则 +1，待定） |
| 公共 API | ② **至少 +3**（改尺寸 / 改位置 / 读回）· ① 若仅改契约表述则 **+0**、若随 D3 新增度量接口则 +N |
| 实现者同步 | `PlatformWindow` **3 个**（条 33：新增纯虚必须全改，含 2 个测试替身） |
| 生产消费点 | ① **9 处**（K3 实测） |
| 用例 | 新增 **~6–10 条**（A1/A2 自动部分 + 边界） |
| CMake | **0** |
| 风险 | **中**——① 动默认路径且 9 消费点；② 新增纯虚触 3 实现者 + 走 Win32 消息路径 |

---

## §7 修订记录

- **v1.0**（2026-10-08）初稿。**输入**：M1 勘察（K5/K6 两条契约级缺口）+ `ECDI_fake_harness` 账本 `REQ-05`（唯一 ECDI 侧缺陷）。**§2 勘察 K1–K8 全部带行号实测**——K1 双链依据（`tmHeight` vs `metrics.height`）+ 契约缺口 · K2 放大效应 · **K3 生产消费点 9 处（实测计数）** · **K4 基线口径第二不一致（`Ascent` 仅 FT 侧有）** · K5 尺寸无 API（`PlatformWindow.h:162` 明写）+ K6 位置亦无 API · **K7 拖动不缺能力（`HTCAPTION` 系统路线；`CaptionBar` 零拖动代码；`IsButtonDown` 生产零消费）** · K8 实现者清单（`TextMeasurer` 7 / `PlatformWindow` 3）。**§3 范围** = ① 行高口径统一 + ② 窗口几何（尺寸 + 位置 + 读回）；**非目标 7 项**含三条 REQ 归属反转的**独立复核结论**。**§4 决策点 D1–D5**（D1 = 行高口径，(b) 主线 / (a) 备选 + **初设必答「`tmInternalLeading` 的 FT 等价量须实测」**）。**§5 验收 A1–A4** · §6 影响面（① 9 消费点 / ② 3 实现者 / 风险 中）。
