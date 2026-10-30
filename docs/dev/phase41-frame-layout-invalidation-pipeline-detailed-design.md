# Phase 41 · 详细设计 —— 帧级布局 / 重绘失效结算管线（#79）

> 状态：**v1.3**（2026-10-30 **三批全部施工完成并通过四构建链**——批 0 = 547、批 1 = 554、批 2 = **556 / 556 × 4**（用户侧 MSVC / Clang / ClangCL / MinGW 实测）；v1.2 = 批 0/1 回填；v1.1 = 收 Gemini 4 Argon 详设审核 2 P1 + 3 P2 全部修入；同日 v1.0 落盘。实施回填见 §8）。上游三件：需求 **v1.2** · 初设 **v1.3** · 账本订正 **roadmap v2.65** + `phase25-list-container-preliminary-design.md:51` B2。
> ★ 本稿是**可直接抄的施工图**：每个函数的完整新文本、每个插入点的行号锚、每条用例的装置与期望值、每条机检的可执行命令（含 v1.1 订正后的正确形态）。
> ★ 基线：HEAD `a375824` · 用例 **546 / 546**（2026-10-29 四链实测）· 公共头 **100** · docs **206**（on-disk）· 本机 1920×1080 @ DPI 120。
> ★ 本 Phase 是**契约推翻项**：三处既有契约文字被改写 + **恰 1 条存量用例翻转**（`Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`）。

**红线（继承需求 / 初设，任一破了本 Phase 即失败）**

1. **布局结果逐位不变**：`Arrange` 的几何输出与改前完全相同。本 Phase 只换驱动者，不换算法。
2. **无窗口路径零副作用 + 存量测试改动面受控**：**545 条零改动** + **恰 1 条显式翻转**。
3. **不引入每帧无界重算**：结算**脏触发**，空闲帧 O(1)。

---

## §0 改动清单总表（施工索引）

| # | 文件 | 性质 | 规模（预估） | 批次 |
|---|---|---|---|---|
| 1 | `ECDI/include/ECDI/Widget/Widget.h` | 类头总纲 + 5 处注释改写 + 5 声明 + 1 公开 API + 2 成员 + `SetPosition` 移出 | +95 / −12 | 批 1 |
| 2 | `ECDI/src/Widget/Widget.cpp` | 新增 4 函数 + 改写 6 函数 + 2 处插入 | +150 / −20 | 批 1 |
| 3 | `ECDI/src/Window/Window.cpp` | `PaintFrame` 插 1 行 + `OnResized` 改 4 行 | +8 / −3 | 批 1 |
| 4 | `ECDI/include/ECDI/Application/Application.h` | 删 `OnWindowResized` 声明 | −2 | 批 1 |
| 5 | `ECDI/src/Application/Application.cpp` | 删 `OnWindowResized` 实现 | −9 | 批 1 |
| 6 | `ECDI/src/Widget/ScrollView.cpp` | `:337` 注释改写（**零逻辑**） | +9 / −2 | 批 1 |
| 7 | `ECDI/include/ECDI/Widget/ScrollView.h` | `UpdateContentExtent` 补 extent 边界注 | +7 | 批 1 |
| 8 | `ECDI/src/Widget/TextWidget.cpp` | 两个 `SetText` 各补 1 行 `Invalidate()` | +6 | 批 2 |
| 8b | `ECDI/include/ECDI/Widget/TextWidget.h` | `:34` 注释改写（override 已删） | +3 / −1 | 批 2 |
| 9 | `ECDI/include/ECDI/Widget/TextBox.h` | 删 `SetText` 两 override 声明 + 注释块；`:93-95` 注释订正 | −9 / +3 | 批 2 |
| 10 | `ECDI/src/Widget/TextBox.cpp` | 删两个 `SetText` 实现 + `SetSize` 末尾 `Invalidate` | −26 / +6 | 批 2 |
| 11 | `ECDI/src/Widget/CollapsiblePanel.cpp` | 雷区预警注释（**零逻辑**） | +4 | 批 2 |
| 12 | `ECDI/src/Window/CaptionBar.cpp` | 「本 override 不 Invalidate」注（**零逻辑**） | +3 | 批 2 |
| 13 | `ECDI/src/Tests/WindowProbe.h` | **新增**（`M9Probe` 搬家 + `PumpOneFrame`） | +80 → **实 +111** | 批 0 ✅ |
| 14 | `ECDI/src/Tests/VisibilityRepaintTests.cpp` | 装置换名 + 1 条用例翻转 | +30 / −55 → **实 −84 / +5**（批 0 部分：删装置 71 行 + 加 2 行 + 删 3 行 include + 5 处换名） | 批 0 ✅ + 批 1 |
| 15 | `ECDI/src/Tests/LayoutInvalidationTests.cpp` | **新增**（`CountingLayout` + T41-1..10） | 批 0 **实 +133**（全量预估 +420） | 批 0 ✅ / 1 / 2 |
| 16 | `ECDI/src/Tests/RunAllTests.h` / `.cpp` | 声明 + 注册（各 1 行） | +2 | 批 0 |

★ **CMake 零改动**：`ECDI/src/Tests/*.cpp` 由 `GLOB_RECURSE` 自动入库；`WindowProbe.h` 是头，不入库。
★ **公共头 100 → 100**（第 1/4/7/9 项全是既有头）· **public API +1**（`NeedsLayout`）· **`sizeof(Widget)` +8**。

---

## §1 逐文件施工（完整代码，可直接抄）

### 1.1 `Widget.h`

#### 1.1.1 类头总纲（插在 `:41`「`/// - Event:   事件响应虚函数（...）`」之后、`class Widget{` 之前）

```cpp
///
/// ## 布局 / 重绘失效（★ Phase 41 · #79——两条通道，判据不同，**不得并列描述**）
///
/// ▸ **重绘失效**：几何 / 可见性 / 样式 / 文本的**值真的变了**（同值守卫通过）⇒ `Invalidate()`。
/// ▸ **布局失效**：值真的变了 **且** 某个 `Layout` 会读它 ⇒ 置**根**布局脏，**帧首结算**重排。
///
/// ▸ **谁读什么**（这张表就是「布局失效」的门控依据）：
///   - 我的**尺寸** → 我的 `Layout`（我的子要重排）、父的 `Layout`（我在父的槽位里）
///   - 我的**位置 / 可见性 / stretch** → **仅**父的 `Layout`
///   - 我的**子集合** → **仅**我的 `Layout`
///   ⇒ 没有任何 `Layout` 读的写入，只触发重绘、不触发重排
///     （例：`TextBox` 的滚动条可见性——它的父 `TextBox` 没有 `Layout`）。
///
/// ▸ **谁结算**：`Window::PaintFrame` 帧首（`SettleLayoutIfDirty`）——**业务侧不再需要手写
///   「SetText → AutoSize → Arrange → Invalidate」调用链**。`Arrange()` 仍公开，显式重排仍是
///   权威入口（`ScrollView` 的内容区、动画收尾等场景照旧可用）。
///
/// ▸ **结算期抑制**：`Arrange` 期间 `Layout` 对子控件批量写几何——那些写入是**布局的输出**、
///   不是布局的输入，且**同一帧**就会被画出来 ⇒ 期间**既不置布局脏、也不 `Invalidate()`**
///   （否则一次 `Arrange` 会发出 N 次无意义的重绘请求，并把自己永久弄脏）。
///   ⚠️ 抑制范围 = 「**经统一出口 `NotifyGeometryChanged` 的失效**」，不是「结算期全库零
///   `Invalidate`」——`ScrollBar::SetRange`（`ScrollBar.cpp:25-32`）仍带裸 `Invalidate()`，
///   它不碰 `m_layoutDirty` ⇒ 不影响结算计数（详设 §2 I41-6）。
///
/// ▸ ⚠️ **`SetText` 只走重绘通道**：文本不改变几何。要「文本变长、控件跟着变宽」请显式
///   `AutoSize()`——它是尺寸的唯一动作入口。
/// ▸ ⚠️ **`ScrollView::UpdateContentExtent` 仍是显式的**：extent 是**内容语义**（不是几何失效），
///   框架不代挂钩（D6）。
/// ▸ ⚠️ **`CollapsiblePanel` 在 #80 修好「展开基准 vs `fillCrossAxis`」冲突前，不得放入
///   `fillCrossAxis = true` 的 Layout 容器**（收起态 `SetSize` 会把基准高抹成 0，`VerticalLayout
///   .cpp:96` 的 `SetSize(cross, GetHeight())` 即触发路径——当前无实例）。
```

#### 1.1.2 公开面：新增 1 个查询（插在 `:294` 的 `void Invalidate();` 之后）

```cpp
	/// @brief **整棵所属树**是否有待结算的布局失效（`Window::PaintFrame` 帧首会消费它）
	/// @details ★ 语义是**整树**、不是「本节点」：脏标志寄存在**根**（D41-2 ⓐ），本方法沿父链
	///          上溯到根再读——与 `HasFocus()`（`Widget.cpp:595-607`）同型。
	///          ⇒ 非根节点返回 `true` 意味着「**同树别的分支**（或本分支）有失效待结算」。
	/// @note 主要消费者 = 测试与诊断；框架自身走私有的 `SettleLayoutIfDirty`。
	[[nodiscard]] bool NeedsLayout() const noexcept;
```

★ **不加 `InvalidateLayout()`**（需求 §6 曾预估 +1 或 +2）：它**没有消费者**——布局失效的全部输入
（尺寸 / 位置 / 可见性 / stretch / 子集合 / 换布局器）都已由 setter 覆盖，绕开 setter 改输入在框架里
**不存在路径**；显式重排仍可用公开的 `Arrange()`。按「第二个真实消费者出现才加」的既有纪律，**不加**。

#### 1.1.3 五处注释改写（逐字）

**① `:119` `SetPosition`（声明 + 移出 inline）**

```cpp
	/// @brief 设置相对于父 Widget 的位置
	/// @details ★ Phase 41：**同值守卫 + 双通道失效**（实现由头内联**移入 `Widget.cpp`**——原为纯赋值）。
	///          门控（D41-4）：位置的**唯一**读者是父的 `Layout` ⇒ 仅 `ParentHasLayout()` 才置布局脏；
	///          重绘失效无条件（值真变化时）——几何变了必须上屏，不押在「平台层碰巧每帧重绘」上。
	void SetPosition(int x, int y);
```

**② `:121-124` `SetSize`（追加 Phase 41 段，不动既有三行）**

```cpp
	/// @brief 设置宽高
	/// @details 虚函数（9.5 R1：TextBox override 挂 Resize 后 Caret 可见——Layout 只 SetPosition 不碰尺寸，
	/// SetSize 是尺寸唯一入口）。签名不变、行为兼容；基类实现 = 直接写 m_geometry（移 Widget.cpp）
	/// @details ★★ Phase 41：基类实现改为「**同值守卫 + 双通道失效**」——
	///          ① 值真的变了才继续（否则零副作用：`Layout` 每帧对每个子调它，值多半相同）；
	///          ② 重绘通道恒发 `Invalidate()`；③ 布局通道门控 = `m_layout != nullptr ||
	///          ParentHasLayout()`（尺寸有两个读者：我的 Layout 摆子、父的 Layout 定我槽位）。
	///          ⚠️ **override 不得重复 `Invalidate()`**——基类已发，重复会逃过结算期抑制
	///          （详设 §1.7 的三处收敛即此纪律）。
```

**③ `:133` `SetStretch`（追加）**

```cpp
	/// @details ★ Phase 41：同值守卫 + 门控 = `ParentHasLayout()`（stretch 的唯一读者是父 Layout）。
