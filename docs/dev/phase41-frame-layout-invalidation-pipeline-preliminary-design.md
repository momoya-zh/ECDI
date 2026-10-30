# Phase 41 · 初步设计 —— 帧级布局 / 重绘失效结算管线（#79）

> 状态：**v1.3**（2026-10-30 详设审核 P1-1a 回流：§1.1.2 / §1.1.3 的新表述**不再原样复述被禁靶词**，改「概括旧意 + 指向本节引文」——否则 §1.1.4 的零命中机检会自己命中自己；§1.1.4 补「两条相反纪律」）。v1.2 = 锚点订正（`:135`/`:281` → `:489`/`:314`）。v1.1 = 收 Gemini 4 Argon 审核（**1 P0 + 2 P1 + 4 P2 全部成立、无一条驳回**）。上游 = 需求稿 **v1.2**（D41-1 = ⓑ、D41-11 = 方案 A 已由用户拍板）。
> ★ 本稿的第一交付物是**需求 §7 闸门 1 的三处契约文字逐字新表述**（§1.1）——需求明写「不能再留『由调用方负责』的模糊语义」，故本稿把可粘贴的最终文本直接给出，不留给详设再措辞。
> ★ 全部行号对 HEAD `a375824` + 未提交需求稿亲核；用例基线 **546 / 546 全绿**（2026-10-29 MinGW 实跑，四构建链）。
> ★ 本 Phase 是**契约推翻项**：`Widget.h:147-149` / `:201-203` / `ScrollView.cpp:337` 三条既有文字被改写，且 `Widget.SetSizeStillNoRepaint` **必须翻转**。两条都在需求里显式登记过，本稿只落实施方案。

**红线（继承需求，本稿的每条方案都要对它负责）**

1. **布局结果逐位不变**：`Arrange` 的几何输出与改前完全相同（不可见子停泊坐标、stretch 余数、末位截断）。本 Phase 只换驱动者，不换算法。
2. **无窗口路径零副作用 + 存量测试改动面受控**：**545 条零改动** + **恰 1 条显式翻转**（`Widget.SetSizeStillNoRepaint` → `Widget.SetSizeRequestsRepaint`）。
3. **不引入每帧无界重算**：结算必须**脏触发**，空闲帧 O(1)。

---

## §1 初设必答（需求 §7 闸门 1–10 逐条收口）

### 1.1 闸门 1 · 三处契约文字（★ 本稿核心交付，逐字可粘贴）

需求 §4 D41-11 的最终推论是：**「重绘失效」与「布局失效」是两条通道、两套判据**，契约注释**必须分开写**，不能再用「不 Arrange、不 Invalidate」这种把两件事并列的句式——那正是 `Widget.h:148` 原文的病根。

据此，注释分工定为**四层**（不是三处重复，每层回答不同的问题）：

| 层 | 位置 | 回答的问题 |
|---|---|---|
| **总纲** | `Widget.h` 类头注释（`Widget.h:30-42` 的「职责划分」段之后） | 两条通道的**判据**是什么？谁读什么？谁负责结算？ |
| **特例甲** | `Widget.h:145-150`（`AutoSize`） | 本方法只调 `SetSize` ⇒ 失效**继承而来**，它自己不做任何失效动作 |
| **特例乙** | `Widget.h:201-203`（`SetVisible` 注释 ③） | **旧豁免被撤销**：为什么当年写「不扩大到 SetSize/SetPosition/SetText」，现在为什么不再成立 |
| **特例丙** | `ScrollView.cpp:337` | 这个类**为什么不需要**自己再豁免一次（旧注释看似正确的原因） |

#### 1.1.0 新增 · `Widget.h` 类头「布局 / 重绘失效」总纲

插在 `Widget.h:41`（`/// - Event:   事件响应虚函数（...）` 那一行）之后、`class Widget{` 之前：

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
///
/// ▸ ⚠️ **`SetText` 只走重绘通道**：文本不改变几何。要「文本变长、控件跟着变宽」请显式
///   `AutoSize()`——它是尺寸的唯一动作入口。
/// ▸ ⚠️ **`ScrollView::UpdateContentExtent` 仍是显式的**：extent 是**内容语义**（不是几何失效），
///   框架不代挂钩（D6）。
```

#### 1.1.1 `Widget.h:145-150`（`AutoSize`）—— 逐字新表述

**原文**

```cpp
	/// @brief 按 GetPreferredSize() 调整自身尺寸（9.8 显式命令——后调用者赢，需求 R5 v1.5）
	/// @return true = 实际调用了 SetSize；false = stretch 互斥 no-op 或尺寸未变化
	/// @note §3.7 纯 geometry operation：内部只调 SetSize（虚分派——TextBox override 既有语义）；
	///       不 Arrange、不 Invalidate、不挂钩 SetText——布局/重绘由调用方负责。
	///       float → int 向零截断（v1 不引入 rounding policy）
	bool AutoSize();
```

**新文**

```cpp
	/// @brief 按 GetPreferredSize() 调整自身尺寸（9.8 显式命令——后调用者赢，需求 R5 v1.5）
	/// @return true = 实际调用了 SetSize；false = stretch 互斥 no-op 或尺寸未变化
	/// @note §3.7 纯 geometry operation：内部**只调 SetSize**（虚分派——TextBox override 既有语义）；
	///       float → int 向零截断（v1 不引入 rounding policy）
	/// @note ★★ **Phase 41 契约变更**（旧契约「由调用方自行排布与刷新」已推翻——★ 原句**故意不复述**：
	///       §1.1.4 的机检要求靶词清零且**不得排除注释**，把靶词原样拼进新注释会让判据自己命中自己；
	///       原文全句见**本节上方「原文」引文块**，`docs/` 不受该机检约束）：
	///       本方法**自身**仍不 Arrange、不 `Invalidate`——但 `SetSize` 现在是**失效源**
	///       （重绘 + 布局双通道，见 `SetSize` 与类头「布局 / 重绘失效」）。
	///       ⇒ 尺寸真变化后，**下一帧首**自动完成重排与重绘，调用方**无需**再补
	///       `Arrange()` / `Invalidate()`；尺寸未变化时两条通道都不动（同值守卫）。
	///       ★ 仍**不挂钩 `SetText`**：文本变长不会自动触发 AutoSize——那是**内容语义**，
	///       不是几何失效（框架不替调用方决定「文本该不该撑开控件」）。
	bool AutoSize();
```

★ **判据措辞的要点**：「只调 SetSize」这半句**保留原样**（它仍然成立，是 D41-9 的结论）；被推翻的只是「因此不失效」这个由它推出的结论。

#### 1.1.2 `Widget.h:201-203`（`SetVisible` 注释 ③）—— 逐字新表述

**原文**

```cpp
	///   ③ **不扩大到 `SetSize` / `SetPosition` / `SetText`**——那些是**另一类契约**：
	///      几何 setter 由 `Layout` 逐帧批量调用（加了会每帧重绘）；`SetText` 是**有意契约**
	///      （`Widget.h` 明写「布局/重绘由调用方负责」）。M9 只管**可见性**这一条。
```

**新文**

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
	///      也是**布局输入**（`Layout` 对不可见子跳过槽位并停泊到 bbox 负区——C-VIS-2/4）
	///      ⇒ `SetVisible` 现在是 §1.3 条① 的修复点。
```

#### 1.1.3 `ScrollView.cpp:337` —— 逐字新表述

**原文**

```cpp
	// 不 Invalidate：SetSize 由布局路径调用，重绘由调用方负责（既有契约）
```

**新文**

```cpp
	// ★★ Phase 41 契约变更（旧契约「`SetSize` 不发重绘、刷新由调用方负责」已推翻——★ 靶词同样**不复述**，
	//   理由同上（§1.1.4 的零命中机检不得排除注释）；原文全句见本节上方「原文」引文块）：
	//   重绘失效由 `Widget::SetSize` 统一发出（值真变化才发，同值守卫在基类），本 override
	//   只负责三段**复合**副作用——`ApplyLayout` / `ClampOffset` / `SyncBars` 改的是
	//   **滚动条与内容节点**的几何/可见性，不是 `ScrollView` 自身的视口几何。
	//   ⚠️ **旧注释当时为什么看起来是对的**：`SetSize` 确实由 `Layout` 逐帧调用；但
	//   ① 「逐帧重绘」的代价在当前平台层**已经付过了**（无 `BeginPaint` ⇒ region 恒脏）；
	//   ② 结算期（`Layout` 正在写几何）的豁免**不再由本类各自判断**，而由
	//      `Widget::NotifyGeometryChanged` 在 `Arrange` 期间**统一拦下**（既不置脏也不 Invalidate）；
	//   ⇒ 本类没有任何理由再单独拒发一次重绘请求。
```

#### 1.1.4 三处改写的自检（机检口径，供详设复跑）

| 机检 | 命令 | 期望 |
|---|---|---|
| 旧句式清零 | `grep -rn "不 Arrange、不 Invalidate\|布局/重绘由调用方负责" ECDI/` | **0 命中** |
| 旧注释清零 | `grep -rn "SetSize 由布局路径调用，重绘由调用方负责" ECDI/` | **0 命中**（仅 v1.0 需求稿的引文可留，且位于 `docs/`） |
| 新总纲存在 | `grep -n "两条通道，判据不同" ECDI/include/ECDI/Widget/Widget.h` | **1 命中** |
★ **陷阱**：上述命令**不得**排除注释行 | 本 Phase 的判据对象**就是注释**——与 Phase 40 的「grep 要排除注释」**方向相反**，须逐条确认 |
| ★ **连带纪律（v1.3 补）** | 新注释**不得原样拼出**上表两个靶词 | 否则「把改动写好（加说明注释）」本身让判据变红。§1.1.2 / §1.1.3 的新表述已改「概括旧意 + 指向本节原文引文」——**引文只在 `docs/` 里**，`ECDI/` 内零命中才成立 |
| ★ **反向纪律（同一张表的两面）** | 判据对象是**代码**时（如「置真只有一个写入点」）**必须**排除注释 | `grep ... \| grep -v ':[0-9]*:[[:space:]]*//'`——Phase 40 的教训 |

