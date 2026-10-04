# Phase 27 · 绘制命令构建层的视口剔除（viewport culling）—— 初步设计（v1.0）

> 来源：需求稿 `phase27-viewport-culling-requirements.md` **v1.1 ✅ 评审通过**（外部评审 2026-10-04：「**可以进入 Preliminary Design**」，并给定**初设必答六问**——需求稿 §4.1）
> 状态：**v1.1**（2026-10-04）——✅ **评审通过（可进入详设）**
> ★ 外部评审结论（初设轮）：「**Phase 27 Preliminary Design：PASS，可以进入 Detailed Design**」——13 项分项全部通过（坐标系 / ContentOffset / Initial Clip / **OnPaint 副作用 = 本稿最大进展** / Intersects 几何 / float-lround 契约 / Paint 接缝 / subtree skip 语义 / C-VIS 共存 / 像素等价 / 测试覆盖 / 批次 / 公共 API「有一个需详设讨论的小问题」）；**无任何需要推翻架构的回退点**。★ 评审给定**详设盯防五件事**（见 §1.1）。
> 定位：**冻结几何与语义、回答必答六问**。★ 评审要求 D3（OnPaint 副作用）为第一优先级 ⇒ 本稿 **§1-Q4 给出全库盘点实测表与判定**；★ 范围锁沿需求 §3.2（不做虚拟化 / 脏区 / 后端执行端 / OnPaint 纯绘制重构）。

---

## 1. 初设必答六问（需求稿 §4.1 逐题作答——本稿核心）

### Q1 ★ `PaintContext` 的 clip 采用什么坐标系？

**答案：全框架只有一个坐标系 = Window 客户区绝对坐标（`Window::PaintFrame` 的根偏移 (0,0) 系）。不存在「内容坐标系 vs 视口坐标系」两套坐标——需求稿 D2 担忧的情形在结构上不存在。**

**论证（带代码）**：`Widget::Paint(PaintContext&, int offsetX, int offsetY)`（`Widget.cpp:248`）在入口处做翻译：

```cpp
int x = offsetX + static_cast<int>(m_geometry.x);   // m_geometry 是「相对父」坐标
```

即 **child 的几何在进入自己的 Paint 之前已被祖先逐层翻译成绝对坐标**。`ScrollView` 的 `ContentOffset` 也不例外：基类循环把 `childOrigin = x − GetContentOffsetX()`（`Widget.cpp` Paint 体，Phase 15 接缝）传下去，child 的 `x = childOrigin + child.geometry.x` **已经是绝对坐标**（滚出视口的行得到负值或超界值——正是剔除判据要的东西）。而既有 `PushClip` 推入的自身边界（`ctx.PushClip(Rect{float(x), float(y), w, h})`，`Widget.cpp` Paint 体）同样用这个绝对 x/y。

⇒ **`widgetRect`（`{float(x), float(y), m_geometry.width, m_geometry.height}`）与既有裁剪区天然同系**，判据直接 `Intersects(self, 当前累计裁剪)`，**零换算、零特判**。评审定调「必须在同一坐标系中判断」由此**结构性满足**，而非靠纪律维持。

### Q2 ★ `ContentOffset` 在进入剔除判定前在哪里完成坐标转换？

**答案：已经完成了——就在既有 `Widget::Paint` 的 `childOrigin` 翻译里（Phase 15 接缝，`Widget.cpp` Paint 体）。本 Phase 在此路径上零新代码。**

`PaintContext` **不感知** `ScrollView` / `ContentOffset` / viewport / scrolling（评审定调的禁令成立且零成本）：`PaintContext` 只维护「当前 Paint 坐标系（= 绝对客户区）里的累计裁剪交集」，见 D27-1。需求稿 D2 的 ⚠️ 待定项就此关闭。

### Q3 ★ Initial clip 是什么？Root 从哪里开始？

**答案：`PaintContext` 增加构造期可选的初始裁剪矩形；`Window::PaintFrame` 注入 client 矩形 `{0, 0, clientW, clientH}`；测试 / 工具路径不注入 = 无界。初始 clip 只进构建侧交集栈，**不发任何命令**。**