```

**④ `:145-150` `AutoSize`（逐字改写——推翻「布局/重绘由调用方负责」）**

```cpp
	/// @brief 按 GetPreferredSize() 调整自身尺寸（9.8 显式命令——后调用者赢，需求 R5 v1.5）
	/// @return true = 实际调用了 SetSize；false = stretch 互斥 no-op 或尺寸未变化
	/// @note §3.7 纯 geometry operation：内部**只调 SetSize**（虚分派——TextBox override 既有语义）；
	///       float → int 向零截断（v1 不引入 rounding policy）
	/// @note ★★ **Phase 41 契约变更**（旧契约「由调用方自行排布与刷新」已推翻——★ 原句**故意不复述**：
	///       I41-9 的判据对象就是注释，把靶词原样拼进新注释会让机检自己命中自己；原文全句见
	///       需求稿 §1.1.2 的引文块，`docs/` 不受该机检约束）：
	///       本方法**自身**仍不 Arrange、不 `Invalidate`——但 `SetSize` 现在是**失效源**
	///       （重绘 + 布局双通道，见 `SetSize` 与类头「布局 / 重绘失效」）。
	///       ⇒ 尺寸真变化后，**下一帧首**自动完成重排与重绘，调用方**无需**再补
	///       `Arrange()` / `Invalidate()`；尺寸未变化时两条通道都不动（同值守卫）。
	///       ★ 仍**不挂钩 `SetText`**：文本变长不会自动触发 AutoSize——那是**内容语义**，
	///       不是几何失效（框架不替调用方决定「文本该不该撑开控件」）。
	bool AutoSize();
```

**⑤ `:201-203` `SetVisible` 注释 ③（逐字改写——撤销旧豁免）**

```cpp
	///   ③ ★★ **Phase 41 契约变更——本条旧豁免已撤销**：原文写「不扩大到 `SetSize` /
	///      `SetPosition` / `SetText`——几何 setter 由 `Layout` 逐帧批量调用（加了会每帧重绘）」。
	///      ⇒ 该理由的**事实基础已不成立**：`Win32PlatformWindow` 的 `WM_PAINT` **不调
	///      `BeginPaint`/`EndPaint`**（决策 39）⇒ update region **从不 validate** ⇒ **每帧本来就在
	///      全量重绘**，`Invalidate` 的**边际**代价 ≈ 0。真正的代价项是「每帧 O(N) 布局」，
	///      而它由「布局失效」的**门控 + 帧首结算**挡掉（见类头「布局 / 重绘失效」）。
	///      ⇒ 现行为**两条通道、两套判据**：
	///        · **重绘通道** = 值真的变了 ⇒ `Invalidate()`（本条即该通道的先例，M9 只先落了这一格）；
	///        · **布局通道** = 值真的变了 **且** 某个 `Layout` 会读它 ⇒ 置根布局脏，帧首结算。
	///      ⇒ 可见性**两条都走**：它既是绘制门控（`Widget.cpp:489` 的 Paint 前置 / `:314` 的 HitTest 门控），
	///      也是**布局输入**（`Layout` 对不可见子跳过槽位并停泊到 bbox 负区——C-VIS-2/4）。
```

★ `SetVisible` 实现里 `:444` 的 `Invalidate();` 换成 `NotifyGeometryChanged(ParentHasLayout());`（§1.2.5）。

#### 1.1.4 私有面（插在 `:387` 的 `ContainsRect` 声明之后）

```cpp
	// ── 布局失效结算（★ Phase 41 · #79）─────────────────────────────────
	//    存放形态 = **根单标志**（D41-2 ⓐ）：置位时沿父链上溯写根，消费只在根读，
	//    与 `Invalidate()` 的「上溯取 Window」同构。⇒ **非根节点上的 `m_layoutDirty`
	//    只在「离线子树挂入」那一次被读**（`AddChild` 合并），其余时刻无意义。

	/// @brief 沿父链上溯到根（无父即自身）
	[[nodiscard]] Widget* GetRootNode() noexcept;

	/// @brief 父节点是否有 `Layout`（位置 / 可见性 / stretch 的唯一读者）——门控表达式的**单一来源**
	/// @details 内联在头里而非移到 `.cpp`：它是零成本的三目（一次指针比较），被 4 个 setter 复用；
	///          若移出会变成 4 次跨 TU 调用，而它没有任何副作用需要藏进实现文件。
	[[nodiscard]] bool ParentHasLayout() const noexcept{
		return m_parent != nullptr && m_parent->m_layout != nullptr;
	}

	/// @brief 几何类 setter 的**唯一**失效出口（三条语义见 §1.2.2 实现处注释）
	/// @param layoutInputChanged 本次写入是否是「某个 `Layout` 会读的输入」——门控由**调用方**算好传入
	///        （出口不知道「这次改的是哪个字段」，故不能自己判）
	/// @note ★ **无条件调用**，门控只作实参：重绘通道对几何类 setter 恒发，绝不因门控为假而跳过。
	void NotifyGeometryChanged(bool layoutInputChanged) noexcept;

	/// @brief 帧首布局结算（**仅根有意义**；由 `Window::PaintFrame` 经 `friend class Window` 调用）
	void SettleLayoutIfDirty();
```

**成员（插在 `:401` 的 `std::unique_ptr<Layout> m_layout;` 之后）**

```cpp
	bool m_layoutDirty = false;	///< ★ Phase 41：根的待结算布局失效（D41-2 ⓐ；**非根只在挂入瞬间被读**）

	int m_arrangeDepth = 0;	///< ★ Phase 41：结算期嵌套深度（D41-3 ⓑ 抑制；用计数而非 bool——同 `m_dispatchRefCount`）
```

★ `sizeof(Widget)` **+8**（bool 与 int 各占 4 字节对齐后）。`Widget` 已禁拷贝/禁移动（地址稳定是硬不变量）
⇒ 无序列化、无跨进程契约 ⇒ **零风险**。

#### 1.1.5 `:329` `Arrange` 补注释（此前**完全无注释**）

```cpp
	/// @brief 立即重排本子树（`m_layout->Arrange` 自上而下 + 递归子节点）
	/// @details ★ Phase 41：**帧首结算**（`Window::PaintFrame` → `SettleLayoutIfDirty`）已接管
	///          「几何变了要重排」——业务侧通常**不需要**再调本方法。
	///          ⚠️ **清脏作用域**（D41-3.2）：在**根**上调用 ⇒ 完成即清根布局脏（`PaintFrame` 随后不再重排）；
	///          在**子树**上调用 ⇒ **不清**（兄弟分支可能仍脏）。
	///          ⚠️ 调用期间对**整棵树**抑制「置脏 + 经出口的 `Invalidate`」（`Layout` 写几何是布局的输出，
	///          不构成新的布局失效，且同帧即上屏）。
	void Arrange();
```

### 1.2 `Widget.cpp`

#### 1.2.1 新增四个函数（插在 `Invalidate()`（`:585-593`）之后，同一节内）

```cpp
// ── ★ Phase 41：布局失效结算（两通道失效 + 帧首结算）──────────────

Widget* Widget::GetRootNode() noexcept{

	Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root;

}

void Widget::NotifyGeometryChanged(bool layoutInputChanged) noexcept{

	Widget* const root = GetRootNode();

	// ★★ 结算期抑制（D41-3 ⓑ）：`Layout::Arrange` 对子控件批量写几何，那些写入是**布局的输出**
	//   而不是输入 ⇒ 不构成新的布局失效；且它们会在**同一帧**被画出来 ⇒ 也不需要重绘请求。
	//   ⇒ 两条通道一起抑制（否则一次 Arrange 会发出 N 次 InvalidateRect，并把自己永久弄脏）。
	//   ⚠️ 抑制范围 = 「经本出口的失效」，**不是**「结算期全库零 Invalidate」：
	//     `ScrollBar::SetRange`（`ScrollBar.cpp:25-32`）仍带裸 Invalidate()，它不碰 m_layoutDirty
	//     ⇒ 不影响 Arrange 计数（详设 §2 I41-6 已记账）。
	if (root->m_arrangeDepth > 0){

		return;

	}

	// ① 布局通道（D41-4 门控）：只有「某个 Layout 会读它」才置根脏
	if (layoutInputChanged){

		root->m_layoutDirty = true;

	}

	// ② 重绘通道：复用既有出口（无窗口 ⇒ 上溯取不到 Window ⇒ 自然 no-op，既有语义）
	Invalidate();

}

void Widget::SettleLayoutIfDirty(){

	// ★ 仅根有意义（`Window::PaintFrame` 以 `m_rootWidget` 调用；friend 授权）
	if (!m_layoutDirty){

		return;   // 空闲帧 O(1)——红线③

	}

	Arrange();    // 根调用 ⇒ 内部按 D41-3.2 清脏

}

bool Widget::NeedsLayout() const noexcept{

	// 脏标志寄存在根 ⇒ 查询必须上溯（与 `HasFocus()` 同型）
	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_layoutDirty;

}
```

★ 为什么出口末尾用 `Invalidate()` 而不是内联 `root->m_window->Invalidate()`：**避免同一表达式的第二个
构造点**（Phase 27 D27-B 纪律）。代价是父链多走一遍——与门控那次同阶，可忽略。
★ `Invalidate()` 本体（`:585-593`）**一字不改**。

#### 1.2.2 `SetPosition`（新函数，插在 `:420` 的 `Widget::SetSize` **之前**）

```cpp
void Widget::SetPosition(int x, int y){

	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);

	// ★ 同值守卫（R6 / D41-3 末段）：**正确性要求，不是优化**——
	//   ① `Layout` 每帧对每个子调本方法（值多半相同）⇒ 无守卫会让「伪变化」把根**永久**置脏
	//      （A41-6「空闲帧不重排」直接变红）；
	//   ② 复合控件内部也常写同值（`CaptionBar::RelayoutChildren` 等）。
	//   ★ Phase 41 前本方法是头内联的**纯赋值**（无守卫需求）——移出 inline 的理由见 `Widget.h`。
	if (m_geometry.x == fx && m_geometry.y == fy){

		return;

	}

	m_geometry.x = fx;
	m_geometry.y = fy;

	// 门控（D41-4）：位置的**唯一**读者是父的 Layout
	NotifyGeometryChanged(ParentHasLayout());

}
```

#### 1.2.3 `SetSize`（替换 `:420-425` 原 6 行）

```cpp
void Widget::SetSize(int w, int h){

	const float fw = static_cast<float>(w);
	const float fh = static_cast<float>(h);

	// ★ 同值守卫（R6 / D41-3 末段）：
	//   ① `Layout` 每帧对每个 child 调 SetSize（值多半相同）；
	//   ② 复合控件内部写同值几何（`TextBox::LayoutScrollBar:719`、`ScrollView::ApplyLayout:272-288`）
	//      ——无守卫会无谓置脏 ⇒ 下一帧白跑一次全树 Arrange。
	if (m_geometry.width == fw && m_geometry.height == fh){

		return;

	}

	m_geometry.width = fw;
	m_geometry.height = fh;

	// 门控（D41-4）：尺寸有**两个**读者——我的 Layout（子要重排）、父的 Layout（我在父的槽位里）
	NotifyGeometryChanged(m_layout != nullptr || ParentHasLayout());

}
```

#### 1.2.4 逐 setter 的门控对照表（施工核对用）

| setter | 同值守卫 | 实参（布局通道） | 重绘 |
|---|---|---|---|
| `SetSize` | 是 | `m_layout != nullptr \|\| ParentHasLayout()` | 恒发 |
| `SetPosition` | 是 | `ParentHasLayout()` | 恒发 |
| `SetVisible` | 已有（`:434`） | `ParentHasLayout()` | 恒发 |
| `SetStretch` | 是（钳 0 后比） | `ParentHasLayout()` | 恒发 |
| `AddChild` | — | `subtreeDirty \|\| m_layout != nullptr` | 恒发 |
| `RemoveChild` | — | `m_layout != nullptr` | 恒发 |
| `SetLayout` | — | `true`（无条件） | 恒发 |
| `TextWidget::SetText` | 不加 | **不进出口**（只 `Invalidate()`） | 恒发 |

#### 1.2.5 `SetVisible`（替换 `:440-444` 的尾部 `Invalidate();`）

```cpp
	m_state.visible = v;

	// ★ 请求重绘：可见性是**绘制门控**（`Widget.cpp:489` 的 Paint 前置 / `:314` 的 HitTest 门控）⇒ 状态变化必须让
	//   新状态上屏。此前的「裸赋值」与 `SetStyle`/`ApplyTheme`（内建 Invalidate）不同族
	//   ⇒ 应用侧改可见性常静默不刷新（= 审计 M9 本体）。
	// ★★ Phase 41：同时是**布局输入**（`Layout` 对不可见子跳过槽位并停泊到 bbox 负区——C-VIS-2/4）
	//   ⇒ 门控 = 父是否有 Layout。这是 §1.3.1「SetVisible 之后兄弟不重排」的修复点。
	// ⚠️ 无窗口（无头测试 / 构造期）时 `Invalidate()` 内部上溯取不到 Window ⇒ 自然 no-op。
	NotifyGeometryChanged(ParentHasLayout());