---

### 1.2 闸门 2 · D41-11 方案 A 的配套翻转（用例逐字）

`VisibilityRepaintTests.cpp:187-213` 的 `TestM9SetSizeStillNoRepaint` 整体替换为下列内容；
`:286` 的注册名同步改。**断言拆两条**（重绘通道 + 门控通道），后者同时是 D41-4 的判据。

```cpp
// ── M9-③ → ★ Phase 41：几何 setter **请求重绘**（契约变更后的正向守卫）────────────
//   ★ 本条是 `Widget.SetSizeStillNoRepaint` 的**翻转**（需求 D41-11 方案 A 的配套动作）。
//     旧条自述「钉住范围边界……若将来有人『顺手统一』，本条会红——那就是范围蔓延的信号」
//     ⇒ Phase 41 **正是**要改变那个意图，翻转是**契约变更的配套**，不是掩盖回归。
//   ★ 新旧判据的差别（为什么拆两条）：
//     · **重绘通道**：值真变化 ⇒ `Invalidate()`（旧契约说「不发」，新契约说「发」）；
//     · **布局通道**：`Button` 与 `RootWidget` **都没有 `Layout`** ⇒ 门控（D41-4）拦下 ⇒ **不置脏**。
//       这一条在两侧都是「不变」——它守的**不是**旧契约，而是**新门控的边界**。
void TestPhase41SetSizeRequestsRepaint() {

	M9Probe probe("ECDI_P41_SetSize");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_SetSize");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ClearRegion();
	button->SetSize(120, 40);
	EXPECT_TRUE(probe.RegionDirty());                              // ★ 重绘通道：必须请求重绘
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());     // ★ 门控通道：无 Layout 读它 ⇒ 不置脏

	probe.ClearRegion();
	button->SetPosition(30, 30);
	EXPECT_TRUE(probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

	// ★ 同值守卫（R6 / 需求 D41-3 末段）：值没变 ⇒ **两条通道都不动**
	probe.ClearRegion();
	button->SetSize(120, 40);
	EXPECT_TRUE(!probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}
```

★ **`ShowAndSettle()` 之后 `NeedsLayout()` 必为假**——泵消息会跑到 `PaintFrame`，帧首结算把根脏清掉。这条前置不成立就说明结算没跑，任何后续断言都无意义（`EXPECT_TRUE(!...NeedsLayout())` 在这里同时充当前置校验）。

★ **重命名后旧名不再存在于注册表** ⇒ 详设须同时更新 `§6 影响面`/`§8 修订记录` 两处登记（需求 D41-11 ③④ 已要求）。

---

### 1.3 闸门 3 · 置脏清单、门控与落点（含 `SetPosition` 移出 inline 的核算）

#### 1.3.1 统一出口：`NotifyGeometryChanged(bool layoutInputChanged)`

所有 setter 的失效动作**只有这一个出口**（单一真相源；「不引入第二个构造表达式」与 `Paint` 的 Phase 27 D27-B 同族纪律）。三条语义按顺序：

1. **结算期抑制**：`root->m_arrangeDepth > 0` ⇒ **直接 return**（既不置脏、也不 `Invalidate`）；
2. **布局通道**：`layoutInputChanged` ⇒ `root->m_layoutDirty = true`；
3. **重绘通道**：`Invalidate()`（无窗口自然 no-op，`Widget.cpp:585-593` 既有语义）。

★ 门控（第 2 步的 `layoutInputChanged` 实参）由**调用方**算好传入——因为它逐 setter 不同，且是纯表达式（表见 1.3.2）。为什么不让出口自己算：出口不知道「这次改的是哪个字段」。

#### 1.3.2 逐 setter 的门控表（实现口径，与需求 D41-4 表一一对应）

`ParentHasLayout()` = `m_parent != nullptr && m_parent->m_layout != nullptr`（私有无副作用查询，1.3.3）。

| setter / 方法 | 同值守卫 | 布局通道（实参） | 重绘通道 | 备注 |
|---|---|---|---|---|
| `SetSize` | ★ 加（width+height 同时相等） | `m_layout != nullptr \|\| ParentHasLayout()` | 走 | 尺寸有两个读者 |
| `SetPosition` | ★ 加（x+y 同时相等） | `ParentHasLayout()` | 走 | 位置的唯一读者是父 Layout；★ 需移出 inline |
| `SetVisible` | 已有（`Widget.cpp:434`） | `ParentHasLayout()` | 走 | 取代原 `:444` 的裸 `Invalidate()` |
| `SetStretch` | ★ 加（钳 0 之后比较） | `ParentHasLayout()` | 走 | 钳 0 语义不变 |
| `AddChild` | — | `subtreeDirty \|\| m_layout != nullptr`（★ 作为**实参**传入，**不套 `if`**） | 走（**无条件**） | 见 1.3.4；★ 门控只在「置脏」那条分支上生效，**不得**连重绘一起跳过（v1.1 订正） |
| `RemoveChild` | —（未命中已早退） | `m_layout != nullptr`（同上，作为实参） | 走（**无条件**） | 同上——从**无** `Layout` 的容器摘子也必须重绘 |
| `SetLayout` | — | `true`（无条件） | 走 | 换布局器 ⇒ 我的子树必须重算 |
| `TextWidget::SetText` | ★ **不加**（既有语义：同文本也 bump revision） | **不置脏**（`false` → 改为只 `Invalidate()`） | 走 | D41-5 严格判据，见 §1.4 |
| `SetStyle` / `ApplyTheme` / `SetWordWrap` | 既有 | 不置脏（已内建 `Invalidate`） | 走 | 需经 `AutoSize` 才落到几何 |

★ **为什么 `SetText` 不调用 `NotifyGeometryChanged`**：出口的后两步对它是「不置脏 + `Invalidate`」，只用到第 3 步 ⇒ 直接调 `Invalidate()` 更直白，且避免「传 `false` 却仍走一遍根上溯」的语义噪声。**这也让 1.3.1 的出口保持「几何类 setter 专用」的窄语义**（命名即语义）。

#### 1.3.3 新增的私有查询与成员

```cpp
	// ── 布局失效结算（★ Phase 41 · #79）────────────────────────────────
	//    存放形态 = **根单标志**（D41-2 ⓐ）：置位时沿父链上溯写根，消费只在根读，
	//    与 `Invalidate()` 的「上溯取 Window」同构。⇒ **非根节点上的 `m_layoutDirty`
	//    只在「离线子树挂入」那一次被读**（`AddChild` 合并），其余时刻无意义。

	/// @brief 沿父链上溯到根（无父即自身）
	[[nodiscard]] Widget* GetRootNode() noexcept;

	/// @brief 父节点是否有 `Layout`（位置 / 可见性 / stretch 的唯一读者）——门控表达式的单一来源
	[[nodiscard]] bool ParentHasLayout() const noexcept{
		return m_parent != nullptr && m_parent->m_layout != nullptr;
	}

	/// @brief 几何类 setter 的**唯一**失效出口（三条语义见 `Widget.cpp` 实现处注释）
	/// @param layoutInputChanged 本次写入是否是「某个 Layout 会读的输入」（门控由调用方算好）
	void NotifyGeometryChanged(bool layoutInputChanged) noexcept;

	/// @brief 帧首布局结算（**仅根有意义**；由 `Window::PaintFrame` 经 friend 调用）
	void SettleLayoutIfDirty();

	bool m_layoutDirty = false;   ///< ★ 根的待结算布局失效（D41-2 ⓐ；非根只在挂入瞬间被读）
	int  m_arrangeDepth = 0;      ///< ★ 结算期嵌套深度（D41-3 ⓑ 抑制；用计数同 `m_dispatchRefCount`）
```

★ **`ParentHasLayout()` 内联在头里而不是移进 `.cpp`**：它是**零成本**的三目（一次指针比较），
且被 4 个 setter 复用——若移出会变成 4 次跨 TU 调用；而 `SetPosition` 移出的理由（需要副作用）它没有。

#### 1.3.4 `AddChild` 的两条实现要求（D41-6 落地）

```cpp
	// ★★ Phase 41（D41-6 实现要求 ②）：**子树脏向新根合并**。
	//   业务侧的真实建树路径是「先离线搭子树、再挂进窗口」——离线期 `AddChild`/`SetLayout`
	//   把脏写在该子树的**临时根**上；根单标志只看真根 ⇒ 不合并就是**挂入后首帧不排**
	//   （几何停在初始值）。★ 无法靠 `SetWindow` 兜底：它是 private、唯一调用点是
	//   `Window` 构造第 113 行（对刚 new 出来的空 root），不是业务路径。
	Widget* const raw = child.get();
	const bool subtreeDirty = raw->m_layoutDirty;
	raw->m_layoutDirty = false;        // 已由真根承载；临时根不再被查询（防「二次挂入」伪脏）

	raw->m_parent = this;

	m_children.emplace_back(std::move(child));

	// ★★ 失效出口**无条件调用**，门控只作为**实参**（v1.1 订正——v1.0 曾把整句套进 `if`，
	//   那会让「往无 Layout 的父节点增删子控件」连**重绘通道**都被跳过。详见下方 ⚠️）：
	//   ① 子树脏：无条件合并（它代表一条**真实**失效，不需要再过门控）
	//   ② 自身置脏：子集合的唯一读者是**我的** Layout
	NotifyGeometryChanged(subtreeDirty || m_layout != nullptr);
```

★ **顺序不可换**：`raw->m_layoutDirty` 必须在 `emplace_back(std::move(child))` **之前**取（
move 之后 `child` 为空指针），且 `m_parent` 也必须在 `emplace_back` 之前写（既有顺序，不改）。

★★ **v1.1 订正（P0-1，自证成立）——为什么「套 `if`」是错的**：

v1.0 写的是 `if (subtreeDirty || m_layout != nullptr){ NotifyGeometryChanged(true); }`。
它把**两条通道一起**挡在门控后面，而门控只该管**布局通道**。真实击穿路径（业务侧最自然的写法）：

