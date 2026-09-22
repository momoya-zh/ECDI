# Phase 18 · 窗口/根背景能力（Window/Root background）—— 初步设计

> 状态：**v1.0 待评审**（2026-09-22）
> 输入：需求确认 **v1.1**（`docs/phase18-window-background-requirements.md`；外部评审：「**没有看到需求层面的阻塞问题**」，可进初步设计）
> 本稿的核心任务（**评审钉死**）：**钉死 `Window → Renderer → Backend` 的背景色数据流** + 回答 **Q5「唯一默认值来源」**——不是先写 API 签名

---

## 1. 设计输入与基线

### 1.1 需求阶段已定（本稿不再讨论）

| 项 | 已定内容 | 依据 |
|---|---|---|
| **纪律（已冻结）** | 清屏属**能力层**、颜色属**决策层** ⇒ **不得**让 Widget 层下发清屏指令；**不得**引入 `RenderCommand::Clear(Color)` 变体 | 需求 §1.3（评审要求「直接冻结，不要在后续设计里动摇」） |
| **路线** | **A：窗口级配置 + Backend 消费**（B 备选 / C 仅兜底） | 需求 §2 |
| **默认与边界** | 默认白 · 零回归 · **不做** Alpha/半透明 · **不做** root 背景（D2）· **不做** margin/渐变等（N1–N6） | 需求 R2 / D2–D4 / N1–N6 |
| **留初设** | D1 落点 · D5 恢复默认 · D6 命名 · Q1–Q5 | 需求 §4 / §6 |

### 1.2 代码基线（B1–B9，2026-09-22 逐条实测）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | **帧编排链**：`Window::PaintFrame()` → `m_renderer.BeginFrame()` → `Execute(commands)` → `EndFrame()`；**命令收集发生在 `BeginFrame` 之前** | `Window.cpp:120-129`（`:123` clear → `:125` root Paint → `:126` BeginFrame → `:127` Execute → `:128` EndFrame） |
| **B2** | **`Renderer` 是纯转发器**：`BeginFrame(){ m_backend.BeginFrame(); }` / `EndFrame(){ m_backend.EndFrame(); }`；持 `RenderingBackend&`（不拥有） | `Renderer.cpp:12` / `:22` · `Renderer.h:15` / `:30` |
| **B3** | **`Window` 同时持有** `m_renderBackend`（`unique_ptr`，从 `RenderServices` 取）**与** `m_renderer`（`*m_renderBackend` 引用）——声明顺序 `m_renderBackend` 在前 | `Window.cpp:44-47` |
| **B4** | `RenderingBackend::BeginFrame()` = **纯虚、无参**（`virtual void BeginFrame() = 0;`） | `RenderingBackend.h:39` |
| **B5** | ★ **`RecordingBackend::BeginFrame() override {}` 是空实现** ⇒ 帧边界在测试替身里**零痕迹**（`EndFrame` 同） | `RecordingBackend.h:86` / `:107` |
| **B6** | **唯一清屏点**：`FillRect(m_memoryDC, &client, (HBRUSH)GetStockObject(WHITE_BRUSH))` | `GDIBackend.cpp:260-261`（注释 = 决策 16） |
| **B7** | 替身的记录结构 = **每种操作一个公开 `std::vector<XxxDraw>`**（`draws` / `textDraws` / `lineCalls` / `roundedRectCalls` / `imageCalls` / `clipOps` / `focusRectCalls`），测试直接断言 | `RecordingBackend.h:76-84` |
| **B8** | `RenderServices` = `{unique_ptr<RenderingBackend> renderer, unique_ptr<TextMeasurer> measurer}`，**move-only**，注入 `Window` 构造 | `RenderServices.h:15-18` · `Window.cpp:42-47` |
| **B9** | **`BeginFrame()` 的场景外调用点共 6 处**（测试直接调后端，不经 Window）：`AntiAliasingTests.cpp:339/342` · `RendererTests.cpp:217/231/280/284` | 全库 grep（2026-09-22） |
| **B10** | ★ **GDI 画刷的既有模式 = 每次创建/销毁、不缓存**（注释即**决策 24**）；且 `ToColorRef(const Color&)` 已是可复用的静态转换（决策 21/23） | `GDIBackend.cpp:343-350` · `GDIBackend.h:62` |
| **B11** | `DrawRect` 有 `a < 1` 分支走 `BlendAlphaSolid`（预乘 DIB + AlphaBlend）；`a == 1` 走原 GDI 快速路径 | `GDIBackend.cpp:326-331` |