```

★ `:429-436` 的同值守卫块与 `:438` 的赋值**一字不改**；**`:440-444` 的原 4 行注释 + `Invalidate();` 整段
由上方代码替换**（注释必须改写是两个原因叠加：① 旧豁免被撤销；② 原文引用的行号早已失效，改 `:489`/`:314`）。
★ v1.1 订正（P2-2）：v1.0 的施工说明写「四行注释**保留**」，与它自己的代码块矛盾——代码块里那四行已被
重写。**以代码块为准**，说明行已改。

#### 1.2.6 `SetStretch`（替换 `:448-462` 的尾部）

```cpp
void Widget::SetStretch(int stretch) noexcept{

	// ★★ 审计 #63 / §7.3：补 Release 侧钳制（原实现只有断言 ⇒ Release 下负值直落 `m_stretch`）。
	//   ...（既有 12 行注释全部保留，见 `:450-457`）...
	FRAMEWORK_ASSERT(stretch >= 0);   // 负值无合理权重语义（详设 §2.1）

	const int clamped = (stretch > 0) ? stretch : 0;

	// ★ Phase 41：同值守卫（stretch 由父 Layout 读，逐帧写同值是常态）
	if (m_stretch == clamped){

		return;

	}

	m_stretch = clamped;

	// 门控（D41-4）：stretch 的唯一读者是父的 Layout
	NotifyGeometryChanged(ParentHasLayout());

}
```

#### 1.2.7 `AddChild` 尾部（替换 `:98-100`）

```cpp
	// ★★ Phase 41（D41-6）：**子树脏向新根合并**。
	//   业务侧的真实建树路径是「先离线搭子树、再挂进窗口」——离线期 `AddChild`/`SetLayout`
	//   把脏写在该子树的**临时根**上；根单标志只看真根 ⇒ 不合并就是**挂入后首帧不排**
	//   （几何停在初始值）。★ 无法靠 `SetWindow` 兜底：它是 private、唯一调用点是
	//   `Window` 构造 `:113`（对刚 new 出来的空 root），不是业务路径。
	Widget* const raw = child.get();

	const bool subtreeDirty = raw->m_layoutDirty;

	raw->m_layoutDirty = false;   // 已由真根承载；临时根不再被查询（防「二次挂入」伪脏）

	raw->m_parent = this;

	m_children.emplace_back(std::move(child));

	// ★★ 失效出口**无条件调用**，门控只作**实参**（D41-6 / v1.1 P0-1 订正）：
	//   ① 子树脏：无条件合并（它代表一条**真实**失效，不需再过门控）；
	//   ② 自身置脏：子集合的唯一读者是**我的** Layout。
	//   ⚠️ 绝不可写成 `if (subtreeDirty || m_layout){ NotifyGeometryChanged(true); }`——
	//   那会让「往**无 Layout** 的父节点加控件」连**重绘通道**都被跳过（新控件要等下一次
	//   偶然重绘才出现）。★ 这是**行为新增**：HEAD 的 AddChild 不发 Invalidate（R41-11）。
	NotifyGeometryChanged(subtreeDirty || m_layout != nullptr);
```

★ **顺序不可换**：`raw->m_layoutDirty` 必须在 `emplace_back(std::move(child))` 之前取（move 后 `child`
为空），`raw->m_parent` 也必须在之前写（既有顺序）。

#### 1.2.8 `RemoveChild`（插在 `:144` 的 `m_children.erase(it);` 之后）

```cpp
	// ★ Phase 41：子集合变化 ⇒ 走失效出口（门控只说「**我的** Layout 会读子集合」，重绘无条件是发）。
	//   ⚠️ 同样**不得**套 `if`——从**无 Layout** 的容器摘子也必须重绘。
	//   ★ 插入点等价性：出口里的 `Invalidate()` 是从 **`this`** 上溯取 Window，与被摘节点的
	//   `m_parent`（`:165` 才清）无关 ⇒ 早一处/晚一处结果相同；放这里只是让「子集合刚变」
	//   这个因果在代码里读得出来。此时 `result` 仍持有所有权、`this` 的父链完好。
	NotifyGeometryChanged(m_layout != nullptr);
```

#### 1.2.9 `SetLayout`（替换 `:362-366`）

```cpp
void Widget::SetLayout(std::unique_ptr<Layout> layout){

	m_layout = std::move(layout);

	// ★ Phase 41：换布局器 ⇒ **我的子树**必须重算（无条件置脏——连「移除布局」也置，
	//   因为「此前由 Layout 摆出的几何是否仍然合法」无法在 O(1) 内判定）。
	//   ⚠️ 若将来出现「在 Layout::Arrange 内部换布局器」的调用（当前不存在），该写入会被
	//   结算期抑制吞掉 ⇒ 要等下一条失效才结算。
	NotifyGeometryChanged(true);

}
```

#### 1.2.10 `Arrange`（替换 `:414-418`）

```cpp
void Widget::Arrange(){

	Widget* const root = GetRootNode();

	// ★ D41-3 ⓑ：结算期抑制对**整棵树**生效（标志寄存在根）
	++root->m_arrangeDepth;

	ArrangeInternal();

	--root->m_arrangeDepth;

	// ★ D41-3.2：**根** Arrange 完成即清根脏；**子树** Arrange **不清**（兄弟分支可能仍脏）
	//   ⇒ 这是 A41-8「一次 WM_SIZE 只 Arrange 一次」的唯一成立条件：
	//     `Window::OnResized` 先同步 Arrange（此处清脏）⇒ 随后 `PaintFrame` 看到干净 ⇒ 不再重排。
	//   ⚠️ 第二个条件（**深度已归零**）是**防御性**：嵌套时 `root == this` 本来就为假，
	//   写出来是让「结算完全退出后才消费脏」这条语义在代码里可见。
	//   ★ 本注释刻意不写成员名——I41-6 的机检才只数到 4 处**代码**（口径见 §2）。
	if (root == this && root->m_arrangeDepth == 0){

		root->m_layoutDirty = false;

	}

}
```

### 1.3 `Window.cpp`

**① `PaintFrame`（插在 `:185` 的构造期防御早退之后、`:188` 的 `m_commands.clear()` 之前）**

```cpp
	// ★★ Phase 41（R3）：**帧首布局结算**——有界（脏才排），空闲帧 O(1)。
	//   位置必须在 `PaintContext` 构造与 `m_rootWidget->Paint()` **之前**：
	//   本帧要画的几何 = 结算后的几何。
	m_rootWidget->SettleLayoutIfDirty();

```

**② `OnResized`（`Window.cpp:830-841` 全函数替换）**

```cpp
void Window::OnResized(int width, int height){

	// 窗口大小变化时，同步 RootWidget 尺寸 + 重排布局链（9.7 触发链）
	if (m_rootWidget){

		// ★ Phase 41：`SetSize` 现在自带「置布局脏 + Invalidate」两条通道 ⇒ 此处**不再**显式
		//   `Invalidate()`（原第三行）。同值 resize 被基类守卫吃掉，不产生任何失效。
		m_rootWidget->SetSize(width, height);

		// ★ 同步重排（**保留**）：`Win32PlatformWindow.cpp:940-941` 承诺「resize 事件到达时
		//   RootWidget 已是新尺寸、布局已更新」——消费者在事件回调里读到的是新几何。
		//   本调用在根上 ⇒ 完成即清根脏 ⇒ 随后的 `PaintFrame` **不再重排**（A41-8 的「恰 1 次」）。
		//   ⚠️ 历史：此前 `Application::OnWindowResized` 还会再 Arrange 一次（同帧两次，幂等
		//   所以无害）——Phase 41 已删，本行是**唯一**的 resize 布局驱动点。
		m_rootWidget->Arrange();   // 9.7：渐进语义——有 Layout 的节点重排 / 无 Layout 的保持手摆

	}

}
```

★ 原 `:836` 的**行尾注释原文保留**在 `Arrange()` 那一行（渐进语义未变，它讲的是 `ArrangeInternal`
的行为）——v1.1 订正（P2-2）：v1.0 的代码块把它漏掉了，与下方说明矛盾。`Invalidate()` 那一行
（原 `:837` 及它的注释）**整行删除**。

### 1.4 `Application.h` / `Application.cpp`（整条删除）

| 文件 | 删除内容 |
|---|---|
| `Application.h:118-119` | `void OnWindowResized(const WindowResizedEvent& event) override;` + 其后的空行 |
| `Application.cpp:287-293` | 整个 `Application::OnWindowResized` 函数体 + 其后的空行 |

★ **不保留空 override**：空实现是死代码；「为何不再在这里重排」的说明已写进 `Window::OnResized`（§1.3②）。
★ `WindowResizedEvent` 仍**照常派发给 Widget 树**（冒泡不变），业务订阅不受影响——删的只是
`Application` 自己的一个默认处理。

### 1.5 `ScrollView.cpp` / `ScrollView.h`

**① `ScrollView.cpp:337` 注释替换（零逻辑）**

```cpp
	// ★★ Phase 41 契约变更（旧契约「`SetSize` 不发重绘、刷新由调用方负责」已推翻——★ 原句**故意不复述**
	//   靶词，见 §2 I41-9：判据对象就是注释，拼进去等于让机检自己命中自己；原文全句见需求稿 §1.1.3）：
	//   重绘失效由 `Widget::SetSize` 统一发出（值真变化才发，同值守卫在基类），本 override
	//   只负责三段**复合**副作用——`ApplyLayout` / `ClampOffset` / `SyncBars` 改的是
	//   **滚动条与内容节点**的几何/可见性，不是 `ScrollView` 自身的视口几何。
	//   ⚠️ **旧注释当时为什么看起来是对的**：`SetSize` 确实由 `Layout` 逐帧调用；但
	//   ① 「逐帧重绘」的代价在当前平台层**已经付过了**（无 `BeginPaint` ⇒ region 恒脏）；
	//   ② 结算期（`Layout` 正在写几何）的豁免**不再由本类各自判断**，而由
	//      `Widget::NotifyGeometryChanged` 在 `Arrange` 期间**统一拦下**（既不置脏也不 Invalidate）；
	//   ⇒ 本类没有任何理由再单独拒发一次重绘请求。
	//   ★ 如实记账：本函数内的 `SyncBars` → `ScrollBar::SetRange`（`ScrollBar.cpp:25-32`）仍带
	//   **裸 Invalidate()**（不走统一出口）⇒ 结算期仍会发一次重绘请求，但它不碰 `m_layoutDirty`
	//   ⇒ 不影响 Arrange 计数（详设 §2 I41-6）。
```

**② `ScrollView.h:66-71` `UpdateContentExtent` 追加一条 `@note`**

```cpp
	/// @note ★ Phase 41：几何**自动**（`SetSize`/`SetPosition` ⇒ 帧首结算）、**extent 手动**
	///       （本方法）——两者不是同一件事：extent 是**内容语义**（我的内容有多大），几何是**结果**
	///       （我在父容器里占多少）。`ListLayout`/`GridLayout` 会摆子，但**不会**替你累加 extent
	///       ⇒ 增删子之后仍须显式更新，否则滚动范围失真（`ModelProbe` 的搜索过滤即此场景，
	///       `ModelProbe.cpp:326-333`）。
```

### 1.6 `TextWidget.cpp`（批 2）

两个 `SetText` 重载（`:37-43` / `:45-51`）末尾各补：

```cpp
	// ★ Phase 41（D41-5）：**只走重绘通道**——文本不改变几何，故**不置布局脏**。
	//   要让控件跟着文本变长，请显式 `AutoSize()`（它是尺寸的唯一动作入口）。
	//   ★ 本行同时兑现审计「条 65」：`SetText` 曾只 bump revision 而不请求重绘
	//   （非鼠标事件驱动的 UI 更新静默不刷新，与 M9 同族）。
	Invalidate();
