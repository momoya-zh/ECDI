# Phase 20.1 · 渲染层 DPI 缩放（render DPI scaling）—— 初步设计（v1.0）

> 来源：`docs/phase20.1-render-dpi-scaling-requirements.md`（**v1.1 ✅ 已通过**，2026-09-24）
> 状态：**v1.0**（2026-09-24）——**待评审**（评审通过后方可进详细设计）
> 定性：**为「框架内部(DIP) → 渲染层(物理)」补上唯一一条换算边**——不改公共数据结构、不改命令缓冲的单位
> ★ **编号约定**：需求稿的约束写作 **`C-1`–`C-6`**；本稿**沿用同一序列**向后延续为 **`C-7`–`C-11`**（一个子阶段一套连续编号，避免跨文档混读）

---

## 1. 设计输入与基线

### 1.1 设计输入

| 来源 | 用到的内容 |
|---|---|
| 需求稿 **v1.1** | **U1–U5** 单位不变式 · **C-1..C-6** 约束 · **§4** 折算量清单（8 类命令） · **Q1–Q7** · **A1–A7** |
| Phase 20 详设 **§14** | **R1**（本子阶段即其正式立项）· ★ 其 **C10（负数对称舍入）不适用本子阶段**——那是**平台层 `int` 几何**的契约，本稿是**渲染层 `float` 几何**（**C-4**） |
| 用户实测（2026-09-24） | 主屏 125% / 跨屏 150%：窗口尺寸 OK · 布局(DIP) OK · 命中(DIP) OK · **绘制 ✗**（内容只占左上约 2/3） |

### 1.2 代码基线（B1–B11，全部实测带行号）

| # | 事实 | 位置 |
|---|---|---|
| **B1** | `Renderer` **只持一个 `RenderingBackend&`**，**类内无其它状态** | `Renderer.h:15/30` |
| **B2** ★★ | `Renderer::Execute` 全库 **3 个调用点**：`Window.cpp:136` · `RendererTests.cpp:40` · `RendererTests.cpp:91` | 实测脚本 |
| **B3** | `Renderer::BeginFrame` 全库 **1 个调用点**：`Window.cpp:135` | 实测脚本 |
| **B4** | `Renderer::ExecuteCommand` = **8 个私有重载**，每个一步转发给后端 | `Renderer.cpp:24-67` |
| **B5** ★★ | `BeginFrame` / `EndFrame` 的既有契约是「**直接转发**」（决策 13）；**Phase 18 详设 §6 第 7 条**并把它冻结为「**仍是单行转发**——不得在 `Renderer` 里存状态」 | `Renderer.h:17` · `phase18-window-background-detailed-design.md:157` |
| **B6** ★★ | `DrawTextCommand` 含 **`std::string`**、`DrawImageCommand` 含 **`Image`（`std::vector<uint8_t> pixels`）** | `RenderCommand.h:29/61` |
| **B7** ★ | `RecordingBackend` 记录的**正是后端收到的几何**：`draws[].rect` · `textDraws[].pos`+`font` · `lineCalls` · `roundedRectCalls` · `imageCalls.dest` · `clipOps[].rect` · `focusRectCalls` | `RecordingBackend.h:24-85` |
| **B8** | `Window::PaintFrame` 是**唯一**帧编排点，五步连续：clear → `Root.Paint` → `BeginFrame` → `Execute` → `EndFrame` | `Window.cpp:131-137` |
| **B9** | `Window::GetDpiScale()` 已存在（Phase 20 落地）；平台窗口未就绪 ⇒ **`1.0f`** | `Window.cpp:189-201` |
| **B10** ★★ | **后端几何取整口径本就不统一**（既有事实）：`DrawRect` **截断**（`static_cast<LONG>`，决策 25）· `PushClip` **`lround`** · `DrawLine` **`lround`**（width 另有 `max(1L, lround)` 下限）· `DrawRoundedRect` 半径 `lround` + 钳制 | `GDIBackend.cpp:347-350` · `:604-607` · `:407` · `:437-439` |
| **B11** | `GDIBackend::GetOrCreateFont` **已**按窗口 DPI 换算 `lfHeight` 并把 DPI 纳入缓存键（Phase 20 批五） | `GDIBackend.cpp:759-772` |