### 1.3 对评审意见的逐条处置

| # | 评审意见 | 处置 |
|---|---|---|
| 1 | §1.3 纪律「直接冻结」 | ✅ 已冻结（需求 v1.1）——本稿 **§2.3** 给出它是如何被**结构性满足**的 |
| 2 | **Q1 是核心：数据流不能含糊** | ✅ **本稿 §2 逐条回答五问**（谁存/谁读/谁调/从哪拿 + 红线） |
| 3 | **R2 第一优先 + 小心「三个默认值」** | ✅ **本稿 §2.4 + 契约 C2**：默认白**只在 `Window` 一处**，`BeginFrame` **不带默认实参** |
| 4 | Q2 倾向 ①（`BeginFrame` 内清屏） | ✅ 采纳——`BeginFrame` 仍是帧生命周期动作，命令流零改动（B1 的顺序不变） |
| 5 | C 案只是 workaround | ✅ 已在需求 v1.1 记明；本稿不重复 |
| 6 | D4 不做 Alpha | ⚠️ **本稿新增一处必须写明的边界**：背景色的 `alpha` **被忽略**（按实色处理，GDI `COLORREF` 无 alpha）——**不接** `BlendAlphaSolid` 路径（见 §3.3 / C4） |

### 1.4 需求 §6 的 Q1–Q5 → 本稿答案索引

| 需求稿问题 | 本稿落点 | 结论 |
|---|---|---|
| **Q1** 数据流五问 | **§2.1 / §2.2** | 颜色存 `Window`；每帧经 `Renderer::BeginFrame(color)` 传入；**Backend 不持状态、不认识 Window** |
| **Q2** 清屏形态 | §2.2 / §3.2 | **形态 ①**：`BeginFrame` 内用传入值清屏（**不新增 `RenderCommand` 变体**） |
| **Q3** `RecordingBackend` 如何表达清屏色 | §7 | 新增 `frameBackgrounds` 公开记录（**命令级可测**，与 B7 风格一致） |
| **Q4** 决策 16 注释与 roadmap 同步 | §5 / §8 | 落地时改 `GDIBackend.cpp:260` 注释 + `roadmap §7.7` 收口（D7） |
| **Q5** 唯一默认值来源 | **§2.4 + C2** | 默认白**唯一来源 = `Window::m_backgroundColor`**；另两层**不构成第二来源** |

---

## 2. ★ 数据流定案（本稿核心）

### 2.1 五问逐条回答

| # | 问题 | 答案 | 落地 |
|---|---|---|---|
| **①** | **谁保存 `Color`？** | **`Window`** —— 新增成员 `Color m_backgroundColor = Color::White();` | 颜色是**决策数据**，归决策层（§1.3 冻结纪律） |
| **②** | **谁在帧起始读取？** | **`Window::PaintFrame()` 自己** —— `:126` 处把成员作为实参传出 | 读取点与被写点同层，无需新通路 |
| **③** | **谁调用 `BeginFrame`？** | **不变**：仍是 `Window::PaintFrame()`（`:126`）· `Renderer` 仍只做转发 | 帧编排职责零变动 |
| **④** | **`GDIBackend` 从哪里拿到？** | **从 `BeginFrame` 的形参** —— 每帧传入，**Backend 不持有长期状态** | 见 §3.1 签名 |
| **⑤** | ★ **红线：Backend 是否知道 Window？** | **不知道** —— `RenderingBackend` 只多收一个 `const Color&`；无 Window/Widget/PaintContext 依赖（`RenderingBackend.h:22-24` 的自述保持成立） | §2.3 核查 |

**⇒ 数据流（终稿）**：

```
Window::m_backgroundColor        ← 唯一状态（决策层）
        │  PaintFrame() 每帧读取
        ↓
Renderer::BeginFrame(const Color&)   ← 纯转发（B2 性质不变）
        ↓
RenderingBackend::BeginFrame(const Color&)   ← 能力层收一个"本帧背景"参数
        ↓
GDIBackend::BeginFrame(const Color& bg)  → CreateSolidBrush(ToColorRef(bg)) + FillRect（沿用决策 24 模式）
```

### 2.2 两子方向对照（需求 Q1 的 (a) / (b)）