```

★ **不加同值守卫**：既有语义是「同文本也 bump `m_textRevision`」（`MarkTextChanged` 无守卫），
加守卫会改变 `preferred` 指纹行为——**不在本 Phase 范围**（需求 D41-5 严格判据只裁决「哪条通道」）。

### 1.7 `TextBox.h` / `TextBox.cpp`（批 2，三处收敛）

**① `TextBox.h:97-104` 整块删除**

```cpp
	// ── SetText override（9.7 修：外部替换文本绕过编辑操作路径，必须置行缓存失效）──
	/// @details TextWidget::SetText 直接改 m_text——多行内容（\n）不失效 `m_lines` 会按旧缓存画成
	/// 单行。override 只做缓存失效 + Invalidate，**不触发 RaiseTextChanged/callback**（D7 契约保持）。
	/// ★ Phase 38：失效方式 = 清 `m_linesValid`（`MarkLinesDirty()`），与旧失效布尔位退役后的
	/// 键守卫同源——行表只在**键未命中**时重建。
	void SetText(const std::string& text) override;

	void SetText(std::string&& text) override;
```

★ **删得干净的依据**：① 基类补了 `Invalidate` 后，`TextBox::SetText` 的三行（`TextWidget::SetText` +
`MarkTextChanged` + `Invalidate`）**全部**成为基类子集；② 本段注释里的 `MarkLinesDirty()` 是**全库唯一
幽灵引用**（`grep -rn "MarkLinesDirty" ECDI/` 只此一处，函数不存在），Phase 38 之后的真相是
「键守卫 + `m_linesValid` 首帧必建」（`TextBox.cpp:454`/`:504`）；③ 它记的**真契约**「`SetText` 不触发
`RaiseTextChanged`（D7）」已在 `TextBox.h:110-116`（`SetOnTextChanged`）**独立记录** ⇒ 删除零损失。

**② `TextBox.cpp:120-143` 整块删除**（两段注释 + 两个函数实现）

**③ `TextBox.cpp:269` 的 `Invalidate();` 删除**

```cpp
void TextBox::SetSize(int w, int h){

	// 9.5 R1 §4.8：基类改几何 → 重新保证光标可见（缩窗后光标越出可视区立即滚回，垂直/水平同时受益）
	Widget::SetSize(w, h);          // ★ Phase 41：基类已发「重绘 + 布局」两条通道
	EnsureCaretVisible();
	// scrollbar-placement-and-appearance △3：条摆位（视口空间，不受偏移）——★ v1.5 起四边**内缩**，见 LayoutScrollBar()
	LayoutScrollBar();
	SyncScrollBar();
	// ★ Phase 41（D41-F）：**删除原先这里的 Invalidate()**——基类已发，重复发会**逃过结算期抑制**
	//   （`Arrange` 期间每个 TextBox 一次无意义 InvalidateRect，违反 I41-3）。

}
```

#### 1.7.4 两处**相邻注释**的连带订正（v1.1 补，P2-3）

§1.7① 删 override、§1.7③ 删 `Invalidate` 之后，**紧邻它们的注释会立刻失真**——必须同批改：

| 文件:行 | 现文（删改后即失真） | 新文 |
|---|---|---|
| `TextBox.h:93-95` | `/// @brief 设置宽高（9.5 R1 override：基类改几何后 EnsureCaretVisible + Invalidate——缩窗后光标越出可视区立即滚回；Layout 只 SetPosition 不碰尺寸，SetSize 是尺寸唯一入口）` | `/// @brief 设置宽高（9.5 R1 override：基类改几何后 EnsureCaretVisible——缩窗后光标越出可视区立即滚回；Layout 只 SetPosition 不碰尺寸，SetSize 是尺寸唯一入口）`<br>`/// @details ★ Phase 41：**`Invalidate` 归基类** `Widget::SetSize`（重绘 + 布局双通道）——本 override 只保留「光标可见 + 条摆位/同步」两件**复合**副作用，**不得**再自行 `Invalidate`（会逃过结算期抑制）` |
| `TextWidget.h:34` | `/// @brief 设置文本（9.7 修：升格 virtual——TextBox override 置行缓存失效；外部替换文本绕过编辑操作路径）` | `/// @brief 设置文本`<br>`/// @details ★ Phase 41：本方法内建 `MarkTextChanged()`（preferred 指纹失效 + `TextBox` 行表键维 1）与 `Invalidate()`（重绘通道）；**仍不置布局脏**（文本不改几何，要撑开请显式 `AutoSize()`）。`<br>`///          `TextBox` 的 override 已删除——它的全部内容都是基类的子集` |

★ **为什么这两行必须同批改**：它们与 §1.7① / §1.7③ 删掉的东西**正向描述同一个行为**。留着就成了
「注释说 `Invalidate`、实现里没有」的漂移——而本 Phase 的全部立论就是**设计链一致性**，注释层不能自带反例。
★ `TextWidget.h:98-99`（`MarkTextChanged` 的「子类直接改 `m_text` 的路径必须同样调用」）**不动**：
它讲的是**编辑路径**，仍然成立且仍然必要（删除 `TextBox::SetText` 不影响它）。

### 1.8 `CollapsiblePanel.cpp` / `CaptionBar.cpp`（批 2，纯注释）

**`CollapsiblePanel.cpp`（插在 `:157` 收起态分支的 `if (!m_expanded){` 之后第一行）**

```cpp
		// ★ Phase 41 雷区预警（**不修**——属 #80）：本分支把传入的 (w,h) 写进 m_expandedRect
		//   作为「展开基准」，而此时呈现几何往往还是 0；`VerticalLayout.cpp:96` 的 fillCrossAxis
		//   分支会调 `child->SetSize(cross, child->GetHeight())` ⇒ 收起态 GetHeight()==0
		//   ⇒ **基准高被抹成 0 ⇒ 此后永远展不开**。当前无实例（examples 均在无 Layout 容器里），
		//   修属 #80（动画驱动的连续几何）。
```

**`CaptionBar.cpp`（插在 `:80` 的 `Widget::SetSize(w, h);` 之后）**

```cpp
	// ★ Phase 41：本 override **不** `Invalidate()`——基类已发两条通道；`RelayoutChildren` 写的是
	//   **无 Layout** 的子（`m_minButton` 等）⇒ 门控把它们拦在布局通道之外，只留重绘（恰是所需）。
```

---

## §2 不变量 I41-1..10（每条机器可校）

| # | 不变量 | 机检命令（Git Bash / 本仓实测口径） | 期望 |
|---|---|---|---|
| **I41-1** | 几何类 setter 的失效动作**只有**一个出口 | `grep -rn "NotifyGeometryChanged" ECDI/src/Widget/Widget.cpp \| grep -v ':[0-9]*:[[:space:]]*//'` | **8** = 1 定义 + **7** 调用（`SetPosition`/`SetSize`/`SetVisible`/`SetStretch`/`AddChild`/`RemoveChild`/`SetLayout`）。★ **必须排除注释**：`AddChild` 的**反例注释**里有 1 处标识符（**故意保留**，§1.2.7），不排除会得 9——那是**口径错**，不是代码错 |
| **I41-2** | 置真**唯一写入点** | `grep -rn "m_layoutDirty = true" ECDI/src` | **1**（出口内） |
| **I41-3** | 清脏**只有两处**，且都在本文件 | `grep -rn "m_layoutDirty = false" ECDI/src` | **2**（`Arrange` 根清 + `AddChild` 合并前清） |
| **I41-4** | 结算驱动点唯一 | `grep -rn "SettleLayoutIfDirty" ECDI/src` | **2**（`Widget.cpp` 定义 + `Window.cpp` 调用） |
| **I41-5** | resize 布局驱动点唯一 | `grep -rn --exclude-dir=Tests -- "->Arrange()\\|\.Arrange()" ECDI/src` | **1**（`Window.cpp:OnResized`）。★ `Application.cpp:292` 已删 ⇒ 注意它原来是 **`.`** 形态、`OnResized` 是 **`->`** 形态，命令必须两者都含 |
| **I41-6** | 结算期抑制**只在出口**判断 | `grep -rn "m_arrangeDepth" ECDI/src/Widget/Widget.cpp \| grep -v ':[0-9]*:[[:space:]]*//'` | **4** = 出口的 `if (root->m_arrangeDepth > 0)` + `Arrange` 的 `++` / `--` / 清脏判断。★ 同样排除注释：`Arrange` 的防御性说明里**刻意不写成员名**（§1.2.10） |
| **I41-7** | 门控表达式**单源** | `grep -rn "m_parent->m_layout" ECDI/` | **1**（`Widget.h` 的 `ParentHasLayout()`）★ 不能限定 `ECDI/src`（内联体在头里） |
| **I41-8** | `TextBox` 不再有 `SetText` override | `grep -rn "void TextBox::SetText" ECDI/src` | **0** |
| **I41-9** | 旧契约文字**清零**（★ 本 Phase 判据对象**就是注释**，故**不得**排除注释行） | `grep -rn "布局/重绘由调用方负责" ECDI/` | **0** |
| **I41-10** | 用例数守恒 | 跑 `ecdi_tests` | **556 passed / 0 failed** |

★★ **两条 grep 口径必须分开（v1.1 订正，P1-1）——「判据对象」决定要不要排除注释**：

| 判据对象 | 命令形态 | 本表适用 |
|---|---|---|
| **代码**（「只有一处」「只在这里判断」） | `grep ... \| grep -v ':[0-9]*:[[:space:]]*//'` | I41-1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 |
| **注释**（「旧契约文字清零」） | `grep ...`（**不得**排除） | I41-9 |

★ 该过滤器**已在本仓实测**：以 `IsVisible()` 为例，`grep -rn "IsVisible()" Widget.cpp | grep -v ':[0-9]*:[[:space:]]*//'`
⇒ 恰 2 条代码命中（`:314` / `:489`），不误删代码行。
★ **I41-9 的连带纪律（v1.1 的核心订正）**：既然判据对象是注释，**新注释就不得原样拼出被禁靶词**——
否则「把改动写好（加说明注释）」本身会让判据变红。⇒ §1.1.3④ 与 §1.5① 的「已推翻」引文已改为
**概括旧意 + 指向 `docs/` 里的原文引文**（`docs/` 不受本机检约束）。
★★ **这两条口径是一对镜像教训**：Phase 40 的坑是「**必须**排除注释」（否则加注释就把判据弄红），
本 Phase 的坑是「**不得**排除注释」（否则旧契约文字藏在注释里查不出来）
⇒ **动手前先回答一句：「我这次判的是代码还是注释？」** 套用上一次的口径必错。

★ **I41-6 的记账**：`ScrollBar::SetRange` 的裸 `Invalidate()`（`ScrollBar.cpp:25-32`）**不计入**本不变量——
它不发失效通道、不碰 `m_layoutDirty`。若审计以「结算期零 Invalidate」为标准，会误判此处为漏网
（详见 §1.5① 与初设 PD41-5）。

---

## §3 测试详设

### 3.1 测试基建（批 0）

#### 3.1.1 `ECDI/src/Tests/WindowProbe.h`（**新增**，`M9Probe` 搬家 + 1 个新方法）