**B6 与 B10 各自直接决定一条设计结论**（见 §2.1 的 A2 选择理由、§2.4 的 Q7 定案）。

### 1.3 需求 Q1–Q7 的本稿结论一览

| Q | 结论 | 落点 |
|---|---|---|
| **Q1** | 落点 = **`Renderer` 入口**；形态 = **A2**（执行期瞬时转换，**不碰 `CommandBuffer`**）；方案 B / C **明确否决** | §2.1 |
| **Q2** | ★ **`Execute(commands, scale = 1.0f)`**（**与需求稿倾向的 `BeginFrame` 相反**——理由与备选见 §2.2 / §2.6） | §2.2 |
| **Q3** | **允许**——「**认识 `scale` ≠ 认识 `DPI`**」；代价是 `Renderer` 从「纯转发」升格为「转发 + 携带一个坐标变换」，**须显式声明** | §2.3 |
| **Q4** | 三个**文件级 static helper**（`ScaleRect` / `ScalePoint` / `ScaleLength`）；**不加 `operator*`** | §2.3 |
| **Q5** | 只折**几何**；`font.size` **不折**（后端按 DPI 换算，B11） | §2.4 |
| **Q6** | 落点在 `Renderer` ⇒ 用既有 `RecordingBackend` **完全无头**断言（B7） | §7 |
| **Q7** | 取整不统一 = **既有近似**（B10）；折算**不引入新取整**，偏差 **≤1px 且与 scale 无关** ⇒ 本子阶段**不统一口径**，记账 | §2.4 |

---

## 2. ★ 核心定案

### 2.1 Q1：落点与形态

**落点 = `Renderer::Execute` 的入口**。两条被否决的方案：

| 方案 | 否决理由 |
|---|---|
| **B：`GDIBackend` 每个 `Draw*` 入口折算** | 把**坐标转换规则泄漏到每个平台后端**——将来接 `OpenGLBackend` 还要重实现一遍同一套规则；而该规则本属**框架**、不属**平台** |
| **C：GDI world transform（`SetMapMode` + `SetWindowExt/ViewportExt`）** | ① 依赖 GDI；② ★ **`AlphaBlend` 走设备坐标、不受 world transform 影响** ⇒ **AA 路径与 `DrawImage` 会与普通绘制不一致**；③ 完全不可复用 |

**形态 = A2（执行期瞬时转换）**，两条独立理由：

1. **语义**：改 `CommandBuffer` 会让「**绘制描述**」变成「**执行后被物理化的命令**」——同一个缓冲被真后端与 `RecordingBackend` 共用时产生**语义污染**；
2. ★ **性能（本稿新补）**：**B6**——`DrawTextCommand` 持 `std::string`、`DrawImageCommand` 持像素缓冲 ⇒「**复制一份折算后的命令**」等价于**每帧深拷贝文本与整张图**，不可接受。⇒ 折算**必须**落在「**取几何 → 乘 → 传给后端**」这一跳，**命令本身一字不动**。

★ **A2 本稿做成「可机检」**：**T20.1-5 直接断言源缓冲逐位不变**（§7）——把「禁止 A1」从纪律升级为**回归锚**。

### 2.2 ★★ Q2：注入形态 —— 本稿定案 `Execute(commands, scale)`（与需求稿倾向相反）

| 候选 | 形态 |
|---|---|
| 需求稿倾向（评审 §12 同向） | `BeginFrame(background, scale)`，`Renderer` 存 `m_scale` |
| **本稿定案** | **`Execute(commands, scale = 1.0f)`**（`scale` 逐层传给 8 个 `ExecuteCommand` 重载） |

**四条理由**（按分量排序）：