```cpp
auto button = std::make_unique<Button>();
button->SetSize(80, 30);          // ★ 此刻 button 无父、无 Layout ⇒ 门控 false ⇒ button 的脏为 false
rootWidget.AddChild(std::move(button));   // ★ RootWidget 无 Layout ⇒ 整个 if 不进入
                                          //    ⇒ **连 Invalidate() 都没发** ⇒ 新控件要等下一次偶然重绘才出现
```

⇒ 症状 = 「动态往没有 `Layout` 的容器里加控件，界面不刷新」——比 M9 更难查（M9 至少是「状态 setter」，
这次是**树结构操作**）。`RemoveChild` 同病（从无 `Layout` 容器摘子不重绘）。

★ **正确形态的三条理由**：
1. `NotifyGeometryChanged(bool)` 的签名**本来就是为这件事设计的**——门控是**实参**，不是调用方的 `if`；
   这正是「统一出口」的价值：**重绘通道对几何类 setter 恒发**，只有置脏受门控。
2. 与 §1.3.2 表格的自洽性：v1.0 表格里 `AddChild`/`RemoveChild` 的「重绘通道」列写的就是「**走**」——
   v1.0 的代码草案与它自己的表格**互相矛盾**，是审计抓出来的硬错。
3. C41-4 仍成立（`m_layoutDirty = true` 的唯一写入点仍只有出口内部一处）。

★ **这是一条行为新增，须记账**：HEAD 的 `AddChild`/`RemoveChild` **都不** `Invalidate`（`Widget.cpp:48-102`
/ `:104-161` 通读无 `Invalidate` 调用）。Phase 41 起两者**恒发重绘**——这是「子集合变 ⇒ 视觉变」的
正确语义，也是 `VisualTest` 一类「运行时加控件」场景本来就在等的东西。

★ **为什么 `subtreeDirty` 一层就够、不需要 `HasDirtyLayoutInSubtree()` 整树遍历**（需求曾留作可选）：
根单标志的**不变量**是「任一次置脏都会上溯做到**当时的根**」⇒ 子树在离线期产生的脏**必然**落在子树根上。
⇒ 子树根标志为真 ⟺ 子树内自上次结算以来有过失效。**代价从 O(subtree) 降到 O(1)**。

★ **`RemoveChild` 同式**：`NotifyGeometryChanged(m_layout != nullptr);` ——门控只说「**我的** Layout 会读子集合」，
而重绘无条件发（被摘子树自己的脏**不上交**——它要离开了）。
★ **插入点**：紧接 `m_children.erase(it)`（`Widget.cpp:144`）之后。★ 早一处/晚一处都**等价**——
出口里的 `Invalidate()` 是从 **`this`** 上溯取 Window，与被摘节点的 `m_parent`（`:165` 才清）无关；
放在 erase 之后只是让「子集合刚变」这个因果在代码里读得出来。★ 此时 `result` 仍持有所有权、
`this` 的父链完好 ⇒ 出口的两次上溯都安全。

#### 1.3.5 `SetPosition` 移出 inline 的影响核算（需求闸门 3 点名）

| 项 | 核算 |
|---|---|
| API 形态 | 签名 `void SetPosition(int x, int y)` **不变** ⇒ 调用点零改动（`Layout` 4 处、`CaptionBar`/`ScrollView`/`TextBox`/`CollapsiblePanel` 若干） |
| ABI | 类布局不变、vtable 不变（非虚）；但函数从「头内联」变为「库内符号」⇒ 静态库与调用方须同时重编 |
| 编译批次 | `Widget.h` 本 Phase **必然**要改（新增成员 + 声明）⇒ **这次全量重编已经注定**，移出 inline **不额外增加**批次 |
| 运行期 | `Layout` 内每 child 一次函数调用（原为内联写两个 float）——与 `SetSize` 已是虚函数调用同量级；对 `ListLayout` 20k 行 ≈ 20k 次调用，相对 `Arrange` 的其余部分可忽略 |
| 替代方案（已否决） | 在头里内联实现守卫 + 通知：会让 `Widget.h` 携带业务逻辑、且 `NotifyGeometryChanged` 的声明必须前置 —— 与 `SetSize` 的既定形态（`Widget.h:123` 注释明写「移 Widget.cpp」）不一致 |

---

### 1.4 闸门 4 · `SetText` 的双通道裁决（★ 严格判据）

**裁决：`SetText` 只走重绘通道，不置布局脏。**

三条理由（第一条是决定性的）：

1. 「文本变长 ⇒ 容器跟着变宽」**不是** `Arrange` 能做的事——`Arrange` 用的还是旧几何，重排一次**结果不变**（纯浪费）。那件事只能由 `AutoSize()` 完成（内容 → 尺寸），而 `AutoSize` 已经因为「内部调 `SetSize`」自动继承了布局通道 ⇒ **链路闭合，不需要 `SetText` 置脏**。
2. 「框架是否该替调用方决定文本撑开控件」是**内容语义**，`AutoSize` 的 `stretch` 互斥与「后调用者赢」正是把该决定**显式化**的既有设计（9.8 R5 v1.5）。`SetText` 自动置布局脏会**绕过**这套闸门（置脏 ≠ 尺寸变化，但会让使用者误以为「文本会撑开」）。
3. 需求 §2 S7 记录的现状是「`SetText` 只 `MarkTextChanged`（失效 preferred 指纹）」——它**已经**正确地为 `AutoSize` 备好了输入；缺的只是重绘。补 `Invalidate` 是条 65 的修复本体。

#### 1.4.1 落点与连带收敛（需求 R4 的两条）

| 文件 | 现状 | Phase 41 |
|---|---|---|
| `TextWidget.cpp:37-51`（两个重载） | `m_text = ...; MarkTextChanged();` | 末尾补 `Invalidate();`（**两个重载都要**） |
| `TextBox.cpp:124-132` / `:134-142` | `TextWidget::SetText(...)` → **重复** `MarkTextChanged()` → `Invalidate()` | ★ **两个 override 整体删除** |
| ★ `TextBox.h:97-104`（v1.1 补，P1-1） | `void SetText(const std::string&) override;` / `void SetText(std::string&&) override;` + 6 行 `@details` | ★ **声明 + 注释块一并删除**——只删 `.cpp` 会 `unresolved external symbol`（override 声明的实现缺失） |

★ **为何删除而不是留空转发**：基类补了 `Invalidate` 之后，`TextBox::SetText` 的三行**全部**成为基类的子集（`MarkTextChanged` 基类已调、`Invalidate` 基类已做）⇒ 保留即死代码。
★ **行为等价性**：`TextWidget::SetText` 是 `virtual`（`TextWidget.h:35/37`）⇒ 删掉 `TextBox` 的 override 后 vtable 槽位指向基类实现，**行为逐位相同**（`TextBox` 的 C28-5 编辑/组合/Undo 路径**不经** `SetText`，它们各自调 `MarkTextChanged`，不受影响）。
★ **`TextBox.h:97-101` 那段注释无须迁走，且它已陈旧**：文中「失效方式 = 清 `m_linesValid`（`MarkLinesDirty()`）」——
`grep -rn "MarkLinesDirty" ECDI/` ⇒ **全库只有这一处注释**（函数不存在）；Phase 38 之后的真相是
「键守卫 + `m_linesValid` 首帧必建」（`TextBox.cpp:454`/`:504`）。而它记的**真契约**——
「`SetText` 不触发 `RaiseTextChanged`/callback（D7）」——**已在 `TextBox.h:110-116`（`SetOnTextChanged`）** 独立记录。
⇒ 删掉这段注释不损失任何活契约，反而清掉一处幽灵 API 引用。
★ **风险**：若将来 `TextBox` 需要 `SetText` 的类内副作用，届时再加 override——这是「第二次用例出现再抽象」的正面用法。

#### 1.4.2 `TextBox::SetSize` 末尾的冗余 `Invalidate()`

`TextBox.cpp:269` 的 `Invalidate()` 在基类补发之后**冗余**。★ 但它不只是冗余——**它违反结算期抑制**：

```
父 Layout → textBox->SetSize(w,h)            [Arrange 期间，m_arrangeDepth == 1]
  → Widget::SetSize           → NotifyGeometryChanged → 抑制（不脏、不 Invalidate）✓
  → EnsureCaretVisible / LayoutScrollBar / SyncScrollBar  → 内部的 SetVisible/SetPosition
                                                            → 同样被抑制 ✓
  → TextBox 自己的 Invalidate()               ← ★ 逃过抑制，每帧每个 TextBox 发一次 InvalidateRect
```

⇒ **删除**（`R4` 的「收敛冗余 `Invalidate`」在结算期语义下是**必要条件**，不是洁癖）。

---

### 1.5 闸门 5 · 可测性（★ 新增测试基础设施）

#### 1.5.1 观测面缺口（需求 §5 已核）

| 验收 | 需要观测什么 | 现有设施 |
|---|---|---|
| A41-4 / A41-6 / A41-8 / A41-9 | 「`Arrange` 发生了几次」 | **无**（`grep "class .*: public Layout" src/Tests/` 零命中） |
| A41-1 翻转用例 | 「布局脏仍为假」 | **无**（没有公开查询） |
| A41-3 / A41-5 / A41-10 | 真窗口 + 泵消息 + region 脏位 | 有（`M9Probe`，`VisibilityRepaintTests.cpp:67-114`，**anonymous namespace 内部**） |

#### 1.5.2 新增公共查询 `NeedsLayout()`（public API **+1**）

**为什么必须公开**：D41-11 的翻转用例要断言「布局通道**没有**被触发」；A41-6/A41-9 要区分
「结算跑了但没重排」与「根本没人置脏」。没有这个查询，两条判据都只能间接推断 ⇒ 假绿风险。