```cpp
#pragma once

// ══════════════════════════════════════════════════════════════════════
// ★ Phase 41：真窗口测试装置 `WindowProbe`
// ══════════════════════════════════════════════════════════════════════
// 来源 = `VisibilityRepaintTests.cpp` 的 anonymous-namespace `M9Probe`（原 `:63-114`）。
// ★ 抽出的依据 = 「第二个真实消费者出现再抽象」：`LayoutInvalidationTests.cpp` 的帧级用例
//   （T41-1/2/4/6/7/8）需要**同一套**「真 Application + 真 Window + FindWindowW + 泵消息 +
//   ValidateRect/GetUpdateRect」。搬运**行为零变化**（5 条 M9 用例判据不动）。
// ★ 两条关键实测事实（沿 M9 原注释，不重复论证）：
//   ① `FindWindowW(nullptr, L"<title>")` 能按标题取到本窗口 HWND——region 判据的前提；
//   ② `WM_PAINT` 无 `BeginPaint/EndPaint` ⇒ **帧计数不可用作判据**，用 region 脏位；
//   ③ 新增：`PumpMessages` 只处理**当下队列里**的消息，队列空即返回 ⇒ 「空泵」**不是**「空闲帧」。

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本头含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Window/Window.h"
#include "Render/RecordingBackend.h"

#include <memory>
#include <utility>

namespace ECDI::Test{

/// @brief 手动消息泵（处理至多 maxCount 条消息后返回；队列空则立即返回）
inline void PumpMessages(int maxCount){

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i){

		TranslateMessage(&msg);
		DispatchMessageW(&msg);

	}

}

/// @brief 真 Application + 真 Window 装置（注入 RecordingBackend 供正向对照观测）
struct WindowProbe{

	Application app;

	Window* window = nullptr;   ///< 非拥有（所有权归 Application）

	HWND hwnd = nullptr;        ///< 平台窗口句柄（FindWindowW 取——region 判据的观测面）

	explicit WindowProbe(const char* title){

		auto recorderOwned = std::make_unique<RecordingBackend>();
		RenderServices services{
			std::move(recorderOwned),
			std::make_unique<RecordingBackend>()   // 测量占位（本组不断言测量）
		};

		window = &app.Create(title, 400, 300, std::move(services));

	}

	WindowProbe(const WindowProbe&) = delete;
	WindowProbe& operator=(const WindowProbe&) = delete;

	~WindowProbe() { if (window) window->Release(); }

	/// @brief 显示并泵若干帧（让窗口进入稳态——region 由 Show 的第一帧弄脏）
	void ShowAndSettle(){

		window->Show();
		PumpMessages(64);

	}

	/// @brief 清空 update region（把「是否变脏」变成可区分二值）
	void ClearRegion(){ ::ValidateRect(hwnd, nullptr); }

	/// @brief update region 是否变脏（非零 = 有绘制请求待处理）
	bool RegionDirty() const {

		RECT rc{};
		return ::GetUpdateRect(hwnd, &rc, FALSE) != 0;

	}

	/// @brief **强制出 1 帧**：请求重绘 → 泵消息
	/// @details ★ 不能改用「空泵」代替：`PumpMessages` 在队列空时立即返回 ⇒ 一帧都不会跑。
	///          T41-4 的判据对象是「**有帧**但不重排」，故必须先造出帧。
	void PumpOneFrame(){

		window->Invalidate();
		PumpMessages(16);

	}

};

}   // namespace ECDI::Test
```

#### 3.1.2 `VisibilityRepaintTests.cpp` 改造（批 0，**行为零变化**）

| 动作 | 细节 |
|---|---|
| 删 | `:47-116` 的 anonymous namespace（`PumpMessages` + `M9Probe` 两份定义） |
| 改 | 顶部 `#include "WindowProbe.h"`；`using namespace ECDI;` 后加 `using namespace ECDI::Test;` |
| 改 | 5 处 `M9Probe probe` → `WindowProbe probe`（`:123`/`:193`/`:221`/`:257` 等） |
| 不动 | 5 条用例的**装置序列与断言**（`:118-280`）——搬家的红线 |

★ `#include <Windows.h>` 与 `#ifdef DrawText` undef 由 `WindowProbe.h` 带入，本文件**理论上**可删——
★ **实况（v1.1 回填）**：**保留了**。原因：5 条用例自己调 `::FindWindowW(nullptr, L"...")` 直接取 HWND
（`probe.hwnd = ...`），删掉就编不过。原本的理由「让『谁需要 Windows.h』只有一个来源」在这个文件里不成立
——**它自己就真的需要**。另：随装置搬走的三行 include（`Application.h` / `RecordingBackend.h` /
`RenderServices.h`）已删，编译验证通过。

#### 3.1.3 `CountingLayout` + `FindFirstBar`（落 `LayoutInvalidationTests.cpp` 匿名 namespace，**必须排在全部用例之前**）

```cpp
/// @brief 计数装饰器：转发给真 Layout —— **只加计数、不改行为**
/// @details ★ 为什么不是「重写一个假 Layout（只 SetPosition）」：那会**改变布局结果**，让
///          A41-2「逐位不变」失去判据（Phase 40 I-3「量具必须与被测行为同构」同族）。
class CountingLayout : public Layout{

public:

	explicit CountingLayout(std::unique_ptr<Layout> inner) : m_inner(std::move(inner)){}

	void Arrange(Widget& parent) override{

		++m_calls;
		m_inner->Arrange(parent);

	}

	[[nodiscard]] std::size_t Calls() const noexcept{ return m_calls; }

private:

	std::unique_ptr<Layout> m_inner;

	std::size_t m_calls = 0;

};
```

`FindFirstBar`（**同一处**，紧随 `CountingLayout` —— 它被 T41-7 使用，必须定义在前）：

```cpp
/// @brief 取 `TextBox` 的滚动条（`TextBox` 唯一的子节点就是条——`TextBox.cpp:44-47`「最后 AddChild」）
/// @details ★ 用 `dynamic_cast` 而不是按索引取：索引依赖「条是第几个子」，而「唯一子节点」是
///          **结构事实**留在了 `TextBox.cpp:44-47` 的注释里，语义更好读。
ScrollBar* FindFirstBar(Widget& parent){

	for (std::size_t i = 0; i < parent.GetChildCount(); ++i){

		if (auto* bar = dynamic_cast<ScrollBar*>(parent.GetChildAt(i))){

			return bar;

		}

	}

	return nullptr;

}
```

#### 3.1.4 `LayoutInvalidationTests.cpp` 文件骨架（v1.1 补，P2-1）

v1.0 只给了用例体、没给文件壳 ⇒ 「可直接抄」不成立。完整骨架如下（用例体按 §3.2 顺序插入到
`// ── 用例区 ──` 之后；**注册块按批次逐行打开**）：

```cpp
#include "RunAllTests.h"
#include "TestFramework.h"
#include "WindowProbe.h"

#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/Layout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Window/Window.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

using namespace ECDI;
using namespace ECDI::Test;

namespace {

// ── 装置（§3.1.3：CountingLayout + FindFirstBar）────────────────────
// ...（两段代码原样粘入）...

// ── 用例区 ──────────────────────────────────────────────────────────
// 批 0：TestT41CountingLayoutGauge
// 批 1：TestT41SetVisibleSchedulesArrange / SetSizeSchedulesArrange / IdleFramesDoNotArrange /
//       HeadlessSetDoesNotArrange / ResizeArrangesOnce / OfflineSubtreeArrangesOnAttach /
//       SubtreeArrangeKeepsRootDirty
// 批 2：TestT41SetTextRequestsRepaint / TestT41PaintTimeBarToggleDoesNotArrange

}   // namespace

void ECDI::Test::RegisterLayoutInvalidationTests(){

	GetTestRegistry().Add("Layout.CountingLayoutGauge",            &TestT41CountingLayoutGauge);            // 批 0

	GetTestRegistry().Add("Layout.SetVisibleSchedulesArrange",     &TestT41SetVisibleSchedulesArrange);     // 批 1
	GetTestRegistry().Add("Layout.SetSizeSchedulesArrange",        &TestT41SetSizeSchedulesArrange);        // 批 1
	GetTestRegistry().Add("Layout.IdleFramesDoNotArrange",         &TestT41IdleFramesDoNotArrange);         // 批 1
	GetTestRegistry().Add("Widget.HeadlessSetDoesNotArrange",      &TestT41HeadlessSetDoesNotArrange);      // 批 1
	GetTestRegistry().Add("Window.ResizeArrangesOnce",             &TestT41ResizeArrangesOnce);             // 批 1
	GetTestRegistry().Add("Widget.OfflineSubtreeArrangesOnAttach", &TestT41OfflineSubtreeArrangesOnAttach); // 批 1
	GetTestRegistry().Add("Layout.SubtreeArrangeKeepsRootDirty",   &TestT41SubtreeArrangeKeepsRootDirty);   // 批 1

	GetTestRegistry().Add("Text.SetTextRequestsRepaint",           &TestT41SetTextRequestsRepaint);         // 批 2
	GetTestRegistry().Add("Layout.PaintTimeBarToggleDoesNotArrange", &TestT41PaintTimeBarToggleDoesNotArrange); // 批 2

}
```

★ **为什么用 `void ECDI::Test::RegisterLayoutInvalidationTests(){`** 这种「限定名定义」写法：
用例体在**匿名 namespace** 里（不可外部链接），注册函数必须在 `ECDI::Test` 里可见它们 ——
限定名定义正好把「定义在 `ECDI::Test`、函数体在 TU 内」两件事同时满足，与 `VisibilityRepaintTests.cpp`
的 `void ECDI::Test::RegisterVisibilityRepaintTests() {`（`:282`）同型。
★ **`RunAllTests.cpp` 的调用行**插在 `RegisterPlatformQuickfixTests();`（`:54`）之后：
`    RegisterLayoutInvalidationTests();  // ★ Phase 41：帧级布局失效结算（T41-1..10）`

### 3.2 十条用例（装置 + 期望值 + 判据）

#### T41-10 · `Layout.CountingLayoutGauge`（**批 0**，量具校准 + A41-2 装置保证）

```cpp
void TestT41CountingLayoutGauge(){

	// 同一棵树跑两遍：裸 VerticalLayout vs CountingLayout(VerticalLayout)
	auto build = [](Widget& root, std::unique_ptr<Layout> layout){

		root.SetSize(200, 200);
		root.SetLayout(std::move(layout));

		for (int i = 0; i < 3; ++i){
			auto child = std::make_unique<Widget>();
			child->SetSize(50, 20 + i * 10);
			root.AddChild(std::move(child));
		}

	};

	Widget bare;
	build(bare, std::make_unique<VerticalLayout>());

	Widget wrapped;
	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	build(wrapped, std::move(counting));

	bare.Arrange();
	wrapped.Arrange();

	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(1));

	// ★ 逐位相同——这是 A41-2「布局结果不变」在**装置层**的保证
	for (std::size_t i = 0; i < bare.GetChildCount(); ++i){

		const Widget* a = bare.GetChildAt(i);
		const Widget* b = wrapped.GetChildAt(i);

		EXPECT_EQ(a->GetX(), b->GetX());
		EXPECT_EQ(a->GetY(), b->GetY());
		EXPECT_EQ(a->GetWidth(), b->GetWidth());
		EXPECT_EQ(a->GetHeight(), b->GetHeight());

	}

	wrapped.Arrange();
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(2));   // 计数随调用次数线性增长

}
```

★ **为什么这条不能省**：T41-4/6/7 的判据全是「`Calls()` 增量 == N」；若计数装饰器本身不计数（或
包装后改变几何），那三条会一起假绿/假红。这是 M9「量具校准」（`VisibilityRepaintTests.cpp:215-246`）同族纪律。

#### T41-1 · `Layout.SetVisibleSchedulesArrange`（批 1，A41-3）

```cpp
void TestT41SetVisibleSchedulesArrange(){

	WindowProbe probe("ECDI_P41_Vis");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Vis");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(200, 300);
	panel->SetLayout(std::make_unique<VerticalLayout>());

	Widget* buttons[3]{};
	for (int i = 0; i < 3; ++i){
		auto b = std::make_unique<Widget>();
		b->SetSize(100, 40);
		buttons[i] = b.get();
		panel->AddChild(std::move(b));
	}

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));
	probe.PumpOneFrame();     // 首帧结算：y = 0 / 40 / 80

	EXPECT_EQ(buttons[0]->GetY(), 0);
	EXPECT_EQ(buttons[1]->GetY(), 40);
	EXPECT_EQ(buttons[2]->GetY(), 80);

	// ★ 被测量：**只改可见性**，不写任何 Arrange
	buttons[1]->SetVisible(false);
	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());   // 布局通道已置脏（父有 Layout）

	probe.PumpOneFrame();

	EXPECT_EQ(buttons[2]->GetY(), 40);          // ★ 第三个自动前移（修复前停在 80）
	EXPECT_EQ(buttons[1]->GetX(), -100);        // C-VIS-4 停泊自身 bbox 负区
	EXPECT_EQ(buttons[1]->GetY(), -40);
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());  // 结算已清脏

}
```

#### T41-2 · `Layout.SetSizeSchedulesArrange`（批 1，A41-4）