1. ★★ **`BeginFrame` 的契约已被 Phase 18 显式冻结为「单行转发、不存状态」**（**B5**）。把 `scale` 塞进它并存入成员 ⇒ 那句契约**当场变假**（`Renderer.h:17` 的「直接转发」与 Phase 18 详设 §6-⑦ 的「不得在 Renderer 里存状态」都要改写）。**保持 `BeginFrame` 一字不动**是本稿最看重的性质（描述不得被自己的改动变成假话）。
2. **变换在「施加点」可见**——`m_renderer.Execute(m_commands, GetDpiScale());` 一行即知「本帧按 1.5× 执行」；`BeginFrame` 版需跨函数追踪 `m_scale` 的来源（教学型框架：**显式优于隐式**）。
3. **`Renderer` 保持无状态** ⇒ 不存在「`BeginFrame` → `Execute` 的顺序契约」，也不存在「忘了设 scale ⇒ 静默按 1.0 渲染」这类失败模式（**那正是本子阶段要修的 bug 的同类**）。
4. **测试更直接**——`renderer.Execute(commands, 2.0f)`，无需先走帧生命周期。

**备选（等价，可切换）**：若评审坚持「scale 是整帧上下文」的语义，改为 `BeginFrame(background, scale)` 只需 **约 3 行**（`Renderer.h` 签名 + `Renderer.cpp` 存 `m_scale` + `Window.cpp:135` 传参），**两方案的行为与测试口径完全相同** ⇒ **这是一个低风险、可逆的决定**。

### 2.3 Q3 + Q4：`Renderer` 的定位与折算的实现形态

**Q3——`Renderer` 允许认识 `scale`**：它只做 `geometry × scale`，**不需要知道** 1.5 是 144 DPI、还是 150% 缩放、还是将来某个 transform ⇒ **不破坏 Widget-agnostic / 平台无关**。但须**显式声明定位变化**：

> `Renderer` 由「**纯转发器**」升格为「**转发 + 携带一个帧级坐标变换**」。

★ 这条**只影响 `Execute` 与 8 个私有重载**，**不触及** `BeginFrame` / `EndFrame`（见 §2.2 理由 1）。

**Q4——三个文件级 static helper，不加 `operator*`**：

```cpp
// Renderer.cpp —— 匿名 namespace（唯一消费者 ⇒ 不建新头）
// ★★ 本子阶段【唯一的几何折算实现点】（契约 C-11）：
//    框架几何(DIP) × scale → 物理像素。纯乘法、不取整（契约 C-8）。
//    不给 Core 的 Rect / Point 加 operator*——那会把「DIP → 物理」降格为
//    几何对象的普通数学操作，极易在 Widget / Layout 里被误用。
static Rect  ScaleRect(const Rect& rect, float scale) noexcept;
static Point ScalePoint(const Point& point, float scale) noexcept;
static float ScaleLength(float value, float scale) noexcept;   // width / cornerRadius
```

★ **命名沿用需求稿 Q4 已给出的 `ScaleRect` / `ScalePoint`**（一个概念一个词），标量补一个 `ScaleLength`。

### 2.4 Q5 / Q6 / Q7

- **Q5（不双重缩放）**：折算**只碰几何**；`Font::size` **原样传给后端**，由 `GDIBackend::GetOrCreateFont` 按窗口 DPI 换算（**B11**）。⇒ 文本的**位置物理**、**字号物理**，两者尺度一致 = **G3**。**这是本子阶段最危险的回归点**（若这里也乘一次 ⇒ 字体双重缩放）。
- **Q6（无头可测）**：折算发生在 `Renderer` ⇒ `RecordingBackend` 记录的**正是折算后的几何**（**B7**）⇒ 新用例**零窗口、零 GDI**。
- **Q7（取整口径）**：**B10** 表明「`DrawRect` 截断 vs `PushClip` 用 `lround`」的 **≤1px 不一致在 Phase 20.1 之前就已存在**。本子阶段的结论：
  1. 折算**只做乘法、不取整**（**C-4 / C-8**）⇒ **不引入新的取整**；
  2. 两处仍各自取整，且**取整误差是绝对量、不随 `scale` 放大** ⇒ 既有偏差**保持不变**（对任意 scale，`trunc(x)` 与 `lround(x)` 之差 ≤ 1）；
  3. ⇒ 记为 **「既有已知近似」**，**本子阶段不统一口径**（统一属 `GDIBackend` 绘制算法改造 = **N5** 边界外）⇒ **建议记入 `roadmap-deferred.md`**（§6-O1）。