| 维度 | **(a) Backend 加配置接口**（`SetBackgroundColor`） | **(b) 颜色作 `BeginFrame` 输入**（**本稿采纳**） |
|---|---|---|
| 状态归属 | 颜色成为 **Backend 的长期状态** ⇒ **决策数据落在能力层**（与 §1.3 冻结纪律的定位相冲突） | 颜色留在 **`Window`** ⇒ 定位正确 |
| Window → Backend 通路 | 需 **Window 绕过 Renderer 直连 `m_renderBackend`**（B3 有通路，但**命令执行器不再是唯一入口**） | **不变**——仍经 `Renderer`，链路单一 |
| 接口变动面 | `RenderingBackend` **+1 虚方法** ⇒ 同步 `GDIBackend` + `RecordingBackend` | `RenderingBackend::BeginFrame` **改签名** ⇒ 同步 `GDIBackend` + `RecordingBackend` + **6 处测试调用点**（B9） |
| 默认值来源（Q5） | 接口带默认实参 ⇒ **Backend 侧又生一个默认**（正是评审警告的「三个默认值」） | **无默认实参** ⇒ 只有 `Window` 一处默认 |
| 「恢复默认」语义 | 需额外定义「未设置」状态 | 传 `Color::White()` 即恢复（D5 自然得解） |
| 清屏时机 | 配置时写入成员，帧时读取 | 每帧传入 ⇒ **运行期改色下一次重绘即生效** |

**⇒ 采纳 (b)，理由排序**：① 状态归属正确（Q5 直接受益）；② 不新增「默认值第二来源」；③ 链路不出现绕过 `Renderer` 的第二入口。**代价**（如实）：改 `BeginFrame` 签名 ⇒ 4 个实现/转发点 + **6 处测试调用点**适配（B9）。若评审认为该代价过重，(a) 可作为备选——但**必须同时解决它的默认值问题**。

### 2.3 红线核查（Backend 不认识 Window）

修改后 `RenderingBackend` 的全部依赖仍是：`Rect` / `Point` / `Color` / `Font` / `Image`（`RenderingBackend.h:9-13`）**+ 新增的 `Color`（本就在列表内）** ⇒ **零新依赖**、**零前置声明新增**。`GDIBackend` 收到的只是「一个颜色值」，与「谁给的、为什么给」无关 ⇒ **红线结构性满足**（§1.3 纪律的落地形态）。

### 2.4 Q5「唯一默认值来源」的落地（契约 C2 的设计依据）

| 层 | 是否有默认值 | 说明 |
|---|---|---|
| `Window` | ✅ **唯一**：`m_backgroundColor = Color::White()` | 语义来源；应用未配置时的取值 |
| `Renderer` | ❌ 无 | 纯转发（B2） |
| `RenderingBackend` / `GDIBackend` | ❌ 无 | 形参**必填、无默认实参**——这是**刻意的**：若给 `background = Color::White()`，Backend 接口里就会出现第二个「默认白」，R2 的零回归将有两个依据，改动任一处都可能静默改变默认行为 |
| `RecordingBackend` | ❌ 无 | 只记录收到的值（B5 现状：`BeginFrame` 是空的，本相位给它加上记录） |

**⇒ 结论**：`BeginFrame(const Color& background)` **不得写默认实参**。这条同时解释了 B9 那 6 处测试调用点**必须显式传色**（它们将传 `Color::White()`——即「显式表达既有默认行为」，而非依赖隐式默认）。

---

## 3. 接口改动分解

### 3.1 签名改动（逐文件 / 逐行）

| # | 文件 | 改动 |
|---|---|---|
| **△1** | `include/ECDI/Render/RenderingBackend.h:39` | `virtual void BeginFrame() = 0;` → `virtual void BeginFrame(const Color& background) = 0;` + 注释补「参数 = 本帧客户区底色（决策层输入，能力层不持有状态）」 |
| **△2** | `include/ECDI/Render/Renderer.h:17` + `src/Render/Renderer.cpp:12` | 转发签名同步：`void BeginFrame(const Color& background);` → `{ m_backend.BeginFrame(background); }` |
| **△3** | `src/Render/GDIBackend.h:38` + `.cpp:244` | `void BeginFrame(const Color& background) override;`；`:260-261` 清屏改用 `background`（见 §3.2） |
| **△4** | `src/Render/RecordingBackend.h:86` | `void BeginFrame(const Color& background) override;` —— 由空实现改为**记录**（见 §7） |
| **△5** | `include/ECDI/Window/Window.h` | **+1 公共 API**：`void SetBackgroundColor(const Color& color);` + **+1 成员** `Color m_backgroundColor = Color::White();`（唯一默认值来源，C2） |
| **△6** | `src/Window/Window.cpp:126` | `m_renderer.BeginFrame(m_backgroundColor);`；新增 `Window::SetBackgroundColor` 实现（赋值 + `Invalidate()`，见 O1） |
| **△7** | `src/Tests/AntiAliasingTests.cpp:339` · `src/Tests/RendererTests.cpp:217/231/280/284` | **6 处既有调用点显式传 `Color::White()`**（B9 / §2.4） |
| **△8** | `src/Render/GDIBackend.cpp:260` | **决策 16 注释同步**（D7）：「清屏白」→「清屏用本帧背景色（**默认**白；Phase 18 起可配）」 |