```cpp
void TestT41SetSizeSchedulesArrange(){

	WindowProbe probe("ECDI_P41_Size2");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Size2");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(300, 100);
	panel->SetLayout(std::make_unique<HorizontalLayout>());

	auto b0 = std::make_unique<Widget>();
	b0->SetSize(50, 20);
	Widget* left = b0.get();
	panel->AddChild(std::move(b0));

	auto b1 = std::make_unique<Widget>();
	b1->SetSize(100, 20);
	Widget* right = b1.get();
	panel->AddChild(std::move(b1));

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));
	probe.PumpOneFrame();

	EXPECT_EQ(left->GetX(), 0);
	EXPECT_EQ(right->GetX(), 50);

	left->SetSize(80, 20);              // ★ 只改尺寸，不写 Arrange
	probe.PumpOneFrame();

	EXPECT_EQ(right->GetX(), 80);       // ★ 兄弟自动右移（修复前停在 50）

}
```

#### T41-3 · `Text.SetTextRequestsRepaint`（批 2，A41-5）

```cpp
void TestT41SetTextRequestsRepaint(){

	WindowProbe probe("ECDI_P41_SetText");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_SetText");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	// ★★ 门控前置（v1.1 订正，P1-2）：给**根**挂 Layout ⇒ `label->ParentHasLayout()` 为真。
	//   否则「不置脏」可能只是被 D41-4 门控拦下（**假绿**），而不是 D41-5「SetText 不走布局通道」。
	//   ——这条前置是本用例**唯一**让 D41-5 与 D41-4 可区分的手段。
	probe.window->GetRootWidget().SetLayout(std::make_unique<VerticalLayout>());

	auto owned = std::make_unique<Label>();
	Label* label = owned.get();
	label->SetSize(120, 24);
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.PumpOneFrame();     // 结算建树期失效 ⇒ 根脏归零

	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

	// ── 左值重载
	const std::string text = "phase41";
	probe.ClearRegion();
	label->SetText(text);

	EXPECT_TRUE(probe.RegionDirty());                             // ★ 重绘通道（修复前：静默不刷新）
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());    // ★ 布局通道**不参与**（D41-5 严格判据）

	// ── 右值重载（★ 两个重载都要覆盖：D41-5 的落点是 `TextWidget.cpp` 的**两个**函数体）
	probe.ClearRegion();
	label->SetText(std::string("phase41-rv"));

	EXPECT_TRUE(probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}
```

#### T41-4 · `Layout.IdleFramesDoNotArrange`（批 1，A41-6）

```cpp
void TestT41IdleFramesDoNotArrange(){

	WindowProbe probe("ECDI_P41_Idle");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Idle");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	for (int i = 0; i < 3; ++i){
		auto child = std::make_unique<Widget>();
		child->SetSize(60, 30);
		probe.window->GetRootWidget().AddChild(std::move(child));
	}

	probe.ShowAndSettle();

	const std::size_t baseline = counter->Calls();
	EXPECT_TRUE(baseline >= 1);       // 前置：建树时的失效确实被结算过（否则「不增」无意义）

	for (int i = 0; i < 3; ++i){
		probe.PumpOneFrame();         // ★ 真有帧（不是空泵）
	}

	EXPECT_EQ(counter->Calls(), baseline);   // ★ 空闲帧不重排

}
```

#### T41-5 · `Widget.HeadlessSetDoesNotArrange`（批 1，A41-7）

```cpp
void TestT41HeadlessSetDoesNotArrange(){

	// ★ 无窗口：置脏是**纯内存写**（D41-8），但没有任何帧首 ⇒ 一次都不结算
	Widget root;
	root.SetSize(200, 200);

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	root.SetLayout(std::move(counting));

	auto owned = std::make_unique<Widget>();
	Widget* child = owned.get();
	child->SetSize(50, 50);
	root.AddChild(std::move(owned));

	EXPECT_TRUE(root.NeedsLayout());          // AddChild ⇒ 置脏（父有 Layout）
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));

	child->SetSize(60, 40);                   // 门控：父有 Layout ⇒ 置脏
	EXPECT_TRUE(root.NeedsLayout());
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));   // ★ 无帧首 ⇒ 不结算

	child->SetVisible(false);                 // 同上
	EXPECT_TRUE(root.NeedsLayout());

	child->SetPosition(10, 10);               // 同上
	EXPECT_TRUE(root.NeedsLayout());

	// ★ 反向：没有 Layout 参与时，一律不置脏
	Widget leaf;
	leaf.SetSize(10, 10);
	EXPECT_TRUE(!leaf.NeedsLayout());     // 无父、无 Layout

	Widget orphan;                        // ★ v1.1 补（P1-2）：构造了就必须断言，否则是死装置
	orphan.AddChild(std::make_unique<Widget>());
	EXPECT_TRUE(!orphan.NeedsLayout());   // 子集合变了，但 orphan 无 Layout ⇒ 不置脏

	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));

}
```

#### T41-6 · `Window.ResizeArrangesOnce`（批 1，A41-8）

```cpp
void TestT41ResizeArrangesOnce(){

	WindowProbe probe("ECDI_P41_Resize");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Resize");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	auto child = std::make_unique<Widget>();
	child->SetSize(60, 30);
	probe.window->GetRootWidget().AddChild(std::move(child));

	probe.ShowAndSettle();

	const std::size_t baseline = counter->Calls();

	::SetWindowPos(probe.hwnd, nullptr, 0, 0, 520, 360, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	probe.PumpOneFrame();
	probe.PumpOneFrame();     // 再一帧：确保「残留脏」也会被暴露

	// ★ 判据：一次 WM_SIZE 恰一次 Arrange（`OnResized` 同步 Arrange 清脏 ⇒ 帧首不再排）
	EXPECT_EQ(counter->Calls() - baseline, static_cast<std::size_t>(1));
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}
```

★ **若首跑得到 `2`**：先查是否收到**两条** `WM_SIZE`（`SWP_NOMOVE|SWP_NOACTIVATE` 下系统仍可能因
边框/DPI 重算再发一条）。⇒ 处置顺序：① 打印 `counter` 增量与实际 `WM_SIZE` 次数；② 若确为两条
`WM_SIZE`，判据改「**每次 `WM_SIZE` 恰 1 次**」（计数 WM_SIZE 累加），**不得**直接放宽成 `<= 2`。

#### T41-7 · `Layout.PaintTimeBarToggleDoesNotArrange`（批 2，A41-9 + D41-4）

```cpp
void TestT41PaintTimeBarToggleDoesNotArrange(){

	WindowProbe probe("ECDI_P41_Bar");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Bar");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	// 装置：计数器挂在**根**上（TextBox 的直接父），条 toggle 的门控落在 TextBox 这一级
	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	auto owned = std::make_unique<TextBox>();
	TextBox* box = owned.get();
	box->SetSize(160, 40);            // ★ 高度刻意压小：让 10 行内容必然溢出
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ShowAndSettle();

	ScrollBar* bar = FindFirstBar(*box);
	EXPECT_TRUE(bar != nullptr);
	if (bar == nullptr) return;

	// ── 段 0 前置校验：空文本 ⇒ 条已被首帧 SyncScrollBar 翻成**不可见**
	//    （默认 visible=true，不先确认「翻下去」，「翻上来」就无从判定）
	EXPECT_TRUE(!bar->IsVisible());

	const std::size_t baseline = counter->Calls();

	// ── 段 1 状态翻转校准 + 段 2 计数判据
	// ★★ v1.1 订正（P1-2）：**必须先 ClearRegion**——`ShowAndSettle` 弄脏的 region 在
	//   「无 BeginPaint」的平台层**永不 validate** ⇒ 不清则下面那条 RegionDirty 断言**恒真**（假绿）。
	probe.ClearRegion();

	box->SetText("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");

	EXPECT_TRUE(probe.RegionDirty());          // SetText 的重绘通道（A41-5 同源）

	probe.PumpOneFrame();
	probe.PumpOneFrame();
	probe.PumpOneFrame();

	EXPECT_TRUE(bar->IsVisible());             // ★ 校准：条真的翻成可见（否则段 2 是假绿）
	EXPECT_EQ(counter->Calls(), baseline);     // ★ 判据：条 toggle 只走重绘通道

	// ── 段 3 正对照（同装置）：父**有** Layout 的可见性变化 ⇒ 必须多排一次
	box->SetVisible(false);
	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());
	probe.PumpOneFrame();
	EXPECT_EQ(counter->Calls() - baseline, static_cast<std::size_t>(1));

}
```

★ `FindFirstBar` 的定义在 **§3.1.3**（与 `CountingLayout` 同处匿名 namespace）——**顺序不可颠倒**：
按本稿顺序粘贴时，函数定义必须出现在调用点之前，否则 `use of undeclared identifier`（v1.1 订正，P2-1；
v1.0 把它放在用例之后，是「可直接抄」承诺的漏洞）。

★ **装置前置写死进注释**：`m_singleLine` 默认 `false`（`TextBox.h:338`）⇒ 多行模式天然成立；文本含 **9 个
`\n`**、控件高 40 ⇒ `GetMaxScrollOffset() > 0`（`TextBox.cpp:733` 的 `need` 判据）。★ 若只给**长单段**
文本（无 `\n`、不开折行）⇒ 行表 1 行 ⇒ 条永不出现 ⇒ **段 0 红**（这是**好的**红：暴露装置错，不是机制错）。

#### T41-8 · `Widget.OfflineSubtreeArrangesOnAttach`（批 1，A41-10）

```cpp
void TestT41OfflineSubtreeArrangesOnAttach(){

	WindowProbe probe("ECDI_P41_Attach");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Attach");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	// ── 离线搭树：panel 是**临时根**，脏都写在它身上
	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(200, 200);
	panel->SetLayout(std::make_unique<VerticalLayout>());

	auto b0 = std::make_unique<Widget>();
	b0->SetSize(100, 30);
	Widget* first = b0.get();
	panel->AddChild(std::move(b0));

	auto b1 = std::make_unique<Widget>();
	b1->SetSize(100, 50);
	Widget* second = b1.get();
	panel->AddChild(std::move(b1));

	EXPECT_TRUE(panel->NeedsLayout());    // 离线期：脏落在临时根上
	EXPECT_EQ(second->GetY(), 0);         // 尚未排过（口径锁定，证明下面不是「本来就对」）

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));   // ★ 子树脏合并

	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());

	// ★★ v1.1 补（P1-2）：**非根查询**——panel 自己的 `m_layoutDirty` 已被 AddChild 清成 false，
	//   只有**上溯到真根**才可能返回 true ⇒ 这是 `NeedsLayout()` 里 `while` 循环的**唯一**覆盖点。
	//   （缺这一条，把 `NeedsLayout()` 写成 `return m_layoutDirty;` 也能让 556 条全绿。）
	EXPECT_TRUE(panel->NeedsLayout());

	probe.PumpOneFrame();

	EXPECT_EQ(first->GetY(), 0);
	EXPECT_EQ(second->GetY(), 30);        // ★ 挂入后首帧即排（修复前停在 0）
	EXPECT_TRUE(!panel->NeedsLayout());   // 结算后非根同样读得到「已干净」

}
```

#### T41-9 · `Layout.SubtreeArrangeKeepsRootDirty`（批 1，D41-3.2）

```cpp
void TestT41SubtreeArrangeKeepsRootDirty(){

	// ★ 无窗口可测：`Arrange()` 是公开 API
	Widget root;
	root.SetSize(200, 200);

	auto childOwned = std::make_unique<Widget>();
	Widget* child = childOwned.get();
	child->SetSize(100, 100);
	child->SetLayout(std::make_unique<VerticalLayout>());
	root.AddChild(std::move(childOwned));

	EXPECT_TRUE(root.NeedsLayout());      // 置脏寄存在**根**
	EXPECT_TRUE(child->NeedsLayout());    // ★ 非根查询：child 自己的标志是 false，须**上溯**才为真

	child->Arrange();                     // ★ 子树 Arrange
	EXPECT_TRUE(root.NeedsLayout());      // ★ 不清（兄弟分支可能仍脏）——D41-3.2
	EXPECT_TRUE(child->NeedsLayout());    // 同上（子树 Arrange 不清根脏）

	root.Arrange();                       // ★ 根 Arrange
	EXPECT_TRUE(!root.NeedsLayout());     // 完成即清
	EXPECT_TRUE(!child->NeedsLayout());   // 非根同步读到「已干净」

}
```

#### 翻转用例 · `Widget.SetSizeRequestsRepaint`（批 1，D41-11 方案 A）