### 2.5 ★ 数学保证：为什么 G2「逐位零回归」是结构性成立的

`scale == 1.0f` 时 `v * 1.0f == v` 在 IEEE-754 下**精确成立**（bit-exact，无 FMA、无舍入）⇒ 折算后的 `Rect` / `Point` / 标量与折算前**逐位相同** ⇒ 后端收到的字节不变 ⇒ **既有 247 个用例与目视行为必然不变**。

⇒ ★ **不需要 `if (scale == 1.0f)` 特判**（乘法本身即恒等，加分支只会引入未被覆盖的路径）。**G2 不靠「小心实现」保证，靠浮点语义保证。**

### 2.6 ★ 与需求稿的偏离与勘误（三条，请一并确认）

| # | 需求稿原文 | 本稿 | 性质 |
|---|---|---|---|
| **1** | **C-6**：「`Renderer::Execute` 全库 **4 个**调用点」 | **3 个**（`Window.cpp:136` · `RendererTests.cpp:40` · `:91`，**B2 实测**） | ★ **事实勘误**（原文的**枚举只列了 3 个**，与其数字自相矛盾）⇒ **结论不变**（带默认值仍零改动）；需求稿据此升 **v1.2** |
| **2** | **§7**：「公共 API **0**」 | **0 新增头 / 1 处形参扩展（带默认值 ⇒ 源代码兼容）**——`Renderer.h` **位于 `include/ECDI/`，是公共头** | ★ **精确化**：否则会被误读为「公共头可一字不动」（实际 `Renderer.h` 的 `Execute` 声明行必改） |
| **3** | **Q2** 倾向 `BeginFrame(background, scale)` | **`Execute(commands, scale)`** | ★ **取向偏离**（理由与等价备选见 §2.2）——**请确认** |

---

## 3. 接口与实现改动分解

### 3.1 改动清单（△1–△5）

| △ | 文件 | 改动 |
|---|---|---|
| **△1** | `include/ECDI/Render/Renderer.h` | ① `Execute` 加**带默认值**的形参：`void Execute(const CommandBuffer& commands, float scale = 1.0f);` ② 8 个私有 `ExecuteCommand` 重载各加 `float scale` ③ 类注释补 §2.3 的**定位声明**（★ `BeginFrame` / `EndFrame` 的**注释与实现均不动**） |
| **△2** | `src/Render/Renderer.cpp` | ① 匿名 namespace **新增 3 个 helper**（§2.3 草图）② `Execute` 把 `scale` 交给 `std::visit` 的 lambda（`[this, scale](const auto& cmd){ ExecuteCommand(cmd, scale); }`）③ 8 个重载改为「**取几何 → helper 折算 → 传后端**」 |
| **△3** | `src/Window/Window.cpp`（`PaintFrame`，**`:136` 单行**） | `m_renderer.Execute(m_commands);` 改为传入 `GetDpiScale()`。★ **帧编排五步顺序一字不动**（Phase 18 详设 §6 第 1 条冻结） |
| **△4** | `src/Tests/RendererTests.cpp` | 新增 **5 个用例 + 5 条注册**（§7）。★ **既有文件 ⇒ `RunAllTests.h/.cpp` 零改动** |
| **△5** | `docs/phase20.1-render-dpi-scaling-requirements.md` | §2.6-1 的 **C-6 计数勘误** ⇒ 需求稿升 **v1.2**（两处：§1.4 我方补第 1 条 · §3 表 C-6 行） |

**逐重载折算表（△2 ③ 的完整清单，与需求稿 §4 逐条对齐）**：