**公共头 92 → 92**（不新增头；`RenderingBackend.h` / `Renderer.h` / `Window.h` 均为既有头）。

### 3.2 清屏点的实现（沿用决策 24，零新概念）

```cpp
// GDIBackend::BeginFrame(const Color& background)
...
// 决策 16（Phase 18 修订）：清屏用本帧背景色——默认白由 Window 给出，本层不持有默认
// 决策 24：画刷每次创建/销毁（无缓存）——与 DrawRect 同款
HBRUSH brush = CreateSolidBrush(ToColorRef(background));
if (brush){
    FillRect(m_memoryDC, &client, brush);
    DeleteObject(brush);
} else {
    // 决策 30：局部失败跳过（此处退化为不刷新底色，帧其余部分照常绘制）
    FRAMEWORK_ASSERT(false && "CreateSolidBrush failed");
}
```

三点说明：
- **`ToColorRef` 复用现成静态转换**（`GDIBackend.h:62`，决策 21/23 的 `ToByte` Clamp 已内含）⇒ **不新增转换**。
- **不引入画刷缓存**——`DrawRect` 的解释（`:343`「决策 24：画刷每次创建/销毁（无缓存）」）在本场景同样成立：清屏**每帧一次**，频率与一次 `DrawRect` 等同。
- **失败路径**：`CreateSolidBrush` 失败的实际后果 = 底色不刷新（而非崩溃）；`FRAMEWORK_ASSERT` 仅 `_DEBUG` 生效（skill 条 50 语境）⇒ 记入契约 C5 的边界。

### 3.3 alpha 的边界（B11 相关，必须写明）

- `DrawRect` 对 `a < 1` 走 `BlendAlphaSolid`（预乘 DIB + AlphaBlend）；**背景色不走该路径**。
- `ToColorRef` 只取 RGB ⇒ **`background.a` 被忽略**（半透明底色被当作实色画）。
- 这是**刻意的**（需求 D4：Alpha/半透明属 Phase 9），且**不需要额外代码**——但必须写成契约（C4），否则实现者可能"顺手"接上半透明分支，把 Phase 9 的范畴拖进来。

---

## 4. 契约（C1–C5）

| # | 契约 | 验证方式 |
|---|---|---|
| **C1** | **默认零回归**：不配置时 `Window` 传白 ⇒ 清屏白 ⇒ 与现状**逐位相同**（同 Phase 17 C1「逐位退化」） | T18-1（守门）+ T18-4 |
| **C2** | **唯一默认值来源**：默认白**只在 `Window` 一处**；`Renderer` 无默认；**`BeginFrame` 形参无默认实参**；`GDIBackend` 不持有默认 | T18-1/T18-2 的**调用形态**即证据（6 处调用点必须显式传参）+ 结构性检查（接口无默认实参） |
| **C3** | **决策层给值、能力层执行**：`RenderingBackend` 只多收一个 `const Color&`，**不持有状态、不认识 `Window`/`Widget`/`PaintContext`** | §2.3（依赖清单零新增）+ A 类结构性判据（见 §8） |
| **C4** | **alpha 被忽略**：背景色按实色处理；**不接** `BlendAlphaSolid` 路径 | T18-5（命令级：记录到的 `Color` 原样；GDIBackend 侧不产生 BlendAlpha 调用） |
| **C5** | **每帧传递、无缓存**：背景色在**每次** `BeginFrame` 传入 ⇒ 运行期改色后下一次重绘生效 | T18-2 / T18-3（改色 → 重绘 → 记录变化） |