- **执行端的真实边界**：GDI 内存 DC = client 尺寸 DIB、GL framebuffer = client 尺寸——绘制**本来就出不了 client**。构建栈以 client 矩形作种子，恰是执行端边界的**精确镜像**（D27-1 镜像不变量的起点），也让整棵树从第一层起就有有限 clip（评审倾向，采纳）。
- **注入形态**：`PaintContext` 新增构造重载（`CommandBuffer&`, `TextMeasurer&`, `const Rect& initialClip`）；既有双参构造保留（栈空 = 无界）⇒ 既有测试 / `RecordingBackend` 路径零改动。
- **Root 起点**：`Window::PaintFrame`（`Window.cpp:123-141`）既有 `m_rootWidget->Paint(ctx, 0, 0)` 不变；根 Widget 自身通常覆盖 client，初始 clip 在根层即与自身矩形求交，语义统一。

### Q4 ★★ 完整盘点所有 `OnPaint` override 的副作用（初设第一优先级）

**盘点方法**：`grep "::OnPaint(" src/ examples/` 全库枚举（12 处 override，含基类空实现），逐个读函数体，搜 `Set* / Invalidate* / Layout* / Update* / Sync* / Create* / Destroy*` 与成员写操作。

| # | Override | 函数体分类 | 副作用 | 判定 |
|---|---|---|---|---|
| 0 | `Widget::OnPaint`（基类，`Widget.cpp`） | 空函数 | 无 | 纯 |
| 1 | `Panel::OnPaint`（`Panel.cpp:42`） | 纯绘制（背景 / 边框环） | 无 | 纯 |
| 2 | `Label::OnPaint`（`Label.cpp:15`） | `DrawTextContent` → `MeasureText`（D2 帧无关）+ `DrawText` | 无 | 纯 |
| 3 | `Button::OnPaint`（`Button.cpp:218`） | 纯绘制（读 `m_displayedBackground` / `HasFocus()`——只读） | 无 | 纯 |
| 4 | `CheckBox::OnPaint`（`CheckBox.cpp:65`） | 纯绘制 | 无 | 纯 |
| 5 | `Radio::OnPaint`（`Radio.cpp:91`） | 纯绘制 | 无 | 纯 |
| 6 | `ProgressBar::OnPaint`（`ProgressBar.cpp`） | 纯绘制（半径绘制时算——只读） | 无 | 纯 |
| 7 | `ScrollBar::OnPaint`（`ScrollBar.cpp:217`） | 纯绘制 | 无 | 纯 |
| 8 | `TextBox::OnPaint`（`TextBox.cpp:1193`） | 绘制 + **两处惰性同步**（下详） | **有（幂等同步型）** | **可容忍** |
| 9 | `CaptionBar::OnPaint`（`CaptionBar.cpp:119`） | 绘制 + **`m_maxButton->SetGlyph(...)`**（`CaptionBar.cpp:122`） | **有（幂等同步型）** | **可容忍** |
| 10 | `CaptionButton::OnPaint`（`CaptionButton.cpp`） | 纯绘制（glyph 点线） | 无 | 纯 |
| 11 | `VisualTest ImageHost::OnPaint`（`examples/VisualTest/main.cpp:88`） | `Panel::OnPaint` + `DrawImage` | 无 | 纯 |

**两处副作用的逐个判定**：