| 重载 | 折算的量 | 不动的量 |
|---|---|---|
| `DrawRectCommand` | `rect` | `color` |
| `DrawTextCommand` | **仅 `pos`** | ★ **`font` 原样**（Q5 / B11）· `text` · `color` |
| `DrawLineCommand` | `start` · `end` · **`width`** | `color`（★ `width` 最易漏） |
| `DrawRoundedRectCommand` | `rect` · **`cornerRadius`** | `color`（★ `cornerRadius` 最易漏） |
| `DrawImageCommand` | **仅 `dest`** | ★ **`image` 原样**（源图像像素不变） |
| `PushClipCommand` | `rect` | — |
| `PopClipCommand` | —（无几何；形参统一带 `scale` 但**不使用**） | — |
| `DrawFocusRectCommand` | `rect` · `cornerRadius` | `color` |

### 3.2 代码草图（仅示形态，精确规格交详设）

```cpp
// ── Renderer.cpp 匿名 namespace（唯一折算实现点，C-11）──
static Rect ScaleRect(const Rect& r, float scale) noexcept
{
    return Rect{ r.x * scale, r.y * scale, r.width * scale, r.height * scale };
}
static Point ScalePoint(const Point& p, float scale) noexcept { return Point{ p.x * scale, p.y * scale }; }
static float ScaleLength(float v, float scale) noexcept { return v * scale; }

// ── Execute：scale 只在此处进入流程，逐层显式传参 ──
void Renderer::Execute(const CommandBuffer& commands, float scale)
{
    for (const auto& command : commands)
    {
        std::visit([this, scale](const auto& cmd) { ExecuteCommand(cmd, scale); }, command);
    }
}

// ── 代表性重载：普通 / 特殊（文本）/ 特殊（图像）──
void Renderer::ExecuteCommand(const DrawRectCommand& cmd, float scale)
{
    m_backend.DrawRect(ScaleRect(cmd.rect, scale), cmd.color);
}
void Renderer::ExecuteCommand(const DrawTextCommand& cmd, float scale)
{
    // 只折 pos——font.size 由后端按 DPI 换算（Phase 20 批五），此处再折就是双重缩放
    m_backend.DrawText(ScalePoint(cmd.pos, scale), cmd.text, cmd.color, cmd.font);
}
void Renderer::ExecuteCommand(const DrawImageCommand& cmd, float scale)
{
    // 只折 dest——源图像像素不变（Image 仍按引用传给后端，无拷贝）
    m_backend.DrawImage(ScaleRect(cmd.dest, scale), cmd.image);
}
```

★ **默认实参只写在声明（`Renderer.h`），定义（`Renderer.cpp`）不得重复**——C++ 规则，详设须写明。

---

## 4. 契约（C-1–C-11）

> ★ **编号约定**：**C-1–C-6 来自需求稿**（约束，原样继承）；**C-7–C-11 为本稿在初设层新立**（契约）。**一个子阶段一套连续编号**，避免跨文档混读。

| # | 契约 | 可检方式 |
|---|---|---|
| **C-1..C-6** | （需求稿 §3，原样继承） | 需求稿 |
| **C-7** ★ | `Renderer` 的**输入**几何恒 **DIP**、**输出给 Backend** 的几何恒 **物理**（U2 + U3 的契约化） | 代码结构（`Execute` 之后的几何必经过 helper） |
| **C-8** ★ | 折算**只做乘法、不做取整 / 不提前量化** | 三个 helper 内**无** `lround` / 无 `static_cast<int>`（grep） |
| **C-9** ★ | `Execute` 的 `scale` 形参**必须带默认值 `1.0f`** | 声明处有默认值、定义处无（B2 的 3 个调用点零改动） |
| **C-10** ★ | **折算不修改 `CommandBuffer`**（A2 的契约化） | **T20.1-5** 断言源缓冲逐位不变 |
| **C-11** ★ | **唯一的折算实现点** = `ScaleRect` / `ScalePoint` / `ScaleLength`（G4 的可检化） | `ExecuteCommand` 内**只调用** helper、不内联写乘法 |