```cpp
	/// @brief **整棵所属树**是否有待结算的布局失效（`PaintFrame` 帧首会消费它）
	/// @details ★ 语义是**整树**、不是「本节点」：脏标志寄存在**根**（D41-2 ⓐ），
	///          本方法沿父链上溯到根再读——与 `HasFocus()`（`Widget.cpp:595-607`）同型。
	///          ⇒ 非根节点返回 `true` 意味着「**同树别的分支**（或本分支）有失效待结算」。
	/// @note 主要消费者 = 测试与诊断；框架自身用 `SettleLayoutIfDirty`（私有）。
	[[nodiscard]] bool NeedsLayout() const noexcept;
```

★ **不新增 `InvalidateLayout()`**（需求 §6 曾预估 public API +1 或 +2）。理由：**它没有消费者**——
「布局失效」的全部输入（尺寸/位置/可见性/stretch/子集合/换布局器）都已由 setter 覆盖；
绕开 setter 直接改输入在框架里**不存在路径**。显式重排仍可用公开的 `Arrange()`。
⇒ 按「第二个真实消费者出现才加」的既有纪律，**不加**。

#### 1.5.3 `CountingLayout`（装饰器，需求 §5 已给草案）

采用需求草案，落 `src/Tests/LayoutInvalidationTests.cpp` 的 anonymous namespace：

```cpp
/// @brief 计数装饰器：转发给真 Layout —— **只加计数、不改行为**
/// @details ★ 为什么不是「重写一个假 Layout（只 SetPosition）」：那会**改变布局结果**，
///          让 A41-2「逐位不变」这条红线失去判据（Phase 40 I-3「量具必须与被测行为同构」同族）。
class CountingLayout : public Layout{

public:

	explicit CountingLayout(std::unique_ptr<Layout> inner): m_inner(std::move(inner)){}

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

#### 1.5.4 `WindowProbe` 抽出（D41-H，第二消费者已出现）

`M9Probe`（`VisibilityRepaintTests.cpp:67-114`）与新增的帧级用例需要**同一套**「真 Application + 真
Window + `FindWindowW` + 泵消息 + `ValidateRect`/`GetUpdateRect`」。按本项目「第二个真实消费者出现
再抽象」的纪律，**此刻**抽取：

| 动作 | 内容 |
|---|---|
| 新增 | `src/Tests/WindowProbe.h`（内部头，**不计入公共头 100**）：`struct WindowProbe` = 现 `M9Probe` 原样搬入 + 新增 `PumpFrames(int n)`（语义等同现 `PumpMessages`） |
| 改 | `VisibilityRepaintTests.cpp`：删 anonymous namespace 里的 `PumpMessages` + `M9Probe`，改 `#include "WindowProbe.h"`，`M9Probe` → `WindowProbe`（行为零变化，5 条用例判据不变） |
| 新增 | `src/Tests/LayoutInvalidationTests.cpp`：10 条新用例 + `CountingLayout` |

★ 该头**不装** `windows.h` 之外的东西；`#ifdef DrawText` 防御性 undef（规范 10）随 `windows.h` 一并搬入。

---

### 1.6 闸门 6 · 清脏作用域与 `m_arrangeDepth` 的形态

#### 1.6.1 清脏作用域（D41-3.2 落地）

```cpp
void Widget::Arrange(){

	Widget* const root = GetRootNode();

	++root->m_arrangeDepth;      // ★ 结算期抑制对**整棵树**生效（标志寄存在根）

	ArrangeInternal();

	--root->m_arrangeDepth;

	// ★ D41-3.2：**根** Arrange 完成即清根脏；**子树** Arrange **不清**（兄弟分支可能仍脏）
	//   ⇒ 这是 A41-8「一次 WM_SIZE 只 Arrange 一次」的唯一成立条件：
	//     `OnResized` 先同步 Arrange（此处清脏）⇒ 随后的 `PaintFrame` 看到干净 ⇒ 不再重排。
	if (root == this && root->m_arrangeDepth == 0){

		root->m_layoutDirty = false;

	}

}
```

★ `root->m_arrangeDepth == 0` 这一半是**防御性**的：嵌套时（子树 Arrange 套在根 Arrange 里）`root == this` 为假，本来也不清；写出来是为了让「**结算完全退出后才消费脏**」这条语义在代码里可见。

#### 1.6.2 `m_arrangeDepth` 用 **int 计数**，不用 bool（闸门 6 的必答）

**实核结论：仓内没有嵌套 `Arrange` 的调用点。**

| 证据 | 事实 |
|---|---|
| 框架内 `.Arrange()` 调用点 | `grep -rn "\.Arrange()" ECDI/src`（排除 `Tests/`）⇒ **恰 2 处**：`Window.cpp:836`、`Application.cpp:292`（本 Phase 删） |
| `ArrangeInternal` 递归 | 只调 `child->ArrangeInternal()`（`Widget.cpp:409`）——**不重入 `Arrange()`** |
| `Layout::Arrange` 内 | 四个 Layout 只写子几何（S9），不调 `Arrange()` |
| `SetSize` 各 override 内 | `TextBox`（`EnsureCaretVisible`/`LayoutScrollBar`/`SyncScrollBar`）、`ScrollView`（`ApplyLayout`/`ClampOffset`/`SyncBars`）、`CollapsiblePanel`、`CaptionBar`（`RelayoutChildren`）——**无一调 `Arrange()`** |

**但仍选计数**，两条理由：
1. **`Arrange()` 是公开 API**，业务侧可在回调里调子树 `Arrange()`；一旦将来出现「业务回调在某个
   `Layout` 里被触发」的路径（例如新增布局器带回调），bool 会**静默**在内层出口复位，外层剩余写入
   逃过抑制 ⇒ 症状是「偶发多排一帧」，极难定位；计数则天然正确。
2. **代价为零差异**：bool 与 int 都是 4 字节（对齐后），且与 `m_dispatchRefCount`（`Widget.h:408`
   明文写「用计数而非 bool」）**同型同理由** ⇒ 一致性收益。

★ 若详设发现必须用 bool（例如需要节省内存的批量节点场景），须给出「嵌套不可能」的**结构保证**，而非「当前没有」。

#### 1.6.3 出口实现（`Widget.cpp`）

```cpp
Widget* Widget::GetRootNode() noexcept{

	Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root;

}

void Widget::NotifyGeometryChanged(bool layoutInputChanged) noexcept{

	Widget* const root = GetRootNode();

	// ★ D41-3 ⓑ：结算期抑制——`Layout::Arrange` 对子控件批量写几何，那些写入是
	//   **布局的输出**（不是输入）⇒ 不构成新的布局失效；且它们会在**同一帧**被画出来
	//   ⇒ 也不需要重绘请求。★ 两个通道一起抑制（需求 v1.2 新增细化）。
	if (root->m_arrangeDepth > 0){

		return;

	}

	// ★ D41-4 门控：只有「某个 Layout 会读它」才置布局脏（由调用方判定后传入）
	if (layoutInputChanged){

		root->m_layoutDirty = true;

	}

	// ★ 重绘通道：复用既有出口（无窗口 ⇒ 内部上溯取不到 Window ⇒ 自然 no-op）
	Invalidate();

}

void Widget::SettleLayoutIfDirty(){

	if (!m_layoutDirty){

		return;   // 空闲帧 O(1)——红线③

	}

	Arrange();    // 根调用 ⇒ 内部按 D41-3.2 清脏

}

bool Widget::NeedsLayout() const noexcept{

	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_layoutDirty;

}
```

★ `NotifyGeometryChanged` 末尾用 `Invalidate()` 而不是内联 `root->m_window->Invalidate()`：**避免同一表达式的第二个构造点**（Phase 27 D27-B 纪律）；多走一次父链上溯，与门控那次同阶，可忽略。

---

### 1.7 闸门 7 · `CollapsiblePanel` 雷区（**只登记，不在本 Phase 修**）

事实（需求 N1 / Gemini P1-4，已亲核）：

- `CollapsiblePanel::SetSize` 的**收起态**分支（`CollapsiblePanel.cpp:157-172`）把传入的 `(w,h)` 写进
  `m_expandedRect` 作为「展开基准」，然后 `ApplyGeometry(0)` 把呈现收敛到 0。
- `VerticalLayout.cpp:95-97` 的 `fillCrossAxis` 分支调 `child->SetSize(cross, child->GetHeight())`。
- 收起态 `GetHeight() == 0` ⇒ **`m_expandedRect` 的高度被抹成 0** ⇒ 此后永远展不开。

⇒ **本 Phase 的处置 = 在契约注释里写明禁令**，落两处：

| 落点 | 文本 |
|---|---|
| 类头总纲（§1.1.0）末尾 | 追加一行 ⚠️：「`CollapsiblePanel` 在 #80 修好「展开基准 vs `fillCrossAxis`」冲突前，**不得放入 `fillCrossAxis = true` 的 Layout 容器**」 |
| `CollapsiblePanel.cpp:157` 分支注释 | 追加一行：「★ Phase 41：本分支与 `VerticalLayout::fillCrossAxis`（传 `(cross, 0)`）冲突——**当前无实例**（`examples` 里都在手摆容器中），修属 #80」 |

★ **为何不修**：`m_expandedRect` 的基准语义属 #80（动画驱动的连续几何），本 Phase 只交付通道。
修它 = 另一个行为变更，会让本 Phase 的「布局结果逐位不变」红线失去判据。

---

### 1.8 闸门 8 · 文档订正（★ 需授权，本稿不自动执行）

| # | 目标 | 现状（错） | 订正为 |
|---|---|---|---|
| 1 | `roadmap-deferred.md` #79 机理⑥ | 「全库唯一布局驱动点 = `WM_SIZE` 的 `Application::OnWindowResized`」 | **三处**（`Window::OnResized:836`、`Application::OnWindowResized:292`、`examples` 四处手写），且前两者是**同一帧两次**（幂等所以无害） |
| 2 | `phase25-list-container-preliminary-design.md:51` B2 | 「Arrange 触发链 = **每帧**：`Window::OnPaint` → `m_rootWidget->Arrange()`」 | `git log -S` 查证：`PaintFrame` **从未**有过 `Arrange`（全历史唯一出现在 `81037ed` 的 `OnResized`）。★ **Phase 41 落地后这句话才变成真的**——但驱动者是帧首**结算**，不是无条件每帧 |