- **`TextBox::OnPaint` 起始的 `SyncScrollBar()`（`TextBox.cpp:1198`）+ `RecalculateLines()`（`TextBox.cpp:1264`，`m_needsLineRecalc` 惰性重算）**：二者都是「**惰性求值 + 同步到当前状态**」型——把 `SetText` / `SetFont` 等入口欠下的账在下次绘制时补齐。被剔除的帧**推迟**（而非丢失）执行：滚回可视区后的第一帧 Paint 恢复遍历即补执行（需求 A3 底线）。caret 闪烁的 `m_showCaret` 翻转在 `OnTimer`（`TextBox.cpp:1003`，翻转 + `Invalidate()`），**不在 OnPaint 内**——绘制只读该 flag。★ **关键反证：caret 的状态推进由 Timer 驱动、不经 Paint** ⇒ 剔除不会停摆任何时间敏感状态。
- **`CaptionBar::OnPaint` 的 `SetGlyph`（`CaptionBar.cpp:122`）**：`SetGlyph` 是**纯字段写入**（`CaptionButton.h:27`，`{ m_glyph = glyph; }`，无 Invalidate）。同步源 = 窗口状态，而最大化 / 还原**必产生 WM_SIZE → Invalidate → 全窗重绘**（该函数注释自证的设计闭环）⇒ CaptionBar（常驻窗口顶部、恒与视口相交）不会在状态变化后被剔除跳过。★ 语义 = 「绘制前读现在是什么」的幂等刷新，跳过 N 帧后再执行结果相同。

**★ 全库 `OnPaint` 中不存在任何 `Invalidate` 调用**（评审最担心的「Paint → OnPaint → Invalidate → 下一帧」状态推进循环类**为零**）⇒ **D3 判定 = 全部可容忍；D4 恒开就此冻结**（D27-6）。★ 盘点结论冻结为本 Phase 契约（C27-7）：新增 OnPaint 副作用须过本盘点口径复核。

### Q5 ★ `Rect::Intersects` 的边界定义与 float → int 的唯一规则

**答案：`ECDI/Core/Rect.h` 新增 inline 自由函数 `Intersects(const Rect&, const Rect&)`——在 float 上直接判（与推入裁剪的矩形**同源**，不引入第二量化），相交定义 = **交集宽 > 0 且交集高 > 0**（严格大于；边界接触 = 不相交 = 剔除）。需求稿 D2 的「int 口径」倾向据此**订正为 float 同源口径**。**

```cpp
// ECDI/Core/Rect.h（header-only，公共 API +1）
inline bool Intersects(const Rect& a, const Rect& b) noexcept {
    const float ix = (std::max)(a.x, b.x);
    const float iy = (std::max)(a.y, b.y);
    return (std::min)(a.x + a.width,  b.x + b.width)  - ix > 0.0f
        && (std::min)(a.y + a.height, b.y + b.height) - iy > 0.0f;
}
```

- **为何 float 同源**（Phase 26 §11.9 教训的直接应用——同一几何量的多处消费必须同一量化）：判据消费的矩形与 `PushClip` 推入的矩形**必须是同一份值**。构建侧引入「先截断到 int 再判」= 在「几何（float）→ 推入（float）→ 执行端量化（lround）」链上多插一层私有量化 ⇒ 两处量化规则漂移的温床。
- **安全性证明（剔除恒不丢像素）**：执行端两后端的裁剪语义已实测同构——`GDIBackend::PushClip`（`GDIBackend.cpp:638-653`）与 `GLRenderer::PushClip`（`GLRenderer.cpp:361-382`）都对四边 `std::lround` 后求交；GDI `IntersectClipRect` 遵循 Windows RECT 语义（右 / 下边**开**——`[l, r)` 列可见），`glScissor(x, y, w, h)` 可见 ⟺ `x ≤ i < x+w` 同为半开。设构建侧判 `a` 与 `b` 不相交（`a.right ≤ b.left` 或对称情形）：`lround` 单调 ⇒ `lround(a.right) ≤ lround(b.left)` ⇒ 执行端两量化矩形的交集**为空**。∎ 反向（相交但执行端画不出像素，如 0.5px 残条被量化吞掉）= 过包含，安全无害。★ 该证明同时解释了此前一处外部试验（scissor 宽高 +1）为何修坏：它把 GL 改成闭区间、破坏了与 GDI 一致的半开契约——**勿回退**。
- **边界接触判定举例**：widget `[100, 150]`、clip `[150, 250]` ⇒ 交集宽 = 0 ⇒ **剔除**（接触列面积为零，不可能产生像素；执行端 `[150,150)` 亦空——两侧一致）。