---

## 5. 影响面

| 面 | 预计 |
|---|---|
| **改动文件** | **4 代码**（`Renderer.h` · `Renderer.cpp` · `Window.cpp` · `RendererTests.cpp`）+ **1 文档**（需求稿 v1.2 勘误） |
| **公共头** | **92 → 92**（`Renderer.h` 已有；**无新增头**） |
| **公共 API** | ★ **0 新增 / 1 处带默认值的形参扩展**（源代码兼容，3 个既有调用点零改动，**B2/B3**） |
| **新增用例** | **5**（**247 → 252**），全部无头 |
| **`RunAllTests.*`** | **零改动**（扩展既有 `RendererTests.cpp`） |
| **`CMakeLists.txt`** | **零改动**（无新文件） |
| **后端（GDI / Recording）** | **零改动**——它们**看到的单位变了**（DIP → 物理），但**接口与实现都不动**（U4） |
| **`main.cpp`** | **零改动**（★ 按惯例，即便需要也须单独授权） |
| **对 Phase 20** | ★ **Phase 20 的 A5 / A6 验收依赖本子阶段** |

---

## 6. 开放决策点（O 系列）

| # | 事项 | 倾向 |
|---|---|---|
| **O1** | **后端取整口径不统一**（**B10**：`DrawRect` 截断 vs `PushClip` 用 `lround`）——是否统一 | **本子阶段不动**（属 N5）；★ **建议记账 `roadmap-deferred.md`**（否则这个既有 ≤1px 偏差会长期无人知） |
| **O2** | `scale` 的取值时机：**每帧取** vs 缓存 | **每帧取**——`GetDpiScale()` 是薄转发（B9），成本可忽略；★ 且**跨屏 `WM_DPICHANGED` 后下一帧自动跟随，无需新增通路** |
| **O3** | 三个 helper 的归属：留在 `Renderer.cpp` 匿名 namespace vs 提为内部头 | **留在原地**——**唯一消费者**（「第二个真实消费者才抽象」）；将来若 `Renderer` 之外也要折算，再抽 |

---

## 7. 测试方向（T20.1-1..T20.1-5）

**落点**：**扩展既有 `src/Tests/RendererTests.cpp`**（既有文件 ⇒ `RunAllTests.h/.cpp` 零改动）；装置 = 既有 `RecordingBackend`（**B7**：它记录的正是后端收到的几何）。