★ 第 2 条的讽刺意味要写进订正注释：**该文档的错误陈述直接生产了 `ModelProbe.cpp:326-333` 的活体 bug**
（按「每帧 Arrange」写业务代码，`SetVisible` 之后确实「应该」自动重排）。

---

### 1.9 闸门 9 · 与 #80 的接缝（动画期代价，量化）

动画（`AnimationManager` 的 `onValue` → `ApplyGeometry` → `SetSize`/`SetPosition`）发生在**定时器回调**里
⇒ `m_arrangeDepth == 0` ⇒ **每次动画帧都会置脏** ⇒ 下一帧首整树 `Arrange`。

| 项 | 量化 |
|---|---|
| `CollapsiblePanel` 展开动画 | 200ms / 60fps ≈ **12 帧** ⇒ **12 次全树 `Arrange`** |
| 单次全树 `Arrange` 代价 | 与既有 `Window::OnResized` 的同步 `Arrange` **完全同阶**（同一函数、同一棵树）——这不是**新增**成本，而是**换了发生时机**（原本 resize 时发生一次，现在动画期每帧一次） |
| 结论 | **已知且接受**。#80 再优化（脏子树 / 动画期聚合）。本 Phase 只保证：**不脏的帧不排**（A41-6），动画期贵是**输入真的在变** |

★ 若在详设发现「12 次全树 Arrange 在 20k 行容器上不可接受」，**不得**在本 Phase 私自加动画聚合
（那是 #80 的范围）——正确做法是把该容器移出动画路径，或提前启动 #80。

---

### 1.10 闸门 10 · `ScrollView` 的 extent 边界（N6）

`ScrollView.h:41` / `:66` 明写 D6「**显式更新（不自动挂钩）**」。新契约下这条**仍然成立**，但会产生
「几何自动、extent 手动」的**双轨观感** ⇒ 必须在 `ScrollView.h` 契约注释里写清一句（**新增**，不推翻既有）：

```cpp
	/// @note ★ Phase 41：几何**自动**（`SetSize`/`SetPosition` ⇒ 帧首结算）、**extent 手动**
	///       （`UpdateContentExtent`）——两者不是同一件事：extent 是**内容语义**（我的内容有多大），
	///       几何是**结果**（我在父容器里占多少）。`ListLayout`/`GridLayout` 会摆子，但**不会**
	///       替你累加 extent ⇒ 增删子之后仍须显式更新，否则滚动范围失真（`ModelProbe` 的
	///       搜索过滤即此场景，`ModelProbe.cpp:326-333`）。
```

---

## §2 初设级决策 D41-A..H（需求未覆盖或留待初设的四项 + 本稿新增）

| # | 决策 | 定案 | 理由 |
|---|---|---|---|
| **D41-A** | `SetPosition` 移出 inline | ★ **是**（`Widget.h:119` → `Widget.cpp`） | 需要守卫 + 通知；`Widget.h` 本 Phase 必然重编 ⇒ 无额外批次（§1.3.5） |
| **D41-B** | 新增公共查询 | ★ **`NeedsLayout()` 一个**；**不加** `InvalidateLayout()` | 前者是翻转用例与 A41-6/9 的**唯一直接判据**；后者零消费者（§1.5.2） |
| **D41-C** | `m_arrangeDepth` 形态 | ★ **int 计数**（不是 bool） | 仓内无嵌套，但 `Arrange` 是公开 API；与 `m_dispatchRefCount` 同型同理由（§1.6.2） |
| **D41-D** | 测试装置 | ★ 抽出 `src/Tests/WindowProbe.h`（`M9Probe` 搬家 + 5 条存量用例仅改名） | 第二消费者出现 ⇒ 按项目纪律即时抽象；复制 40 行编排到新文件会立刻分叉（§1.5.4） |
| **D41-E** | `TextWidget::SetText` / `TextBox::SetText` | ★ 基类**补 `Invalidate`**；`TextBox` 两个 override **整体删除** | D41-5 严格判据；删除后行为逐位等价（基类是 `virtual`，override 内容是基类子集）（§1.4.1） |
| **D41-F** | `TextBox::SetSize` 末尾 `Invalidate` | ★ **删除** | 结算期抑制的**必要条件**（否则每次 `Arrange` 对每个 TextBox 发一次无意义重绘请求）（§1.4.2） |
| **D41-G** | `Application::OnWindowResized` | ★ **整条删除**（头 + 实现），不保留空 override | 与 `Window::OnResized` 同帧重复（§1.4 需求）；空 override 是死代码。「为何不再这里重排」的说明写进 `Window::OnResized` 注释 |
| **D41-H** | `Window::OnResized` 的三件套 | ★ **保留同步 `Arrange()`**，**删掉显式 `Invalidate()`** | `SetSize` 已内含重绘通道；同步 `Arrange` 是 `Win32PlatformWindow.cpp:940-941` 时序承诺的兑现者（D41-7 ⓐ）；根 `Arrange` 清脏 ⇒ `PaintFrame` 不再重排（A41-8） |

★ 需求已有的 D41-1..11 全部为本稿前提，不重复裁决。

---

## §3 方案 PD41-1..9（逐文件落点 + 代码草案）

### PD41-1 · `ECDI/include/ECDI/Widget/Widget.h`

| 位置 | 动作 |
|---|---|
| `:41` 后 | ★ **新增类头总纲**「布局 / 重绘失效」（§1.1.0，逐字） |
| `:119` | `SetPosition` 改声明（移出 inline）+ 新注释（守卫 + 双通道 + 门控） |
| `:121-124` | `SetSize` 注释追加 Phase 41 段（守卫 + 双通道 + 门控 `m_layout \|\| ParentHasLayout()`） |
| `:133` | `SetStretch` 注释追加「同值守卫 + 门控」 |
| `:145-150` | ★ **`AutoSize` 注释改写**（§1.1.1，逐字） |
| `:201-203` | ★ **`SetVisible` 注释 ③ 改写**（§1.1.2，逐字） |
| `:290-294` 后 | 新增 `NeedsLayout()` 公开查询（§1.5.2） |
| `:327` `SetLayout` | 注释追加「换布局器 ⇒ 无条件置脏」 |
| `:329` `Arrange` | ★ 新增注释（此前**完全无注释**）：帧首结算已接管 / 清脏作用域 / 结算期抑制 |
| `:353` private 区 | 新增 `GetRootNode` / `ParentHasLayout` / `NotifyGeometryChanged` / `SettleLayoutIfDirty` 声明 + `m_layoutDirty` / `m_arrangeDepth` 成员（§1.3.3） |

★ 类布局变化：`+bool` `+int` ⇒ `sizeof(Widget)` 增 8 字节（对齐后）。`Widget` 已禁拷贝/禁移动
（地址稳定是硬不变量）⇒ 无序列化/无跨进程契约 ⇒ **零风险**，但须在详设记账。

### PD41-2 · `ECDI/src/Widget/Widget.cpp`

按 §1.3.4 / §1.6.3 / §1.6.1 落：`GetRootNode`、`NotifyGeometryChanged`、`SettleLayoutIfDirty`、
`NeedsLayout`、`SetPosition`（新）、`SetSize`（加守卫 + 通知）、`SetVisible`（`Invalidate()` → 通知）、
`SetStretch`（加守卫 + 通知）、`AddChild`（合并子树脏）、`RemoveChild`（置脏）、`SetLayout`（置脏）、
`Arrange`（抑制 + 根清脏）。

```cpp
void Widget::SetVisible(bool v){

	if (m_state.visible == v){
		return;                     // 既有同值守卫（M9，正确性要求）——不动
	}

	m_state.visible = v;

	// ★ Phase 41：两条通道
	//   重绘：可见性是**绘制门控**（`Widget.cpp:489` 的 Paint 前置 / `:314` 的 HitTest 门控）⇒ 必发（M9 既有语义）；
	//   布局：**仅当父有 Layout** 才置脏——不可见子被 Layout 跳过槽位并停泊到 bbox 负区（C-VIS-2/4）。
	NotifyGeometryChanged(ParentHasLayout());

}

void Widget::SetSize(int w, int h){

	const float fw = static_cast<float>(w);
	const float fh = static_cast<float>(h);

	// ★ 同值守卫（R6 / D41-3 末段）：**正确性要求，不是优化**——
	//   ① `Layout` 每帧对每个 child 调 SetSize（值多半相同）⇒ 无守卫即「伪变化」把根永久弄脏；
	//   ② 复合控件内部写同值几何（`TextBox::LayoutScrollBar:719`、`ScrollView::ApplyLayout:272-288`）
	//      同样会无谓置脏 ⇒ 下一帧白跑一次全树 Arrange（A41-6 变红）。
	if (m_geometry.width == fw && m_geometry.height == fh){
		return;
	}

	m_geometry.width  = fw;
	m_geometry.height = fh;

	// 门控（D41-4）：尺寸有两个读者——我的 Layout（子要重排）、父的 Layout（我在父的槽位里）
	NotifyGeometryChanged(m_layout != nullptr || ParentHasLayout());

}
```

### PD41-3 · `ECDI/src/Window/Window.cpp`

```cpp
void Window::PaintFrame()
{
	if (m_rootWidget == nullptr){
		return;
	}

	// ★★ Phase 41（R3）：**帧首布局结算**——有界（脏才排），空闲帧 O(1)。
	//   位置必须在 `PaintContext` 构造与 `m_rootWidget->Paint()` **之前**：
	//   本帧要画的几何 = 结算后的几何。
	m_rootWidget->SettleLayoutIfDirty();

	m_commands.clear();
	...
}
```