替换 `VisibilityRepaintTests.cpp:187-213` 整个 `TestM9SetSizeStillNoRepaint`，并把 `:286` 的注册名
`Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`：

```cpp
// ── M9-③ → ★ Phase 41：几何 setter **请求重绘**（契约变更后的正向守卫）────────────
//   ★ 本条是 `Widget.SetSizeStillNoRepaint` 的**翻转**（需求 D41-11 方案 A 的配套动作）。
//     旧条自述「钉住范围边界……若将来有人『顺手统一』，本条会红——那就是范围蔓延的信号」
//     ⇒ Phase 41 **正是**要改变那个意图，翻转是**契约变更的配套**，不是掩盖回归。
//   ★ 新旧判据的差别（为什么拆两条）：
//     · **重绘通道**：值真变化 ⇒ `Invalidate()`（旧契约说「不发」，新契约说「发」）；
//     · **布局通道**：`Button` 与 `RootWidget` **都没有 `Layout`** ⇒ 门控（D41-4）拦下 ⇒ **不置脏**。
//       这一条在两侧都是「不变」——它守的**不是**旧契约，而是**新门控的边界**。
void TestPhase41SetSizeRequestsRepaint(){

	WindowProbe probe("ECDI_P41_SetSize");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_SetSize");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	// ★ 前置：结算跑过 ⇒ 根脏为假（否则下面「不置脏」的断言无判别力）
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

	probe.ClearRegion();
	button->SetSize(120, 40);
	EXPECT_TRUE(probe.RegionDirty());                              // ★ 重绘通道：必须请求重绘
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());     // ★ 门控通道：无 Layout 读它 ⇒ 不置脏

	probe.ClearRegion();
	button->SetPosition(30, 30);
	EXPECT_TRUE(probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

	// ★ 同值守卫（R6 / D41-3 末段）：值没变 ⇒ **两条通道都不动**
	probe.ClearRegion();
	button->SetSize(120, 40);
	EXPECT_TRUE(!probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}
```

### 3.3 用例分布与批次计数

| 批次 | 新增用例 | 累计 |
|---|---|---|
| 起点 | — | **546** |
| 批 0 | T41-10 | **547** |
| 批 1 | T41-1 / 2 / 4 / 5 / 6 / 8 / 9（7 条）+ 翻转（**不计数**，替换旧条） | **554** |
| 批 2 | T41-3 / 7（2 条） | **556** |

---

## §4 批次与实施顺序

### 批 0 · 测试基建（**产品码零改动**）

1. 新增 `src/Tests/WindowProbe.h`（§3.1.1）。
2. 改 `VisibilityRepaintTests.cpp`：删匿名 namespace、换名、加 `using`（§3.1.2）。
3. 新增 `src/Tests/LayoutInvalidationTests.cpp`：`CountingLayout` + **仅 T41-10**。
4. `RunAllTests.h` 加 `void RegisterLayoutInvalidationTests();`；`RunAllTests.cpp` 加一行调用。
5. **验收**：`ecdi_tests` = **547 passed / 0 failed**。
   ★ 若 M9 五条有任何一条变红 ⇒ 搬家过程改了行为，**回退重搬**，不许调判据。

### 批 1 · 机制 + 契约文字 + 翻转（**必须一次做完**）

1. `Widget.h`：§1.1.1 总纲 → §1.1.2 `NeedsLayout` → §1.1.3 五处注释 → §1.1.4 私有面 → §1.1.5 `Arrange` 注释。
2. `Widget.cpp`：§1.2.1 四个新函数 → §1.2.2 `SetPosition` → §1.2.3 `SetSize` → §1.2.5 `SetVisible`
   → §1.2.6 `SetStretch` → §1.2.7 `AddChild` → §1.2.8 `RemoveChild` → §1.2.9 `SetLayout` → §1.2.10 `Arrange`。
3. `Window.cpp`：§1.3 两处。
4. `Application.h/.cpp`：§1.4 删除。
5. `ScrollView.cpp` / `ScrollView.h`：§1.5 两处（**纯注释**）。
6. 翻转用例（§3.3 末）+ 新增 T41-1/2/4/5/6/8/9。
7. **语法预检**（每步后跑，成本数秒）：

```bash
g++ -std=c++20 -fsyntax-only -I ECDI/include -I ECDI/src -I ECDI/src/Tests \
    -I third_party/freetype/include ECDI/src/Widget/Widget.cpp
```

8. **验收**：`ecdi_tests` = **554 passed / 0 failed**。

### 批 2 · 同族收敛

1. `TextWidget.cpp`（§1.6）→ `TextBox.h/.cpp`（§1.7，**三处同一提交**）→ `CollapsiblePanel.cpp` /
   `CaptionBar.cpp`（§1.8，纯注释）。
2. 新增 T41-3 / T41-7。
3. **验收**：`ecdi_tests` = **556 passed / 0 failed**（四构建链，最终由用户侧跑 MSVC / Clang / ClangCL）。

★ **为什么批 1 不能拆**：`AddChild` 合并、`Arrange` 清脏、出口、`PaintFrame` 结算四者互为前提——
拆开做等于让「半边脏」活过一个批次。★ **为什么批 2 可独立回退**：它只收敛「重复动作」，
回退后机制仍在、只是多了几次无意义 `Invalidate`（正确性不受影响）。

---

## §5 影响面与规模

| 项 | 内容 |
|---|---|
| 源码 | **6 个 .cpp**（`Widget` / `Window` / `Application` / `ScrollView` / `TextWidget` / `TextBox`）+ **2 个纯注释**（`CollapsiblePanel` / `CaptionBar`） |
| 头 | **5 个**（`Widget.h` / `Application.h` / `ScrollView.h` / `TextWidget.h` / `TextBox.h`） |
| 测试 | 1 新增头（`WindowProbe.h`）+ 1 新增 cpp（`LayoutInvalidationTests.cpp`）+ 2 改动（`VisibilityRepaintTests.cpp` / `RunAllTests.*`） |
| 公共头 | **100 → 100** |
| public API | **+1**（`NeedsLayout`）；`SetPosition` 签名不变（仅移出 inline） |
| `sizeof(Widget)` | **+8** |
| CMake | **0** |
| 用例 | **546 → 556**（+10；545 守恒 + 1 翻转） |
| docs | **206 → 207**（本稿） |
| 编译批次 | ★ **一次近全量**（`Widget.h` 被几乎所有 TU 包含）⇒ 与 Phase 40 同量级（~35 min / 链 × 4） |
| 对外可感行为 | ① 业务不必再手写 `Arrange`/`Invalidate` 链；② `ModelProbe` 搜索过滤的活体 bug **自动修复**（`examples` **零改动**）；③ **`AddChild`/`RemoveChild` 现在恒发重绘**；④ 动画期每次值变化会在下帧触发全树重排（已知接受，#80 再优化） |

---

## §6 风险与回退

| # | 风险 | 对策 / 回退 |
|---|---|---|
| **R41-1** | `Widget.h` 近全量重编 | 必然。批 1 前逐步跑 `-fsyntax-only`（数秒）抓真错 |
| **R41-2** | `AddChild`/`RemoveChild` 新增 `Invalidate()` 撞上既有 region 断言 | 已核 M9 五条都在 `AddChild` **之后**才 `ClearRegion()`。★ 四链首跑若出现意外 region 红，先查这条（I41 表外的唯一行为新增） |
| **R41-3** | 一次 `WM_SIZE` 实测出 **2** 次 `Arrange` | 先查是否两条 `WM_SIZE`；若确为两条，判据改「每次 `WM_SIZE` 恰 1 次」，**不得**放宽为 `<= 2`（§3.2 T41-6 注） |
| **R41-4** | `SetVisible` 的三个既有 `SyncBars` 站点突然开始置脏 | 已核四个复合控件**均无** `SetLayout`（`grep -rn "SetLayout" ECDI/src --include=*.cpp \| grep -v Tests` ⇒ 只有定义）⇒ 门控全拦。★ 将来给 `TextBox` 加 Layout 时这批站点会开始置脏——那是**正确**行为 |
| **R41-5** | `Window` 构造期 `root->SetSize`（`Window.cpp:125`）现在多一次 `Invalidate()` | `m_platformWindow` 在 `:113` 之前已创建、`hwnd` 有效 ⇒ `InvalidateRect` 合法（窗口未显示，无害）。已于 §1.2.3 的路径上记账 |
| **R41-6** | `NeedsLayout()` 的**整树**语义被误读为「本节点」 | 名字 + `@details` 双保险；备选名 `HasPendingLayout()`（本稿取前者，与 `HasFocus()` 同族） |
| **R41-7** | 批 2 删 `TextBox::SetText` 后行为漂移 | 等价性论证见 §1.7①；★ 判据 = 批 2 前后 `556 / 556` 且 `TextBox` 相关用例零改动 |
| **R41-8** | `CollapsiblePanel` 在 `fillCrossAxis` 容器里炸（#80 雷区） | **本 Phase 只登记**：类头总纲 ⚠️ + 源码注释。红线① 要求布局结果不变，修它 = 另一次行为变更 |
| **R41-9** | 四构建链耗时长，中途红难定位 | 按批推进 + 每批先 MinGW 自检（AI 可自跑，增量 ~30s）；MSVC / Clang / ClangCL 由用户侧终验 |
| **R41-10** | 「全绿」记账不完整 | 报全绿必写：**断言是否启用**（`FRAMEWORK_ASSERT` 仅 `_DEBUG`）+ **当时显示器缩放**（1920×1080 @ DPI 120）+ **哪几条链谁跑的** |

---

## §7 文档同步清单（本 Phase 内）

| # | 文件 | 动作 | 何时 |
|---|---|---|---|
| 1 | `roadmap-deferred.md` | **已做**（v2.64：新增版本行 + #79 行内 ⑥ 订正 + 行尾立项状态） | 已完成 |
| 2 | `phase25-list-container-preliminary-design.md:51` | **已做**（B2 订正 + 行号失效标注） | 已完成 |
| 3 | `roadmap-deferred.md` | 收口时：#79 迁「✅ 已完成」五列格式 + 版本行 v2.65 | 收口 |
| 4 | 本稿 §9 | 实施回填（实际行号 / 读数 / 实施期订正） | 每批后 |
| 5 | `README.md` | 若其中有「测试 N 条」锚点则同步 556 | 收口 |

---

## §8 实施回填（**实施期填写，现在留空**）

| 项 | 计划值 | 实际值 | 差异说明 |
|---|---|---|---|
| 批 0 用例数 | 547 | **547**（2026-10-30 MinGW 实跑：`547 passed / 0 failed / 547 total`，`[FAIL]` 行 0） | 与计划一致 |
| 批 0 施工期两处偏差 | — | ① **`<Windows.h>` 未删**（§3.1.2 原计划删）：真窗口用例自己调 `::FindWindowW` 直接取 HWND，删了编不过 ⇒ 保留，`WindowProbe.h` 只保证「装置不需要额外 include」；② **`FindFirstBar` 未随批 0 落地**：它唯一的消费者是批 2 的 T41-7，提前放入会产生 `-Wunused-function`（匿名 namespace）⇒ 改在批 2 与用例同时追加 | 两处都是「纸面顺序 vs 编译现实」的落差，已回填 |
| 批 1 用例数 | 554 | **554**（2026-10-30 **用户侧四构建链全绿**：MSVC / Clang / ClangCL / MinGW；翻转 1 条 + 新增 7 条 = 547 + 7） | 与计划一致 |
| 批 2 用例数 | 556 | **556**（2026-10-30 **用户侧四构建链全绿**：MSVC / Clang / ClangCL / MinGW；新增 T41-3 / T41-7 = 554 + 2） | 与计划一致 |
| I41-1 `NotifyGeometryChanged` **代码行**计数（排除注释） | 8 | **8** = `:715` 定义 + `:119` AddChild / `:170` RemoveChild / `:396` SetLayout / `:490` SetPosition / `:513` SetSize / `:536` SetVisible / `:564` SetStretch | ★ 实测 = 预测（v1.0 的「9」是口径错，v1.1 已订正） |
| I41-2 置真写入点 | 1 | **1**（`Widget.cpp:734`） | ✓ |
| I41-3 清脏点 | 2 | **2**（`:107` AddChild 合并前清 / `:464` 根 Arrange 清） | ✓ |
| I41-4 结算驱动点 | 2 | **2**（`Widget.cpp:743` 定义 + `Window.cpp:190` 调用） | ✓ |
| I41-5 Arrange 调用点 | 1 | **1**（`Window.cpp:849`） | ★ `Application.cpp` 的 `.Arrange()` 已删 |
| I41-6 `m_arrangeDepth` 代码行 | 4 | **4**（`:451` `++` / `:455` `--` / `:462` 清脏判断 / `:725` 抑制判断） | ★ 注释里刻意不写成员名，故不必排除亦有 4 |
| I41-7 门控表达式 | 1 | **1**（`Widget.h:471` `ParentHasLayout()` 内联体） | ✓ |
| I41-9 旧契约靶词 | 0 / 0 | **0 / 0**（`ECDI/` 全库） | ★ 施工期自证：v1.1 的「不复述靶词」纪律真的挡住了自指 |
| `Widget.cpp` 净增行 | ~+150 / −20 | **+183 / −7**（`git diff --numstat` 实测；`Widget.h` +104/−6） | ★ 纸面预估偏保守（总纲注释块比预期长） |
| 首次全量编译耗时 | ~35 min / 链 | MinGW 本机 >6 min 未跑完即停（批 1/批 2 均由用户侧四链替代） | ★ 不承诺读数 |
| 四链结果 | 556 / 556 × 4 | **批 2 收口 = 556 / 556 × 4**（批 1 中间态 554 亦四链通过） | ✓ 全链收口 |
| 批 2 实际改动 | 6 产品文件 +25/−38 · 测试 +2 用例 + `FindFirstBar`（`LayoutInvalidationTests.cpp` 394 → 512 行） | `TextWidget.cpp` +9 · `TextWidget.h` +4/−1 · `TextBox.h` +2/−11 · `TextBox.cpp` +3/−26 · `CollapsiblePanel.cpp` +5 · `CaptionBar.cpp` +2 | 纯注释两文件（CollapsiblePanel / CaptionBar）无行为改动 |