### Q6 ★ A2 像素等价测试如何固定非确定因素

**答案：测试契约四条（写进 T27-10 场景定义）**：① **静态场景**——全部控件样式 / 文本 / 几何在对照两帧间零变更；② **无焦点控件**——不设 `SetFocusedWidget`（TextBox 不获焦 ⇒ 无 caret blink timer 路径，`OnTimer` 不启动）；③ **固定 DPI**——单窗口单 DPI（120），不跨屏；④ **无动画 / 无定时器**——场景不含 Button 动画值（`m_displayedBackground` 静态）且对照帧间不推进任何 Timer。对照方法 = 沿 Phase 26 无头 GL 探针先例（屏幕外窗口 + `glReadPixels`）：同一棵树、同一布局，`SetCullingEnabled(false)` / `(true)` 各渲染一帧，framebuffer 逐字节比对。**任何字节差 = culling 实现缺陷**（场景已确定性化，不存在「场景自己变了」的歧义）。

---

### 1.1 ★ 详设盯防五件事（评审给定，v1.1 吸收）

1. **冻结 `PaintContext` clip stack 的精确数据结构与 initial clip 生命周期**。
2. **冻结 `Widget::Paint` 中 `self` 的构造方式**——确保 culling rect 与 PushClip **永远同源**（评审：「先拿同一个 `self` 判断，再把同一个 `self` Push 进去」的设计值得保留）。
3. **`SetCullingEnabled(false)` 的 API 可见性最终定下来**（★ 评审认为**最值得讨论**的点——「测试能关 culling」与「用户能在自己的 OnPaint 里关 culling」是两件事；方案 A 公共 API vs 方案 B 测试/内部缝的比较落详设，见 O3 升级）。
4. **把 T27-5/T27-6/T27-7 的 subtree + ScrollView 坐标场景画成精确测试树**（核心行为 = **subtree pruning 而非单 Widget draw pruning**——Parent 部分相交保留 ⇒ Child A 视口外连同其子树整段跳过、Child B 相交保留其子树）。
5. **列出所有现存命令流测试的对账清单**（O2 兑现）。

---

## 2. 代码基线（带行号实测 2026-10-04）

| # | 事实 | 位置 |
|---|---|---|
| B1 | `Widget::Paint`：`IsVisible` 早退 → 算 x/y → `PushClip(自身边界)` → `OnPaint` → children 循环 → `PopClip`；**无任何视口判定** | `Widget.cpp:248-277` |
| B2 | `PaintContext` = 纯命令收集门面，**无裁剪状态**：`PushClip`/`PopClip` 只 `emplace_back` 状态命令 | `PaintContext.h:55-60` · `PaintContext.cpp:50-62` |
| B3 | `PaintContext` 构造 = `(CommandBuffer&, TextMeasurer&)`，每帧栈上创建 | `PaintContext.h:23` · `Window.cpp:136` |
| B4 | `Window::PaintFrame`：`m_commands.clear()` → 构造 ctx → `m_rootWidget->Paint(ctx, 0, 0)` → Begin/Execute/End；**无初始 client 裁剪** | `Window.cpp:123-141` |
| B5 | `Rect` 纯聚合（4 个 float），无任何方法 | `Rect.h` |
| B6 | childOrigin 翻译：`childOriginX = x − GetContentOffsetX()`（ContentOffset 已烘焙进绝对坐标） | `Widget.cpp` Paint 体（Phase 15 接缝） |
| B7 | GDI 执行端裁剪：四边 `lround` → `IntersectClipRect`（RECT 语义右/下开） | `GDIBackend.cpp:638-653` |
| B8 | GL 执行端裁剪：四边 `lround` → 栈内求交 → `glScissor`（半开） | `GLRenderer.cpp:361-382,685-699` |
| B9 | OnPaint override 全集 = 12 处（src 11 + examples 1），副作用 2 处（§1-Q4 表） | 全库 grep |
| B10 | `ScrollView::SetContentOffset` 唯一入口（clamp + sync + Invalidate）；滚轮经它 | `ScrollView.cpp:113,348` |