```cpp
void Window::OnResized(int width, int height){

	if (m_rootWidget){

		// ★ Phase 41：`SetSize` 现在自带「置布局脏 + Invalidate」两条通道 ⇒ 下方**不再**显式
		//   `Invalidate()`（原第三行）。同值 resize 被守卫吃掉，不产生任何失效。
		m_rootWidget->SetSize(width, height);

		// ★ 同步重排（保留）：`Win32PlatformWindow.cpp:940-941` 承诺「resize 事件到达时
		//   RootWidget 已是新尺寸、布局已更新」——消费者在事件回调里读到的是新几何。
		//   本调用在根上 ⇒ 完成即清根脏 ⇒ 随后的 `PaintFrame` **不再重排**（A41-8 的「1 次」）。
		//   ⚠️ 历史：此前 `Application::OnWindowResized` 还会再 Arrange 一次（同帧两次，
		//   幂等所以无害）——Phase 41 已删，本行是**唯一**的 resize 布局驱动点。
		m_rootWidget->Arrange();

	}

}
```

### PD41-4 · `ECDI/include/ECDI/Application/Application.h` + `src/Application/Application.cpp`

删除 `OnWindowResized` 的声明（`Application.h:118-119`）与实现（`Application.cpp:287-293`）。
★ 事件本身**照常派发**（`WindowResizedEvent` 仍冒泡到 Widget 树）——只是 `Application` 不再替它重排。

### PD41-5 · `ECDI/src/Widget/ScrollView.cpp`

- `:337` 注释改写（§1.1.3，逐字）；
- `SetSize` 的其余三段（`ApplyLayout`/`ClampOffset`/`SyncBars`）**不动**——它们改的是条与内容节点，
  且内部 `SetSize`/`SetPosition`/`SetVisible` 会自动走两条通道（`ScrollView` 无 `Layout` ⇒ 只重绘）。

★★ **如实记账（v1.1 补，P2-3）：结算期的 `ScrollView::SetSize` 并非「绝对零 `Invalidate`」**

`SyncBars()` → `m_vBar->SetRange(...)` / `m_hBar->SetRange(...)`，而 `ScrollBar::SetRange`
（`ScrollBar.cpp:25-32`）末尾是**裸 `Invalidate()`**——它不走 `NotifyGeometryChanged`，故**不受结算期抑制**：

| 提问 | 事实 |
|---|---|
| 会破坏正确性吗 | **不会**。裸 `Invalidate()` 只发重绘请求，**不触碰 `m_layoutDirty`** ⇒ A41-6 / A41-8 的 `Arrange` 计数不受影响 |
| 代价 | ≈ 0。`Window::OnResized` 期间本来就要重绘（正好替掉 D41-H 删掉的那句）；`PaintFrame` 期间 `WM_PAINT` 无 `BeginPaint` ⇒ region 恒脏 ⇒ 多一次 `InvalidateRect` 是 no-op |
| 要修吗 | **不修**。给 `ScrollBar::SetRange` 补同值守卫属「重复失效」家族，但它**不在**本 Phase 的契约面上（`SetRange` 不是几何 setter）；记入 `roadmap` 的 #71/#72 快修族更合适 |

★ 记这条的目的是**防止误读**：「结算期抑制」的准确表述是「**几何类 setter 经统一出口的失效被抑制**」，
不是「结算期全库零 `Invalidate`」。详设/后续审计若按后者去 grep，会误判此处为漏网。

### PD41-6 · `ECDI/src/Widget/TextWidget.cpp`

两个 `SetText` 重载末尾各补 `Invalidate();`，注释写明「Phase 41：条 65 修复（重绘通道），
**不置布局脏**——要撑开请显式 `AutoSize()`」。

### PD41-7 · `ECDI/src/Widget/TextBox.cpp`

| 位置 | 动作 |
|---|---|
| ★ `TextBox.h:97-104`（v1.1 补，P1-1） | **删除** `SetText` 两个 override 的声明 + 上方 `@details` 注释块（`:97-104` 共 8 行） |
| `:124-132` / `:134-142` | ★ **删除两个 `SetText` override 实现**（D41-E） |
| `:269` | ★ **删除末尾 `Invalidate()`**（D41-F），注释指向基类 |

★ 三类改动**必须同一次提交**：`.h` 删声明 + `.cpp` 删实现 + `TextWidget.cpp` 补 `Invalidate`——只做前两步
会丢重绘通道（A41-5 红），只做后两步会 `unresolved external symbol`。

### PD41-8 · `ECDI/src/Window/CaptionBar.cpp` / `src/Widget/CollapsiblePanel.cpp`

| 文件 | 动作 |
|---|---|
| `CaptionBar.cpp:78-84` | **零代码改动**（`RelayoutChildren` 写的是无 Layout 的子 ⇒ 门控拦下，无伪置脏）。仅补一行注释说明「本 override 不 `Invalidate`：基类已做」 |
| `CollapsiblePanel.cpp:157` | **零代码改动** + 追加雷区预警注释（§1.7） |

★ 需求 R4 说「四个 `SetSize` override 纳入同一契约」——**纳入的方式是「不需要各自处理」**：
基类已发两条通道，override 只保留各自的**复合副作用**（`TextBox` 的 caret/条、`ScrollView` 的三段、
`CaptionBar` 的子控件摆位、`CollapsiblePanel` 的基准语义）。这比「每个 override 自己补 `Invalidate`」
更强：**新写 override 的人默认就是对的**。

### PD41-9 · `src/Tests/*`

| 文件 | 动作 |
|---|---|
| `WindowProbe.h`（**新增**） | `M9Probe` 搬家 + `PumpFrames`；含 `windows.h` 与 `#ifdef DrawText` undef |
| `VisibilityRepaintTests.cpp`（改） | 用 `WindowProbe`；`:187-213` 用例**整体翻转**为 `TestPhase41SetSizeRequestsRepaint`；`:286` 注册名改 |
| `LayoutInvalidationTests.cpp`（**新增**） | `CountingLayout` + T41-1..10；`RunAllTests.h` 声明 + `RunAllTests.cpp` 调用（**手工两处**） |

---

## §4 测试 T41-1..10（546 → **556**）

| # | 注册名 | 覆盖验收 | 装置 | 判据 |
|---|---|---|---|---|
| **T41-1** | `Layout.SetVisibleSchedulesArrange` | A41-3 | 真窗口 + `Panel(VerticalLayout)` + 3 `Button` | 隐藏中间子 → 泵 1 帧 → 第三个子 `GetY() == 0 + h1`（前移） |
| **T41-2** | `Layout.SetSizeSchedulesArrange` | A41-4 | 真窗口 + `Panel(HorizontalLayout)` + 2 固定宽 `Button` | `b1->SetSize(80,20)` → 泵 1 帧 → `b2->GetX() == 80` |
| **T41-3** | `Text.SetTextRequestsRepaint` | A41-5 | 真窗口 + `Label`（`WindowProbe` 量具） | `ClearRegion` → `SetText` → `RegionDirty()` **为真**；且 `NeedsLayout()` **为假**（布局通道不参与） |
| **T41-4** | `Layout.IdleFramesDoNotArrange` | A41-6 | 真窗口 + `CountingLayout(VerticalLayout)` | 泵 3 轮空消息 ⇒ `Calls()` **增量 0** |
| **T41-5** | `Widget.HeadlessSetDoesNotArrange` | A41-7 | **无窗口**：`Panel` + `VerticalLayout` + 2 子 | `SetSize`/`SetVisible` 不崩；`NeedsLayout()` **为真**；`Calls()` **为 0**（无帧首 ⇒ 无结算） |
| **T41-6** | `Window.ResizeArrangesOnce` | A41-8 | 真窗口 + `CountingLayout`；`::SetWindowPos(hwnd,...,500,320,...)` | 泵完 ⇒ `Calls()` 增量 **恰 1** |
| **T41-7** | `Layout.PaintTimeBarToggleDoesNotArrange` | A41-9 + D41-4 门控 | 真窗口 + `Panel(CountingLayout(VerticalLayout))` + **多行 `TextBox`**（含多个 `\n` 且**高度刻意压小** ⇒ `GetMaxScrollOffset() > 0`） | ★ 三段判据（**前置校验 + 状态翻转校准 + 计数**），见下方专段——v1.1 补 P1-2 |
| **T41-8** | `Widget.OfflineSubtreeArrangesOnAttach` | A41-10 | 真窗口（根无 Layout）+ 离线建 `Panel(VerticalLayout)`+2 子 → `AddChild` | 泵 1 帧 ⇒ 两子 `GetY()` 为 0 与 h0（挂入前均为 0） |
| **T41-9** | `Layout.SubtreeArrangeKeepsRootDirty` | D41-3.2 | **无窗口**：根 `Panel` + 子 `Panel(VerticalLayout)` | `child->SetSize(...)` ⇒ `NeedsLayout()` 真；`child->Arrange()`（子树）⇒ 仍**真**；`root.Arrange()` ⇒ **假** |
| **T41-10** | `Layout.CountingLayoutGauge` | A41-2 装置保证 + 量具校准 | 无窗口：同一棵树，裸 `VerticalLayout` vs `CountingLayout(VerticalLayout)` | 两者子几何**逐位相同**；`Calls()` 随 `Arrange()` 次数线性增长 |

★ **T41-7 的三段判据（v1.1 补，P1-2 指出 v1.0 会假绿）**

v1.0 只写「`SetText(长文本)` → 泵 3 轮（条出现）⇒ `Calls()` 增量 0」——**「条出现」是假设，不是断言**。
若条根本没翻成可见，`SyncScrollBar` 的四元组守卫（`TextBox.cpp:741-747`）会直接 return，
`m_vBar->SetVisible` 压根不被调用 ⇒ 判据「增量 0」**恒真且毫无意义**。故必须补齐：

| 段 | 断言 | 作用 |
|---|---|---|
| **0 前置校验** | `ShowAndSettle()`（空文本）后 `FindBar(tbox)->IsVisible() == false` | 证明首帧 `SyncScrollBar` 已把条从默认 `visible=true`（`WidgetState`）翻成 `false`。**不先确认「已翻下去」，「翻上来」就无从判定** |
| **1 状态翻转校准** | `tbox->SetText(长文本)` → 泵 3 轮 → `FindBar(tbox)->IsVisible() == true` | ★ 本用例的**量具校准**：确认 `SetVisible(true)` 真的发生（`need = !m_singleLine && GetMaxScrollOffset() > 0`，`TextBox.cpp:733`） |
| **2 计数判据** | 紧接同一时点 `Calls()` 增量 **0** | 被测量：条 toggle 只走重绘通道 |
| **3 正对照** | `tbox->SetVisible(false)`（其父**有** `Layout`）→ 泵 1 轮 → 增量 **1** | 证明「门控为真时确实会多排一次」——否则段 2 的 0 可能只是 `CountingLayout` 没被结算驱动过 |