★ **批 0 的「量具校准」实跑证据**：`Layout.CountingLayoutGauge` 在 547 条中排最后（注册序），绿；
M9 五条（含**尚未翻转**的 `Widget.SetSizeStillNoRepaint`）全部保留原判据并在册 ⇒ 搬家未改行为。

---

## §9 修订记录

- **v1.3（2026-10-30）** **批 2 施工回填 + 全链收口**：`TextWidget.cpp`（两个 `SetText` 各补
  `Invalidate`，**不加同值守卫**——同文本 bump revision 是既有语义，改它超出 D41-5 范围）·
  `TextBox.h`（删 `SetText` 两 override 声明 + `SetSize` 注释订正）· `TextBox.cpp`（删两个
  `SetText` 实现 + 删 `SetSize` 末尾 `Invalidate`——D41-F，结算期抑制的必要条件）·
  `TextWidget.h:34` 注释订正 · `CollapsiblePanel.cpp` / `CaptionBar.cpp`（纯注释：雷区预警 +
  「本 override 不 Invalidate」）· 测试 T41-3 / T41-7 + `FindFirstBar`（批 2 与用例同时落地，
  避免匿名 namespace 未使用函数）。**四构建链 556 / 556**（用户侧）。
  ★ **批 2 机检**：`TextBox::SetText` 全库 0 残留 · `TextBox::SetSize` 内 `Invalidate()` 代码行 0
  （仅注释）· `TextWidget::SetText` 两函数体各 1 条代码行 `Invalidate()`。
  ★ **行为净效果**：删 override 后 `TextBox::SetText` 经 vtable 走基类——旧 override 会把
  `m_textRevision` **重复 bump 两次**（基类一次 + 自己一次），删除反而消除了冗余自增（详设审核③的实证结论）。

- **v1.2（2026-10-30）** **批 1 施工回填**（机制 + 契约文字 + 翻转）：`Widget.h`（总纲 / `NeedsLayout` /
  5 处注释 / 私有面 / 2 成员 / `SetPosition` 移出）· `Widget.cpp`（4 新函数 + 6 改写 + 2 插入）·
  `Window.cpp`（帧首结算 + `OnResized`）· `Application.h/.cpp`（删 `OnWindowResized`）·
  `ScrollView.cpp:337` + `ScrollView.h`（纯注释）· 翻转用例 + T41-1/2/4/5/6/8/9。**四构建链 554 / 554**（用户侧）。
  ★ **九条机检全部实测 = 预测**（见 §8）：I41-1 = 8、I41-2 = 1、I41-3 = 2、I41-4 = 2、I41-5 = 1、
  I41-6 = 4、I41-7 = 1、I41-9 = 0 / 0。**这是本 Phase 第一次「先算的数字全对」**——前两个 Phase 各栽过一次
  「算的和跑的差一」，差别在于这次 v1.1 把两条 grep 口径（代码 / 注释）明确分开并逐条写了期望。
  ★ **施工期一处自证**：写 `Widget.h` 的 `AutoSize` 新注释时，我又顺手把靶词写进去了（P1-1a 同款），
  落笔后立刻 `grep` 抓到 ⇒ 立即改成「不复述靶词」的表述。**判据真的挡住了**——但挡住的原因是「跑了 grep」，
  不是「记得纪律」。⇒ 结论写进纪律：**注释类判据必须在落笔后立刻跑，不能等收尾**。

- **v1.1（2026-10-30）** 收 **Gemini 4 Argon** 详设审核（**2 P1 + 3 P2**）。**五项全部成立、零驳回**；
  另有价值的是——**§9 列出的三项盯防点经审核逐条对码核实全部成立（零隐患）**，其中盯防②由审核方
  实证「同线程 `SetWindowPos` 经 `WM_WINDOWPOSCHANGED` → `DefWindowProcW` **恰派发 1 条 `WM_SIZE`**」
  （与 `WindowChromeTests.cpp` 的 6 处既有用法同型）⇒ §3.2 T41-6 的「若首跑得 2」降为**兜底预案**，
  不再当主路径怀疑。

  | 编号 | 性质 | 落点 |
  |---|---|---|
  | **P1-1a** | ★ **机检自洽性（最重的一条）** | §1.1.3④ 与 §1.5① 的「已推翻」引文**原样拼出了被禁靶词** ⇒ I41-9 会自我击穿。改为「概括旧意 + 指向 `docs/` 原文引文」，并在 §2 补**两条 grep 口径的镜像纪律** |
  | **P1-1b** | 机检口径 | I41-6 期望 4，但 §1.2.10 的注释写了成员名 ⇒ 实为 5。注释改「第二个条件（**深度已归零**）」，机检命令同时加注释排除 |
  | **P1-1c** | 机检口径 | I41-1 表内数字自相矛盾（7 / 9）。统一为 **8 = 1 定义 + 7 调用**（命令**排除注释**）；`AddChild` 的**反例注释**保留标识符（那是有价值的施工警告），并在表内写明「不排除会得 9，那是口径错」 |
  | **P1-2a** | 测试假绿 | T41-3 的 `!NeedsLayout()` 会被 D41-4 门控拦成假绿 ⇒ **给根挂 `VerticalLayout`** 使 `ParentHasLayout()` 为真；并**补右值重载**覆盖（D41-5 的落点是两个函数体） |
  | **P1-2b** | 测试假绿 | T41-7 在 `SetText` 前**漏 `ClearRegion()`** ⇒ `RegionDirty()` 恒真。已补，并写明平台层 reason（无 `BeginPaint` ⇒ region 永不 validate） |
  | **P1-2c** | 覆盖缺口 | `NeedsLayout()` 的**上溯循环 0% 覆盖**（全部调用都在根上）⇒ T41-8 补 `panel->NeedsLayout()`（挂入后 / 结算后各一条）、T41-9 补 `child->NeedsLayout()` 两条 |
  | **P1-2d** | 死装置 | T41-5 构造了 `orphan` 却没断言 ⇒ 补 `!orphan.NeedsLayout()` |
  | **P2-1** | 施工顺序 | `FindFirstBar` 原文排在调用点之后（粘贴即编译错）⇒ 上移 §3.1.3；新增 **§3.1.4 文件骨架**（includes + 匿名 namespace 分段 + 按批次打开的注册块 + `RunAllTests.cpp` 的调用行） |
  | **P2-2** | 稿内矛盾 | §1.2.5 施工说明说「四行注释保留」而代码块已重写 ⇒ 以代码块为准改说明；§1.3② 代码块漏了 `Arrange()` 的行尾注释 ⇒ 补回 |
  | **P2-3** | 相邻注释残留 | `TextBox.h:93-95`（「EnsureCaretVisible + Invalidate」）与 `TextWidget.h:34`（「TextBox override 置行缓存失效」）在 §1.7 删改后**立刻失真** ⇒ 新增 §1.7.4 一并订正；§0 / §5 的头数由 4 → **5** |

  ★ **本轮的方法论收获**（已写进 §2）：机检口径没有「通用正确答案」，**每次都要先问「判的是代码还是注释」**——
  Phase 40 的坑是必须排除注释，本 Phase 的坑是不得排除。P1-1a 更狠：它伤的是**判据自己**
  （「把改动写好」直接让判据变红），这类错只有把机检**当真跑一遍**才会暴露。

- **v1.0（2026-10-30）** 详设落盘。§0 十六项改动总表（含批次归属）· §1 逐文件完整代码（`Widget.h` 五处
  注释逐字、`Widget.cpp` 四新函数 + 六改写 + 两插入、`Window` 两处、`Application` 整删、`ScrollView` 两处、
  `TextWidget`/`TextBox` 三处收敛、两个纯注释文件）· §2 十条不变量与可执行机检（★ I41-1 故意保留
  「先算后核」痕迹 + I41-9 的「判据对象是注释、不得排除注释」口径）· §3 测试基建（`WindowProbe.h` 全文 +
  `CountingLayout` + `FindFirstBar`）与十条用例（含 T41-7 的四段判据、T41-6 的「恰 1 次」判据与
  「若为 2 怎么办」的处置顺序）· §4 三批（含语法预检命令与「批 1 不可拆」的论证）· §5 影响面 ·
  §6 十条风险 · §7 文档同步 · §8 空回填表。
- ~~★ **本稿最需要审核盯防的三点**~~ → **v1.1 已由 Gemini 4 Argon 逐条对码裁决：三项全部成立、零隐患**
  （① 出口末尾用 `Invalidate()` 与内联 `root->m_window->Invalidate()` 在**全部拓扑**（离线节点 / 离线子树 /
  窗口根 / 树内节点）下逐位等价；② 同线程 `SetWindowPos` 恰 1 条 `WM_SIZE`；③ `m_textRevision` 的
  10 处编辑路径各自独立 bump，删 override 反而消除了冗余自增）。
- ★ **v1.1 之后请继续盯防的两点**：① §2 的**两条 grep 口径**是否在施工时被混用（P1-1a 的根因就是
  「新注释拼了靶词」，而这类错只有把机检**真跑一遍**才会暴露——建议批 1 结束后**真的跑** I41-1/6/9 三条，
  不要只在纸上核对）；② §3.2 T41-3 给根挂 `Layout` 之后，Label 是否会被 `VerticalLayout` 的
  `fillCrossAxis`/`SetPosition` 副作用影响到断言前置（本稿取 `fillCrossAxis = false` 的默认构造，理论上
  只写 `SetPosition(0,0)`，但施工时若 `label->SetSize(120,24)` 被后续 Arrange 覆盖，需按实况调整顺序）。
- ★★ **施工期发现并当场订正的一处「传播中的陈旧锚点」**：`SetVisible` 原注释（`Widget.cpp:440`）引用
  「`Widget.cpp:135`/`:281` 的 Paint 前置」——**这两个行号早已失效**（实测 `:135` 落在 `RemoveChild`
  的 `it == end()` 分支内、`:281` 是空行）。真实位置 = **`:489`**（`Paint` 的 `if (!IsVisible()) return;`）
  与 **`:314`**（`HitTest` 同款门控）。本稿与初设 v1.2 已把两处新表述的锚点改正。
  ★ 结论**不受影响**（`IsVisible()` 确实是 Paint 与 HitTest 的双门控），错的是**引用**——
  而这正是「把旧注释原样抄进新契约」最容易继承的债：**抄之前必须重新定位**。