| # | 用例 | 驱动 | 断言 | 对应 |
|---|---|---|---|---|
| **T20.1-1** ★ | `Renderer.ScaleIdentity` | `Execute(cmds)`（走默认实参）与 `Execute(cmds, 1.0f)` 各一次 | 两者**逐位相同**，且与未加 scale 时的历史期望值相同（`EXPECT_NEAR`，容差 = 既有 `kEpsilon`） | **G2** / C-2 / C-9 |
| **T20.1-2** ★ | `Renderer.ScaleRectAndClip` | `scale = 2.0f`：`DrawRect{Rect{1,2,3,4}}` + `PushClip{Rect{5,6,7,8}}` | `draws[0].rect == {2,4,6,8}` · `clipOps[0].rect == {10,12,14,16}` | A1 / A4 |
| **T20.1-3** ★★ | `Renderer.ScaleAllGeometryFields` | `scale = 1.5f`，一组命令**覆盖全部几何字段**：`DrawLine`（start/end/**width**）· `DrawRoundedRect`（rect/**cornerRadius**）· `DrawImage`（**dest**）· `DrawFocusRect`（rect/cornerRadius） | 逐字段断言（★ **A7**：`width` / `cornerRadius` 是最易漏的两个） | **A7** |
| **T20.1-4** ★★ | `Renderer.ScaleTextPositionOnly` | `scale = 2.0f` + `DrawText{pos{5,7}, font.size = 14}` | `textDraws[0].pos == {10,14}` 且 ★ **`textDraws[0].font.size == 14.0f`（未变）** | **Q5** / G3 / **C-8** |
| **T20.1-5** ★★ | `Renderer.ScaleLeavesBufferIntact` | `scale = 2.0f`，缓冲含 `Push → Draw → Pop`（含状态命令） | ① 命令**数量 / 类型 / 顺序**不变（A6）② ★★ **`commands` 内的几何逐位不变**（A2 / **C-10** 的唯一证据） | **A6** / C-10 |

★ **T20.1-4 与 T20.1-5 是本子阶段的两根承重桩**：前者钉住「不双重缩放」，后者把「禁止修改 `CommandBuffer`」从纪律变成**回归锚**。

**用例数：247 → 252**（5 条注册；与需求稿 §7 的「3–5」估计一致，取上界）。

---

## 8. 验收（需求 A1–A7 的落地口径）

| 需求 | 落地方式 |
|---|---|
| **A1**（150% 内容充满窗口） | 人工（主屏 125% / 跨屏 150%）；★ 与 Phase 20 的 A5/A6 合并做一次 |
| **A2**（100% 逐位零回归） | **既有 247 全绿** + **T20.1-1**（纯函数锚）+ **§2.5 的浮点论证** + 四工具链 |
| **A3**（缩放/移动/最大化同源同尺度） | 人工目视（改后绘制几何与命中几何仍同源——命中侧 Phase 20 已折，本稿只补绘制侧） |
| **A4**（落点唯一可 grep） | 脚本：`ScaleRect` / `ScalePoint` / `ScaleLength` 的**定义各 1 处**；`ExecuteCommand` 内**无**裸乘法 |
| **A5**（四工具链 + 测试全绿） | 本地（★ 报绿须写明 `_DEBUG` 是否启用） |
| **A6**（顺序/类型/数量不变） | **T20.1-5 ①** |
| **A7**（覆盖每种几何字段） | **T20.1-3**（`Rect` / `Point` / `width` / `cornerRadius`） |

---

## 9. 交给详细设计的五件事

| # | 事项 |
|---|---|
| **①** | **8 个 `ExecuteCommand` 重载的逐条改动规格**（含 `PopClipCommand` 的未用形参写法——★ 既有先例：它的 `cmd` 本就未被使用） |
| **②** | **默认实参的落点规则**（**只写声明**，`Renderer.cpp` 不得重复；误写即编译错误） |
| **③** | **测试期望值的逐点算准**（`scale = 1.5f` 下部分积在二进制中不可精确表示）⇒ 取值与容差须**脚本复算** |
| **④** | **A4 的 grep 判据具体式样**（判据必须区分**代码行与注释**） |
| **⑤** | **Phase 20 文档的回填清单**（需求稿 §8 已定）：初设 **§2.1 规则 3** 补「第二条换算边」· 详设 **§14 的 R1 标 ✅** |

---

## 10. 修订记录

- **v1.0**（2026-09-24）初稿。**输入**：需求稿 v1.1 · Phase 20 详设 §14 的 R1 · 用户实测。**内容**：B1–B11 代码基线（★ 新勘：**B2 勘误需求稿的调用点数 4 → 3** · **B5 `BeginFrame` 契约已被 Phase 18 冻结** · **B6 命令持有 string / Image ⇒ 否决「复制命令再折算」** · **B10 后端取整口径本就不统一**）· **七项定案**（Q1 落点 + A2 · ★ Q2 定案 `Execute(commands, scale)`，**与需求稿倾向相反** · Q3/Q4 · Q5/Q6/Q7）· ★ **§2.5 G2 的浮点结构性论证**（`×1.0f` 精确 ⇒ 无需特判）· ★ **§2.6 偏离与勘误三条**（C-6 计数 · 公共 API 精确化 · Q2 取向）· △1–△5 改动分解 · **C-7–C-11 新契约** · 影响面 · O1–O3 · **T20.1-1..5** · 验收 A1–A7 落地 · 交给详设五件事。**待评审。**