★ **装置的前置条件（写死进用例注释）**：`m_singleLine` 默认 `false`（`TextBox.h:338`）⇒ 多行模式天然成立；
内容须含**多个 `\n`**（或开 `SetWordWrap(true)`），且 `TextBox` 高度压到只容 1–2 行，
使 `GetMaxScrollOffset() > 0`。★ 只给长**单段**文本（无 `\n`、不开折行）⇒ 行表只有 1 行 ⇒ 条永不出现 ⇒ 段 1 红（这是**好的**红，它暴露的是装置错而非机制错）。

★ **`FindBar(Widget&)`** = 遍历 `GetChildCount()` 用 `dynamic_cast<ScrollBar*>` 取条——`TextBox` 唯一的子节点
就是条（`TextBox.cpp:44-47`：「最后 `AddChild`」⇒ 逆序命中时条优先）。

★ **判据为何成立（门控粒度的关键）**：`m_vBar` 的**直接父**是 `TextBox`，而 `TextBox` **没有** `Layout`
⇒ `ParentHasLayout()` 为假 ⇒ 只走重绘通道。这正是 D41-4 想钉住的那一格，也是 v1.0 没验证的塌陷点。

★ **T41-5 的 `Calls() == 0` 判据不平凡**：它同时证明「置脏不做结算」（D41-8）与「无窗口不崩」。
若将来有人在 setter 里直接调 `Arrange()`，本条会红。

★ **T41-9 是无窗口可测的**（`Arrange()` 是公开的），不需要真窗口 ⇒ 覆盖 D41-3.2 的成本极低。

---

## §5 契约 C41-1..10（每条附机检；★ 机检纪律见末行）

| # | 契约 | 机检 |
|---|---|---|
| **C41-1** | **布局结果逐位不变** | `LayoutTests` 33 条（逐位断言）全绿 + T41-10 的裸/包装对照 |
| **C41-2** | **帧首结算只有一处** | `grep -rn "SettleLayoutIfDirty" ECDI/src` ⇒ **1**（`Window.cpp`） |
| **C41-3** | **resize 布局驱动点只有一处** | `grep -rn --exclude-dir=Tests -- "->Arrange()\|\.Arrange()" ECDI/src` ⇒ **1**（`Window.cpp:836` 的 `m_rootWidget->Arrange()`）。★ **v1.1 订正（P2-1）**：v1.0 用的 `\.Arrange()` **漏了 `->` 形态**——`Window.cpp:836` 是 `->Arrange()`，而唯一用 `.Arrange()` 的 `Application.cpp:292` 本 Phase 删除 ⇒ 原命令跑出来是 **0**，不是 1。★ 另：`--exclude-dir` 必须写在**路径之前**（Git Bash 实测：放后面会被当成文件名而报 `No such file or directory`） |
| **C41-4** | **置真只有一个写入点** | `grep -rn "m_layoutDirty = true" ECDI/src` ⇒ **1**（`NotifyGeometryChanged` 内） |
| **C41-5** | **门控表达式单源** | `grep -rn "m_parent->m_layout" ECDI/` ⇒ **1**（`Widget.h` 的 `ParentHasLayout()` 内联体内）。★ **v1.1 订正（P2-2）**：v1.0 前半句限定 `ECDI/src` 是错的——内联体在头里 ⇒ 该目录下恒 **0**；只保留 `ECDI/` 全库这一式 |
| **C41-6** | **结算期抑制只在出口判断** | `grep -rn "m_arrangeDepth" ECDI/src/Widget/Widget.cpp` ⇒ **4**（`++`/`--`/清脏判断/抑制判断） |
| **C41-7** | **旧契约文字清零** | §1.1.4 的三条 grep |
| **C41-8** | **同值守卫三处**（`SetSize`/`SetPosition`/`SetStretch`） | 用例判据（翻转用例覆盖 `SetSize`/`SetPosition`；T41-4 覆盖整树）；机检不可靠，**不用 grep 判定** |
| **C41-9** | **用例数守恒** | `556 passed / 0 failed`（四构建链） |
| **C41-10** | **`TextBox` 无 `SetText` override** | `grep -rn "void TextBox::SetText" ECDI/src` ⇒ **0** |

★ **机检纪律（本 Phase 方向相反，务必注意）**：Phase 40 的教训是「grep 计数**必须排除注释行**」；
本 Phase 的 C41-7 判据对象**就是注释** ⇒ 那三条**不得**排除注释。⇒ 每条 grep 都要先回答
「我这次判的是代码还是注释」，不能套用上一次的口径。

---

## §6 影响面

| 项 | 内容 |
|---|---|
| **源码（6 个 .cpp）** | `Widget.cpp`（机制主体：+5 函数 / 改 8 函数）· `Window.cpp`（`PaintFrame` + `OnResized`）· `Application.cpp`（删 1 函数）· `ScrollView.cpp`（注释 + 0 逻辑）· `TextWidget.cpp`（+2 行 `Invalidate`）· `TextBox.cpp`（−18 行） |
| **头（4 个）** | `Widget.h`（总纲 + 5 处注释 + 6 声明 + 2 成员 + `SetPosition` 移出）· `Application.h`（−2 行）· `ScrollView.h`（+extent 边界注）· ★ `TextBox.h`（**−8 行**：`SetText` 两个 override 声明 + `@details` 块，v1.1 补 P1-1） |
| **公共头** | **100 → 100**（零新增——`Widget.h`/`Application.h`/`ScrollView.h` 均既有） |
| **public API** | **+1**（`NeedsLayout()`）；`SetPosition` 签名不变（仅移出 inline） |
| **`sizeof(Widget)`** | **+8 字节**（`bool` + `int` 对齐后）。禁拷贝/禁移动 ⇒ 无外部契约受影响 |
| **CMake** | **0**（`src/Tests/*.cpp` 由 `GLOB_RECURSE` 自动入库；`WindowProbe.h` 是头，不入库） |
| **`RunAllTests.h` / `.cpp`** | **手工 2 处**（新测试文件的声明 + 注册调用） |
| **用例** | **546 → 556**（+10 新；**545 守恒 + 1 条翻转**） |
| **docs** | **204（tracked）→ 206**：`find docs -name "*.md"` **实测 on-disk 206**（含需求稿 v1.2 + 本稿两张**未提交**稿）；`git ls-files docs` = 204 是**已提交**基数。详设 +1 ⇒ **207**。★ v1.0 写「204 → 205」漏算了仍处 untracked 的需求稿（v1.1 订正，P2-4） |
| **编译批次** | ★ **一次近全量**（`Widget.h` 被几乎所有 TU 包含）⇒ 与 Phase 40 同量级（~35 min / 链 × 4） |
| **行为变化（对外可感）** | ① 业务不再需要手写 `Arrange`/`Invalidate` 调用链；② `ModelProbe` 搜索过滤的活体 bug **自动修复**（`examples` **零改动**）；③ 动画期每次值变化会在下一帧触发全树重排（§1.9，已知接受） |

---

## §7 分批（3 批；每批独立可验、可回退）

| 批 | 内容 | 用例 | 退回方式 |
|---|---|---|---|
| **批 0** | 测试基建：`WindowProbe.h` 抽取 + `VisibilityRepaintTests.cpp` 改名（**行为零变化**）+ `CountingLayout` + T41-10 | 546 → **547** | 纯测试文件，`git checkout` 即回退；产品码零改动 |
| **批 1** | 机制 + 契约文字 + 翻转：`Widget.h/.cpp`、`Window.cpp`、`Application.*`、`ScrollView.cpp:337`、T41-1/2/4/5/6/8/9 + 翻转用例 | 547 → **554** | 需整体回退（机制环环相扣，半批无意义） |
| **批 2** | 同族收敛：`TextWidget.cpp`、`TextBox.cpp`、`CollapsiblePanel.cpp`/`CaptionBar.cpp` 注释、T41-3/7 | 554 → **556** | 独立可回退（只影响「重复动作」） |

★ **批 0 的价值**：它在产品码零改动的前提下先证明「量具能测出 Arrange 次数」与「真窗口装置可复用」
——否则批 1 一旦测试红，无法区分「机制错」与「量具错」（Phase 40 的 T40-6 假红就是这类）。

---

## §8 风险 R41-1..10