---

## 3. 决策定案

### D27-1 ★ 接缝：构建侧交集栈放进 `PaintContext`（需求 D1(a) 兑现）

`PaintContext` 增加私有 `std::vector<Rect> m_cullStack`：
- `PushClip(rect)`：**先**对栈顶（或初始 clip）求交入栈，**再**照旧发射 `PushClipCommand`（命令流零变化）；
- `PopClip()`：弹栈 + 照旧发射命令；
- 新查询 `IsRectVisible(const Rect&) const`：栈空且无初始 clip = 无界 ⇒ 恒 true；否则对栈顶做 `Intersects`。

★ **镜像不变量（C27-1）**：**构建栈顶 ≡ 执行端此刻的有效裁剪**（同一批 float 矩形、同一求交序；执行端多一层 lround 半开量化，§1-Q5 证明其不产生可见差异）。剔除判据 = 对「自己 Push 之前」的栈顶判交——正是「祖先们将为这个子树提供的裁剪」。

### D27-2 ★ 坐标系：单一绝对客户区（Q1/Q2，零换算代码）

见 §1-Q1/Q2。`PaintContext` 不新增任何坐标参数；`ScrollView` 不感知本 Phase。

### D27-3 ★ 判据：float 同源 + 严格 >0（Q5；需求 D2 口径订正）

`Intersects` 落 `Rect.h` 公共自由函数；不加安全边距（需求既定）；边界接触 = 剔除。**int 口径订正的理由**（记档）：int 先量化再判会引入与 `PushClip` 值不同的第二份几何——正是 Phase 26 裁剪区量化缺陷（详设 §11.9）的同族错误；float 同源 + lround 半开执行的安全性有 §1-Q5 证明兜底。

### D27-4 ★ 判定位置与顺序：`Widget::Paint` 入口、任何命令之前

```cpp
void Widget::Paint(PaintContext& ctx, int offsetX, int offsetY){
    if (!IsVisible()) return;                       // ① C-VIS（Phase 25，不动）
    int x = offsetX + static_cast<int>(m_geometry.x);
    int y = offsetY + static_cast<int>(m_geometry.y);
    const Rect self{ static_cast<float>(x), static_cast<float>(y),
                     m_geometry.width, m_geometry.height };
    if (!ctx.IsRectVisible(self)) return;           // ② ★ 剔除——不 Push 不 Pop 不进 OnPaint
    ctx.PushClip(self);                             // ③ 以下原序不动
    OnPaint(ctx, x, y);
    ... children ...
    ctx.PopClip();
}
```

- **顺序 = 评审给定序**：`IsVisible → 剔除 → PushClip → OnPaint → children → PopClip`；两道早退共用「不 Push 不 Pop」的例外路径语义（不变量 I1 天然平衡）。
- **`self` 的构造与既有 `PushClip` 实参逐位同一**（同一表达式）⇒ 判据消费的矩形与随后推入的矩形**按构造同源**。
- **与 C-VIS 正交**（评审确认）：`IsVisible` 管隐藏子（停泊负区），剔除管「可见但在视口外」；顺序固定不交换（隐藏子不必算几何）。

### D27-5 ★ 初始 clip：构造注入 client 矩形，不发命令（Q3）

见 §1-Q3。既有双参构造保留 = 栈空无界 ⇒ 既有测试 / `RecordingBackend` 零改动。

### D27-6 ★★ D3 判定：副作用全部可容忍 ⇒ 恒开冻结（Q4；需求 D4 兑现）

盘点表（§1-Q4）冻结为契约 C27-7。**本 Phase 不做 OnPaint 纯绘制重构**（需求既定）；「让 OnPaint 更接近纯绘制」作为**未来架构候选**另账登记（§10 O4）。

### D27-7 ★ 对照缝：`SetCullingEnabled(bool)`（默认 true）