---

## 5. 影响面

| 项 | 实测 / 预计 |
|---|---|
| 改动文件 | **7**（`RenderingBackend.h` · `Renderer.h/.cpp` · `GDIBackend.h/.cpp` · `RecordingBackend.h` · `Window.h/.cpp`）+ **2 个测试文件**（6 处调用点） |
| 公共头 | 92 → **92** |
| **公共 API** | **+1**（`Window::SetBackgroundColor`）——★ 与 Phase 16「净增 0」不同，**本相位确有 API 增加**，如实记 |
| `Renderer` 接口 | `BeginFrame` 改签名（**结构不变**：仍纯转发、仍持引用） |
| `RenderingBackend` 接口 | **1 个纯虚改签名**（方法数不变 ⇒ 实现者数量不变，但**每个实现者都要改**） |
| `RecordingBackend` | +1 记录成员（`frameBackgrounds`）+ `BeginFrame` 由空实现改为记录 |
| 测试调用点 | **6 处**（B9）+ 新增 T18-1..T18-6 |
| 断言特征串 | **11 → 11**（无新增框架断言；若 §3.2 的 `FRAMEWORK_ASSERT` 落地则 **11 → 12**，**待定**见 O2） |
| 用例 | 226 → **226 + N** |
| 决策 16 注释 + `roadmap §7.7` | 落地时同步（D7 / Q4） |
| `examples/ModelProbe/main.cpp` | ★ 若用 root 级 padding + 底色做 R3 组合目视，**须单独授权**（skill 条 2） |

---

## 6. 开放决策点（O 系列）

| # | 决策 | 倾向 | 说明 |
|---|---|---|---|
| **O1** | `SetBackgroundColor` 是否立即 `Invalidate()` | ✅ **是** | 否则运行期改色不重绘（须等下一次外部重绘才可见）；与「运行期可改」的定位配套。备选：仅赋值、由调用方自行 `Invalidate()`（与 Phase 16 配置期 API 的「不隐含副作用」风格一致）——**需拍板** |
| **O2** | §3.2 的 `FRAMEWORK_ASSERT(false && …)` 是否保留 | ⚠️ **倾向不保留** | `CreateSolidBrush` 对合法 `COLORREF` 几乎不会失败；加它会把断言特征串推到 12 条（A2 类核验需同步）。**倾向**：不加断言，失败即静默跳过（决策 30 的既有姿态），把这条记入 C5 的边界说明 |
| **O3** | 是否做 **GDI 像素级**验收（真读回客户区像素验证底色真的变了） | ⚠️ **倾向不做** | 命令级（T18-1..T18-3 经 `RecordingBackend` 记录）+ **目视**（ModelProbe）已覆盖；像素级基建属 `AntiAliasingTests` 那套（本相位不为它扩范围）。若评审要求，可加 T18-7（代价：需真实 HDC 搭建） |
| **O4** | `SetBackgroundColor` 是否给 getter | ❌ **不做** | Phase 17 的 O2 先例（不为可测性扩 API；测试可直接观察 backend 收到的值） |

---

## 7. 测试方向（T18-1..T18-6）

**装置**：`RecordingBackend` 新增 `std::vector<Color> frameBackgrounds;`（与 B7 的既有风格一致：**公开成员 + 每调用一条记录**），`BeginFrame(const Color& bg)` 里 `frameBackgrounds.push_back(bg);`

| # | 用例（建议名） | 判据 |
|---|---|---|
| **T18-1** | `WindowBackground.DefaultWhite` | 不配置 ⇒ `PaintFrame` 后 `frameBackgrounds.back() == Color::White()`（**C1 守门**；同时证明「默认值确实由 Window 给出」） |
| **T18-2** | `WindowBackground.CustomColorReachesBackend` | `SetBackgroundColor(C)` ⇒ 重绘 ⇒ 记录到 `C`（C5 / 数据流端到端） |
| **T18-3** | `WindowBackground.ChangeTakesEffectNextFrame` | 两次 `PaintFrame` 之间改色 ⇒ 两条记录不同（运行期生效，O1 的配套证据） |
| **T18-4** | `WindowBackground.ExplicitWhiteEqualsDefault` | 显式 `SetBackgroundColor(Color::White())` 与不配置 ⇒ 记录**逐位相同**（C1 的另一面：默认与显式同义） |
| **T18-5** | `WindowBackground.AlphaIgnored` | 传 `a = 0.5` 的色 ⇒ 记录到的 `Color` 原样（**命令层不替他做 alpha 决策**）+ 断言**未**触发任何 Blend 路径（C4） |
| **T18-6** | （适配，非新增）| **6 处既有调用点显式传白**后，`AntiAliasing` / `Renderer` 既有用例**全绿**（零回归；B9 / §2.4） |