| # | 风险 | 评估与对策 |
|---|---|---|
| **R41-1** | `Widget.h` 近全量重编 | 必然发生（新增成员）。四构建链各一次 ~35 min。★ 批 1 前先跑 `g++ -fsyntax-only`（数秒）抓真错 |
| **R41-2** | `SetPosition` 移出 inline 后 `Layout` 热路径变慢 | 与 `SetSize` 同为函数调用；`ListLayout` 20k 行 ≈ 20k 次 ⇒ 相对 `Arrange` 其余部分可忽略。★ 若详设想量化：`Calls()` 无法测，需在 `Arrange` 外层计时（不列为验收） |
| **R41-3** | **`SetVisible` 的既有调用点**现在可能置脏 | 三个 `SyncBars`/`SyncScrollBar` 站点的父控件**均无 `Layout`**（已亲核 4 个复合控件无 `SetLayout`）⇒ 门控全拦。★ 但**若将来**给 `TextBox` 加了 `Layout`，这批调用点会立刻开始置脏——这是**正确**行为，非回归 |
| **R41-4** | 离线子树脏合并的残余盲区 | 论证：任一次置脏都上溯到**当时的根** ⇒ 子树根的标志必然反映子树。★ **反例不可构造**（除非有人手工清 `m_layoutDirty`——全库只有 `Arrange` 与 `AddChild` 两处写 `false`，均在同文件） |
| **R41-5** | `NeedsLayout()` 是**整树**语义，易被误读为「本节点」 | 名字 + `@details` 双保险；★ 若审核认为名字有歧义，备选 `HasPendingLayout()`（本稿推荐前者，与 `HasFocus()` 同族） |
| **R41-6** | `Window` 构造期 `root->SetSize`（`Window.cpp:125`）现在会发一次 `Invalidate()` | `m_platformWindow` 已在 `:113` 之前创建、`hwnd` 有效 ⇒ `InvalidateRect` 合法且无害（窗口未显示）。★ 不能靠「无窗口 no-op」挡住（根**有**窗口）——已实核 `:113` → `:125` 的顺序 |
| **R41-7** | `TextBox::SetSize` 去 `Invalidate` 后「值没变就不重绘」 | 语义**收紧且正确**（同值无视觉变化）。唯一风险是「依赖 SetSize 顺带刷新」的外部调用点——`examples` 无（已核 4 处 `SetSize` 均为一次性配置） |
| **R41-8** | 删 `Application::OnWindowResized` | 框架内唯一消费者就是它自己；`WindowResizedEvent` 仍派发给 Widget 树，业务订阅不受影响 |
| **R41-9** | `SetLayout` 无条件置脏期间被抑制 | 构造期无 `m_arrangeDepth` ⇒ 正常置脏。★ 若有人**在 `Layout` 内换布局器**（不存在），变化会等到下一条失效才结算 ⇒ 记入 `SetLayout` 注释的 `⚠️` |
| **R41-10** | 「全绿」的记账 | 报全绿必写：**断言是否启用**（`FRAMEWORK_ASSERT` 仅 `_DEBUG`）+ **当时显示器缩放**（本机 1920×1080 @ DPI 120）+ **哪几条链是用户侧跑**（MSVC/Clang/ClangCL；AI 只自跑 MinGW） |
| **R41-11** | ★ **`AddChild` / `RemoveChild` 从此恒发 `Invalidate()`**（HEAD 两者通读**都不发**——`Widget.cpp:48-102` / `:104-161`） | 这是「子集合变 ⇒ 视觉变」的正确语义（P0-1 修复的连带结果）。风险面：既有用例若在 `AddChild` **之后**断言 `!RegionDirty()` 会红——已核 `M9` 五条均在 `AddChild` **之后**才 `ClearRegion()`（`VisibilityRepaintTests.cpp:135`/`:205`/`:233`/`:269`）⇒ 不受影响。★ 四链若出现意外的 region 断言红，先查这条 |
| **R41-12** | 结算期「零 `Invalidate`」是**误解** | `ScrollBar::SetRange`（`ScrollBar.cpp:25-32`）带裸 `Invalidate()`，不受抑制且**无需**抑制（不碰 `m_layoutDirty`）。详见 PD41-5。★ 详设/审计 grep 时勿判为漏网 |

---

## §9 留给详设的（本稿已定案之外）

1. **批 1 的精确 diff 顺序**（`Widget.h` → `Widget.cpp` → `Window.cpp` → `Application.*`），以及每步后的语法预检命令。
2. **T41-1/2/6 的具体几何数值**（`kRowHeight`-style 常量取值、`VerticalLayout` 的 spacing/padding 默认值参与后的期望 `y`）——须按 `VerticalLayout.cpp` 实读算，不得凭记忆。
3. **`WindowProbe` 的最终字段形态**（是否需要 `SetWindowPos` 封装、是否需要 `PumpUntil(...)` 谓词式泵）——以「10 条用例里出现第 2 次同型编排」为抽象判据。
4. **§1.1.0 总纲文本的最终排版**（行宽、缩进与既有类头注释的一致性）。
5. **`roadmap-deferred.md` / `phase25` 订正的落笔版本号**（需求闸门 8，需用户授权后才动）。

---

## §10 修订记录

- **v1.3（2026-10-30）** ★ **靶词自指订正（详设审核 P1-1a 回流）**：§1.1.2（`AutoSize`）与 §1.1.3
  （`ScrollView.cpp:337`）的新表述原来**原样拼出了被禁靶词**（「……布局/重绘由调用方负责」等），
  而 §1.1.4 的机检要求 `ECDI/` 内**零命中且不得排除注释** ⇒ **新注释会让判据自己命中自己**。
  改为「概括旧意 + 指向本节上方的原文引文块」（引文在 `docs/`，不受该机检约束）。
  §1.1.4 同时补两条**方向相反**的纪律：判据对象是**注释** ⇒ 不得排除注释；是**代码** ⇒ 必须排除。
  ★ 这是本 Phase 第二次栽在「机检口径」上，且这次伤的是**判据自己**——只有把机检**真跑一遍**才暴露。

- **v1.2（2026-10-30）** ★ **锚点订正（详设施工期发现）**：`SetVisible` 新表述里引用的
  「`Widget.cpp:135`/`:281` 的 Paint 前置」**两个行号早已失效**（实测 `:135` 落在 `RemoveChild` 的
  `it == end()` 分支内、`:281` 是空行）⇒ 改为 **`:489`**（`Paint` 的 `if (!IsVisible()) return;`）
  与 **`:314`**（`HitTest` 同款门控）。两处（§1.1.2 与 §1.3.3 的实现注）同步改。
  ★ **结论不受影响**（`IsVisible()` 确实是 Paint 与 HitTest 的双门控），错的是**引用**——
  教训：把旧注释原样抄进新契约时**必须重新定位每个行号**。详设 v1.0 已记同一笔。

- **v1.1（2026-10-30）** 收 **Gemini 4 Argon** 审核（1 P0 + 2 P1 + 4 P2）。**七条全部成立、零驳回**——
  其中 P0-1 是本稿与**自己的表格**互相矛盾（§1.3.2 写「重绘通道：走」，§1.3.4 代码却把它套进 `if`），
  属硬错，非表述问题。逐条落点：

  | 编号 | 性质 | 落点 |
  |---|---|---|
  | **P0-1** | 机制漏洞 | §1.3.4 改「**无条件调用、门控作实参**」；补「往无 Layout 容器增删子控件连重绘都被跳过」的击穿路径 + 三条理由 + **行为新增记账**；§1.3.2 表格两行同步；§8 新增 **R41-11** |
  | **P1-1** | 头文件遗漏 | §1.4.1 表格 + PD41-7 补 `TextBox.h:97-104` 的**声明**删除；§6 头数 3 → **4**；另指出该段注释里的 `MarkLinesDirty()` 是**全库唯一的幽灵引用**（函数不存在），真契约 D7 已在 `TextBox.h:110-116` 独立记录 ⇒ 删除无损失 |
  | **P1-2** | 测试假绿风险 | §4 T41-7 由「一条假设」扩成**四段判据**（前置校验 / 状态翻转校准 / 计数 / 正对照）+ 装置前置条件（多 `\n`、压小高度 ⇒ `GetMaxScrollOffset() > 0`）+ `FindBar()` 取条手法 |
  | **P2-1** | 机检命令错 | C41-3 改 `-- "->Arrange()\|\.Arrange()"`：`Window.cpp:836` 是 `->`，唯一 `.Arrange()` 的 `Application.cpp:292` 本 Phase 删除 ⇒ 原命令恒 0；另记「`--exclude-dir` 须在路径之前」 |
  | **P2-2** | 机检命令错 | C41-5 去掉 `ECDI/src` 限定（`ParentHasLayout` 是头内联体 ⇒ `src` 下恒 0），只留 `ECDI/` 全库式 |
  | **P2-3** | 记账缺失 | PD41-5 补「结算期**并非**零 `Invalidate`」专段（`ScrollBar::SetRange` 裸 `Invalidate`）；§8 新增 **R41-12** |
  | **P2-4** | 基数错 | §6 docs 改「204（tracked）→ **206**（on-disk 实测）」，详设 → **207** |

  ★ 审核对文末三处「盯防点」的结论：§1.1 契约文字**通过**（四层分工把两条判据分开了）；
  §1.3.4 的**归纳论证通过**（脏必上溯到当时的根）但**代码形态有洞**（已修）；§1.6.2 计数型
  `m_arrangeDepth` **不算过度设计**（保留 int）。

- **v1.0（2026-10-30）** 初设落盘。§1 十项闸门逐条收口（★ 1.1 给出三处契约文字的**逐字新表述** +
  新增类头总纲，四层分工表；1.2 翻转用例逐字；1.3 置脏出口与门控表 + `SetPosition` 移出 inline 核算；
  1.4 `SetText` 严格判据 + `TextBox` override 删除；1.5 `NeedsLayout()` + `CountingLayout` + `WindowProbe`
  抽取；1.6 清脏作用域 + 计数型 `m_arrangeDepth` 的实核论证；1.7 `CollapsiblePanel` 雷区只登记；
  1.8 两处文档订正待授权；1.9 动画期代价量化；1.10 `ScrollView` extent 边界）。
  §2 八项初设级决策 D41-A..H。§3 九个逐文件落点（含代码草案）。§4 十条用例（546 → 556）。
  §5 十条契约与机检（★ 含「grep 是否排除注释」的口径反转提醒）。§6 影响面。§7 三批。
  §8 十条风险。§9 五项留给详设。
- ~~★ **本稿最需要审核盯防的三点**~~ → **v1.1 已由 Gemini 4 Argon 裁决**：① §1.1 契约文字**通过**（两条判据真的分开了，不是换说法）；
  ② §1.3.4 的「子树脏一层合并」**归纳本身成立**，但**代码形态有 P0 洞**（已修，见 v1.1 表）；
  ③ §1.6.2 计数型 `m_arrangeDepth` **不算过度设计**（保留 int）。
- ★ **v1.1 之后仍请审核盯防的两点**：① §1.3.4「无条件调用 + 门控作实参」这一式在 `RemoveChild` 上是否也自洽
  （被摘子树仍有父指针残余的那一小段窗口）；② §4 T41-7 的四段判据在**真实装置**上是否可稳定复现
  （尤其段 0 的「空文本首帧条必为不可见」依赖 `ShowAndSettle` 至少跑过一帧 `PaintFrame`——泵 64 条消息足够，但要写成显式断言而非默认）。