`PaintContext` 新增开关：false 时 `IsRectVisible` 恒 true（构建栈**照常维护**——镜像不变量不受开关影响）。用途：① A2 像素等价的剔除开 / 关对照；② D4 的潜在降级基础设施（若未来出现不可容忍副作用的全局逃生门，先于 Widget 级 opt-out）。★ 语义 = 调试 / 测试缝，不改变生产行为。

### D27-8 ★ 剔除语义与退化矩形

剔除 = 整段跳过（`PushClip`/`OnPaint`/children/`PopClip` 全部不发生，A1 的「整段验证」由此直接成立）。**退化矩形（宽或高 ≤ 0）恒被剔除**——与执行端「0 宽裁剪 = 不可见」逐像素等价；其 OnPaint 副作用推迟语义已由 C27-7 白名单覆盖（盘点中无退化矩形场景的实际消费者）。

---

## 4. 接口草案（公共 API +3，公共头 94 → 94）

| 变更 | 头 | 性质 |
|---|---|---|
| `inline bool Intersects(const Rect&, const Rect&)` 自由函数 | `ECDI/Core/Rect.h`（header-only） | **公共 API +1** |
| `PaintContext::IsRectVisible(const Rect&) const` | `ECDI/Render/PaintContext.h` | **公共 API +1**（查询） |
| `PaintContext::SetCullingEnabled(bool)` | `ECDI/Render/PaintContext.h` | **公共 API +1**（对照缝） |
| `PaintContext` 构造重载 `(CommandBuffer&, TextMeasurer&, const Rect& initialClip)` | `ECDI/Render/PaintContext.h` | 重载（不另计——既有双参签名不动） |
| `Window::PaintFrame` 改用三参构造传 client 矩形 | `src/Window/Window.cpp` | 实现内部 |

**零改动面**：`RenderingBackend` / `Renderer` / `RenderCommand` / 两后端 / `ScrollView` / `TextMeasurer` / 全部控件头。

---

## 5. 契约

| # | 契约 |
|---|---|
| **C27-1** | **镜像不变量**：构建栈顶 ≡ 执行端有效裁剪（同一批矩形、同一求交序；执行端 lround 半开量化不产生可见差异——§1-Q5 证明） |
| **C27-2** | 剔除 = `PushClip`/`OnPaint`/children/`PopClip` **整段**不发生、零命令（A1 的语义基础） |
| **C27-3** | 判据 = 交集宽 > 0 且高 > 0（float 同源；边界接触 = 剔除；不加边距） |
| **C27-4** | 顺序 = `IsVisible → 剔除 → PushClip → OnPaint → children → PopClip`（固定不交换） |
| **C27-5** | 初始 clip：`Window` 注入 client 矩形（只进构建栈不发命令）；无注入 = 无界 |
| **C27-6** | `SetCullingEnabled(false)` ⇒ 命令流与 Phase 26 之前**逐位一致**（A2 对照基准） |
| **C27-7** | **OnPaint 副作用白名单 = §1-Q4 表**（2 处幂等同步型）；新增副作用须按同口径复核 |

---

## 6. 盯防（可机检）

| # | 盯防项 | 判据 |
|---|---|---|
| ① | 剔除判定必须在 `PushClip` 之前 | `Widget.cpp` Paint 体行序：`IsRectVisible` 行号 < `PushClip` 行号 |
| ② | 栈维护与命令发射同函数同序 | `PaintContext::PushClip` 内先栈操作后 `emplace_back`（或反之但固定）；两路径不可拆函数 |
| ③ | `Intersects` 严格大于 | `Rect.h` 中无 `>=` 判交 |
| ④ | `PaintContext` 零滚动感知 | `grep -c "ScrollView\|ContentOffset\|viewport" PaintContext.*` = **0** |
| ⑤ | OnPaint 副作用白名单不膨胀 | 新增 `OnPaint` override 含 `Set*/Invalidate*/…` 时须更新 §1-Q4 表与 C27-7（评审纪律） |
| ⑥ | 执行端量化不回退 | `GDIBackend.cpp` / `GLRenderer.cpp` 的 `lround` 裁剪量化保持；**禁止**再引入 ±1 类闭合修正（§1-Q5 反例） |
| ⑦ | `self` 构造与 `PushClip` 实参同源 | `Widget.cpp` 中两处为同一表达式（不复制第二份几何公式） |