**用例数**：226 → **226 + 5**（T18-1..T18-5；T18-6 是既有用例适配，不计新增）。

---

## 8. 验收（A 系列，初稿）

| # | 项 | 判据 |
|---|---|---|
| **A1** | 四工具链构建 + 全绿 | 226 + 5 用例；MSVC / ClangCL / Clang / MinGW |
| **A2** | 断言启用核验 | 特征串 **11**（若 O2 保留断言则 12）|
| **A3** | 零回归 | 既有 226 用例全绿（**尤其** B9 那 6 处适配后的 `AntiAliasing` / `Renderer` 用例） |
| **A4** | ★ **结构性判据：`RenderingBackend` 的依赖零新增** | 头文件依赖清单 = `Rect`/`Point`/`Color`/`Font`/`Image`（§2.3）；**不得**出现 `Window` / `Widget` / `PaintContext` 的前置声明或 include |
| **A5** | ★ **结构性判据：默认值唯一来源** | 全库 grep：`Color::White()` 在**渲染链路**（Window 成员初始化）只出现 1 处；`BeginFrame` 声明**无默认实参**（源码级检查） |
| **A6** | 目视 | ModelProbe：root 级 padding + 非白底色 ⇒ 露出的四边是**配置色**而非白（R3 组合；**须授权**动 `main.cpp`） |
| **A7** | 文档与索引同步 | 决策 16 注释（D7）· `roadmap §7.7 #39` 收口 · 两份 README 规模锚点 |

---

## 9. 修订记录

- **v1.0**（2026-09-22）**初步设计初稿**。输入 = 需求确认 **v1.1**（评审「无阻塞」）。① **§1.2 代码基线 B1–B11 全部带行号实测**——其中 **B1**（帧编排顺序：命令收集在 `BeginFrame` 之前）· **B2**（`Renderer` 是纯转发器）· **B5**（★ `RecordingBackend::BeginFrame` 是**空实现** ⇒ 帧边界在替身里零痕迹）· **B9**（6 处场景外调用点）· **B10**（★ **GDI 画刷的既有模式 = 决策 24「每次创建/销毁、不缓存」**）是决定本稿形态的关键五条；② **§2 数据流定案**——五问逐条回答（颜色存 `Window` · `PaintFrame` 读取 · 调用点不变 · 经 `BeginFrame` 形参传入 · **Backend 不认识 Window**）；**两子方向对照后采纳 (b)「颜色作 `BeginFrame` 输入」**，理由按序为「状态归属正确」→「不给 Backend 生第二默认值」→「不出现绕过 `Renderer` 的第二入口」，代价如实记为 4 个实现/转发点 + 6 处测试调用点；③ **§2.4 回答 Q5**：默认白**唯一来源 = `Window::m_backgroundColor`**，并据此推出 **`BeginFrame` 形参不得写默认实参**（否则 Backend 侧又生一处默认）；④ **§3.2 清屏实现沿用决策 24**（`CreateSolidBrush` + `FillRect` + `DeleteObject`，复用 `ToColorRef`）⇒ **零新概念**；⑤ **§3.3 显式处理 alpha**（`ToColorRef` 丢弃 alpha ⇒ 半透明底色按实色画）——**不需要额外代码，但必须写成契约 C4**，否则实现者可能顺手接 `BlendAlphaSolid` 把 Phase 9 范畴拖进来；⑥ **契约 C1–C5**（C2「唯一默认值来源」与 C4「alpha 忽略」为本稿新增的两条）；⑦ **§5 影响面如实记「公共 API +1」**（与 Phase 16 的「净增 0」不同）；⑧ **开放点 O1–O4**（O1 运行期改色是否隐含 `Invalidate` · O2 是否保留 `FRAMEWORK_ASSERT`（影响特征串 11/12）· O3 是否做像素级 · O4 不给 getter）；⑨ **§7 测试 T18-1..T18-6** + **§8 验收 A1–A7**（A4/A5 是**结构性判据**：依赖零新增 / 默认值唯一来源）。**待评审。**