---

## 7. 用例（T27-1..T27-12，自动化 319 → ~331）

| # | 用例 | 对应验收 |
|---|---|---|
| T27-1 | 全可见控件：命令流与剔除前**逐位一致**（回归锚） | A5 |
| T27-2 | 全视口外子树：`PushClip`/`Draw*`/`PopClip` **整段 = 0**（构造侧 CommandBuffer 直接断言） | A1 |
| T27-3 | 边界接触（`child.right == clip.left`，含 y 向）：剔除 | C27-3 / Q5 |
| T27-4 | 部分相交：整段照常构建（部分可见 ≠ 全跳） | A6 |
| T27-5 | 嵌套：父部分相交保留、视口外孙整段剔除、相交孙保留（★ 评审 v1.1：详设须画**精确测试树**——Parent 部分进入 viewport，Child A 完全在外 ⇒ 其 Grandchild A **即使自身位置奇怪也一起跳过**、Child B 相交 ⇒ Grandchild B 保留；验证的是 **subtree pruning 而非单 Widget pruning**） | C27-1 |
| T27-6 | `ScrollView` 行滚出视口：该行整段 0；可见行不变（ContentOffset 路径） | Q1/Q2 |
| T27-7 | 滚出多屏再滚回：首帧渲染正确（含 TextBox 滚动条惰性同步补执行） | A3 |
| T27-8 | C-VIS 正交：隐藏子停泊负区照旧剔除（行为与 Phase 25 逐位同）；可见视口外子被新判据剔除 | D27-4 |
| T27-9 | 退化矩形（0 宽 / 0 高）：剔除且不进 OnPaint | D27-8 |
| T27-10 | **像素等价**：无头 GL 探针，同一树 culling on/off 两帧 `glReadPixels` 逐字节一致（Q6 确定性契约场景） | A2 |
| T27-11 | `SetCullingEnabled(false)` ⇒ 命令流与既有基线逐位一致（对照缝回归锚） | C27-6 |
| T27-12 | 初始 clip：注入 client 矩形后超界根子树被剔除；不注入（旧构造）= 无界不剔除 | C27-5 |

★ 命令计数（A1）的观测 = 构建侧 `CommandBuffer` 直接检查（`PaintContext` 封装它；既有 ClipTests 先例），**不依赖** `RecordingBackend`。

---

## 8. 影响面

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（无新增 header；★ 评审建议明确区分「头文件数量」与「API 数量」两个口径，v1.1 采纳） |
| 公共 API | **+3**（`Intersects` / `IsRectVisible` / `SetCullingEnabled`——★ 第三项的可见性 = O3 详设必答，可能收敛为 +2）+ 1 构造重载 |
| 用例 | **319 → ~331**（+12；需求上修 ~+10 的兑现） |
| CMake | **0 改动**（无新文件；`Intersects` header-only） |
| 改动文件 | `Rect.h` · `PaintContext.h/.cpp` · `Widget.cpp` · `Window.cpp`（+ 测试） |
| 风险 | **低～中（可控性高）**（★ 评审 v1.1 修正：算法危险已由 D3 盘点 + 安全性证明消除，但本条属于**改变 `Widget::Paint` traversal 语义的基础设施修改**——触及所有 Widget / ScrollView / 嵌套 clip / 存量 Paint 测试 / 两后端裁剪语义 / OnPaint 副作用，风险点已拆细故可控） |

---

## 9. 批次

| 批 | 内容 | 依赖 |
|---|---|---|
| 批一 | `Rect.h::Intersects` + `PaintContext` 交集栈（构造重载 / `IsRectVisible` / `SetCullingEnabled`）+ T27-2/3/4/11/12（PaintContext 级，不接 Widget） | 无 |
| 批二 | `Widget::Paint` 接入（D27-4）+ `Window::PaintFrame` 初始 clip + 存量命令流测试对账 + T27-1/5/6/7/8/9 | 批一 |
| 批三 | A2 像素等价（无头 GL 探针）+ T27-10 | 批二 |
| 批四 | A4 性能基准（大文本构建耗时对比，读数回填）+ 文档收口 | 批三 |

---

## 10. 开放项

| # | 项 | 说明 |
|---|---|---|
| O1 | A4 基准参数表 | 沿 D26-5 先例（固定参数 / 架构级口径），详设冻结 |
| O2 | 存量测试对账清单 | 批二实施时盘点全部断言命令流的用例（ClipTests / TextBoxTests / ScrollViewTests…），详设列对账表 |
| O3 | ★★ **`SetCullingEnabled` 的 API 边界（评审 v1.1 升级：原「命名问题」→ 详设必答题）**——「`IsRectVisible` 属正常 PaintContext 能力，`SetCullingEnabled` 更像测试/debug seam，两者性质不同」。方案 A = 维持公共 API（简单 / A2 方便 / 有逃生门；代价 = 背兼容性契约、用户可随意关）；方案 B = 测试 / 内部缝（构造参数或测试专用路径，生产 API 恒开；实现稍脏）。★ 评审不否掉现方案，详设比较后定 |
| O4 | **未来架构候选**：OnPaint 纯绘制化（TextBox `SyncScrollBar` / CaptionBar `SetGlyph` 迁往 Update 相位） | 本 Phase 不做；随 D3 盘点产出登记 deferred（编号待用户拍板） |

---

## 11. 修订记录

- **v1.1**（2026-10-04）**外部评审吸收（✅ PASS，可进入详设）**。① ★ **结论**：13 项分项全部通过（OnPaint 副作用盘点 = 本稿最大进展），**无任何需要推翻架构的回退点**；评审特别认可：Q1/Q2 的「PaintContext 不变成 ScrollView 上下文」、`self` 同源构造（「先拿同一个 `self` 判断，再 Push 同一个 `self`」）、严格 `>0` 避免 ±1 危险补丁、Q3 的「有 initial clip = 有限绘制世界 / 无 = 无限绘制世界」抽象、T27-12 对无头测试语义的保护、「没有把事情做大」的范围克制。② ★★ **新增 §1.1 详设盯防五件事**（评审给定）：clip stack 数据结构 + initial clip 生命周期 · `self` 同源冻结 · **`SetCullingEnabled` API 边界（评审最想踩刹车的点）** · T27-5/6/7 精确测试树 · 存量命令流测试对账清单。③ **O3 升级**：命名问题 → **API 边界必答题**（方案 A 公共 API vs 方案 B 测试/内部缝；「测试能关」≠「用户能关」）。④ **影响面口径拆分**（评审建议）：公共头文件数量 94 → 94 与公共 API +3 分列（+3 中的 `SetCullingEnabled` 随 O3 可能收敛为 +2）。⑤ **风险修正**：低 → **低～中（可控性高）**——属「改变 Paint traversal 语义的基础设施修改」，触及面广但已拆细。⑥ T27-5 补测试树要求（subtree pruning ≠ 单 Widget pruning）。
- **v1.0**（2026-10-04）初稿。**输入**：需求稿 v1.1（评审通过 + §4.1 必答六问）· 本次实测勘察（B1–B10 带行号 · **OnPaint 全库盘点 12 处 → 副作用仅 2 处且均为幂等同步型、全库无 `Invalidate` 类 Paint 循环 ⇒ D3 放行 / D4 恒开冻结** · 执行端两后端 lround 半开语义实测 ⇒ 剔除安全性证明成立）。**内容**：§1 六问逐答（坐标系单一性 / ContentOffset 零新代码 / 初始 clip 构造注入 / 盘点表 / float 同源 + 严格 >0 + 安全性证明 / A2 确定性契约）· 决策 D27-1..D27-8 · 接口草案（公共 API +3 · 头 94 → 94）· 契约 C27-1..C27-7 · 盯防 7 条 · 用例 T27-1..T27-12（319 → ~331）· 四批 · 开放项 O1–O4。
