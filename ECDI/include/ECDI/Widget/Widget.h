#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Core/CursorShape.h"   // #72-②：GetCursor 返回形态（叶子头，零依赖）
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Widget/WidgetState.h"

#include <vector>
#include <memory>
#include <algorithm>

namespace ECDI{

// 前向声明：避免头文件循环依赖
class Window;
class Application;
class TimerEvent;
class MouseMoveEvent;
class MouseButtonUpEvent;
class MouseButtonDownEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;
class DropFilesEvent;
class Layout;

/// @brief ECDI Framework 的 Widget 基类
/// @details
/// 采用 Composite 模式：每个 Widget 既可以是叶子节点，也可以是容器节点。
/// 通过 AddChild/RemoveChild 构建 Widget 树。
///
/// 禁止拷贝和移动（HWND 通过 GWLP_USERDATA 绑定对象地址，节点地址必须稳定）。
///
/// 职责划分（四类能力）：
/// - Tree：    父子关系管理（AddChild / RemoveChild / GetParent）
/// - Geometry：位置与尺寸（SetPosition / SetSize / GetX 等）
/// - State：   可见性与启用状态（IsVisible / IsEnabled）
/// - Event：   事件响应虚函数（OnMouseMove / OnKeyDown 等）
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
///   它不碰 `m_layoutDirty` ⇒ 不影响结算计数。
///
/// ▸ ⚠️ **`SetText` 只走重绘通道**：文本不改变几何。要「文本变长、控件跟着变宽」请显式
///   `AutoSize()`——它是尺寸的唯一动作入口。
/// ▸ ⚠️ **`ScrollView::UpdateContentExtent` 仍是显式的**：extent 是**内容语义**（不是几何失效），
///   框架不代挂钩（D6）。
/// ▸ ⚠️ **`CollapsiblePanel` 在 #80 修好「展开基准 vs `fillCrossAxis`」冲突前，不得放入
///   `fillCrossAxis = true` 的 Layout 容器**（收起态 `SetSize` 会把基准高抹成 0，
///   `VerticalLayout.cpp:96` 的 `SetSize(cross, GetHeight())` 即触发路径——当前无实例）。
class Widget{

public:

	Widget();

	virtual ~Widget();

	// ── 禁止拷贝与移动 ──────────────────────────────
	Widget(const Widget&) = delete;
	Widget& operator=(const Widget&) = delete;

	Widget(Widget&&) = delete;
	Widget& operator=(Widget&&) = delete;

	// ── Tree ────────────────────────────────────────

	/// @brief 添加子 Widget（转移所有权到当前节点）
	/// @param child 子 Widget 的 unique_ptr，不能为 null
	/// @pre child 不能已有父节点（禁多父）
	/// @pre child 的子树中不能包含 this（防环）
	/// @details ★ 审计 #63 / §7.3：三条 @pre 在 **Release 侧均有运行时分支**——
	///          不满足即**拒绝执行**（返回 void，传入的 unique_ptr 随之析构），
	///          Debug 侧仍 `FRAMEWORK_ASSERT` 弹框终止。
	///          ⚠️ **Release 下的真实后果**（为什么不能只留断言）：空指针 ⇒ 写空指针崩；
	///          多父 ⇒ **两树各持一份所有权 ⇒ double-free**；成环 ⇒ Arrange/Paint/
	///          HitTest **无限递归 ⇒ 栈溢出**。
	///          ⚠️ 签名保持 void（**不改签名** ⇒ 既有调用点零改动）；「静默丢弃」是
	///          有意的两难取舍，详见 `Widget.cpp` 内注释的「返回值语义」段。
	void AddChild(std::unique_ptr<Widget> child);

	/// @brief 移除指定子 Widget，返回其所有权
	/// @param child 要移除的子 Widget 指针
	/// @return 被移除的子 Widget（调用方可继续使用或销毁）；**nullptr = child 为空 / 不是直接子节点**
	/// @details ★ 审计 #63 / §7.3：未命中在 Release 侧**返回 nullptr**
	///          （原实现对 `end()` 解引用 = UB），与 `GetChildAt` 越界返 nullptr
	///          的先例一致。Debug 侧仍断言暴露。
	std::unique_ptr<Widget> RemoveChild(Widget* child);

	/// @brief 请求移除自身（★ Phase 35：事件派发期间安全的「删除自身」入口）
	/// @details **为什么需要它**：事件派发栈（`Application::OnMouseButtonUp` 的冒泡循环）
	///          在回调返回后仍要读 `current->GetParent()`；若回调里 `parent->RemoveChild(this)`
	///          并丢弃返回值 ⇒ 回调返回的那一刻 `current` **已经析构** ⇒ 父链访问 = UAF。
	///          本方法把「**析构**」从回调的栈帧里搬出去：
	///          ① 若 `Application` 正处在事件派发中 ⇒ 入**延迟移除队列**，返回 `true`，
	///             **此刻不析构**——对象在本次派发栈完全展开（所有回调返回）后被 `DrainDeferredRemoval`
	///             析构，或在 `ProcessDeferredDestroy`（消息循环每轮）里析构；
	///          ② 否则（不在派发中——含无窗口/无头控件）⇒ 立即脱树，返回 `false`。
	/// @return `true` = **已入延迟队列**：对象此刻**已脱离父节点**
	///         （`GetParent()==nullptr`、不再参与后续 HitTest/Paint/焦点），但**保证存活到
	///         本次派发结束**（派发结束即析构）；`false` = 已立即脱树并**析构**
	///         （返回之后 `this` 已失效，调用方不得再访问）。
	/// @warning **两态都禁止**在返回后继续使用 `this` 的**语义状态**（父链/焦点/capture 均已不成立）；
	///          唯一区别是「本次派发栈是否安全」——`true` 时安全，`false` 时调用栈已离开对象。
	/// @note ★ 与 `RemoveChild` 的分工：`RemoveChild` 是**父方**的树维护原语（立即脱树 + 交回所有权），
	///       是延迟机制**内部**的实现者；`RequestRemove` 是**子方**在回调里的安全出口。
	/// @note 返回值**刻意不标注 `[[nodiscard]]`**：最常见用法是回调末尾一句
	///       `RequestRemove();` 后直接返回——强制检查返回值会让这一行变得啰嗦且无收益
	///       （两态下调用方都不该继续用 `this`）。
	bool RequestRemove();

	/// @brief 获取父 Widget 指针（RootWidget 返回 nullptr）
	Widget* GetParent() const noexcept{

		return m_parent;
	}

	/// @brief 获取子节点数量
	size_t GetChildCount() const noexcept { return m_children.size(); }


	// ── Geometry ────────────────────────────────────

	/// @brief 获取几何信息（位置 + 尺寸）的只读引用
	const Rect& GetGeometry() const noexcept { return m_geometry; }

	/// @brief 设置相对于父 Widget 的位置
	/// @details ★ Phase 41：**同值守卫 + 双通道失效**（实现由头内联**移入 `Widget.cpp`**——原为纯赋值）。
	///          门控（D41-4）：位置的**唯一**读者是父的 `Layout` ⇒ 仅 `ParentHasLayout()` 才置布局脏；
	///          重绘失效无条件（值真变化时）——几何变了必须上屏，不押在「平台层碰巧每帧重绘」上。
	void SetPosition(int x, int y);

	/// @brief 设置宽高
	/// @details 虚函数（9.5 R1：TextBox override 挂 Resize 后 Caret 可见——Layout 只 SetPosition 不碰尺寸，
	/// SetSize 是尺寸唯一入口）。签名不变、行为兼容；基类实现 = 直接写 m_geometry（移 Widget.cpp）
	/// @details ★★ Phase 41：基类实现改为「**同值守卫 + 双通道失效**」——
	///          ① 值真的变了才继续（否则零副作用：`Layout` 每帧对每个子调它，值多半相同）；
	///          ② 重绘通道恒发 `Invalidate()`；③ 布局通道门控 = `m_layout != nullptr ||
	///          ParentHasLayout()`（尺寸有两个读者：我的 Layout 摆子、父的 Layout 定我槽位）。
	///          ⚠️ **override 不得重复 `Invalidate()`**——基类已发，重复会逃过结算期抑制。
	virtual void SetSize(int w, int h);

	// ── Stretch（9.7 自适应布局：剩余空间分配权重）────────

	/// @brief 设置剩余空间分配权重（9.7——0 = 不参与分配，保持当前尺寸；>0 = 主轴尺寸由父 Layout 分配）
	/// @pre stretch >= 0（debug assert；负值无合理权重语义）
	/// @details ★ 审计 #63 / §7.3：负值在 Release 侧**钳为 0**（与 `Layout` 的
	///          spacing/padding 钳 0 同型；选钳 0 而非拒绝，因为「0 = 不参与分配」
	///          是有定义且正确的语义）。Debug 断言保留以暴露调用方错误。
	/// @details ★ Phase 41：同值守卫 + 门控 = `ParentHasLayout()`（stretch 的唯一读者是父 Layout）。
	void SetStretch(int stretch) noexcept;

	/// @brief 剩余空间分配权重只读查询（默认 0——不参与分配，向后兼容锚点）
	[[nodiscard]] int GetStretch() const noexcept{ return m_stretch; }

	// ── AutoSize（9.8 内容自适应：GetPreferredSize 查询 + AutoSize 显式动作）────

	/// @brief 控件「希望」的尺寸（内容驱动；未 override = 当前尺寸——零回归；兑现 Size.h 注释）
	/// @details 9.8：让控件知道自己需要多大。默认返回当前尺寸；TextWidget override 返回内容测量值；
	/// TextBox override 加 padding。preferred 是查询——尺寸调整走 AutoSize()
	[[nodiscard]] virtual Size GetPreferredSize() const;

	/// @brief 按 GetPreferredSize() 调整自身尺寸（9.8 显式命令——后调用者赢，需求 R5 v1.5）
	/// @return true = 实际调用了 SetSize；false = stretch 互斥 no-op 或尺寸未变化
	/// @note §3.7 纯 geometry operation：内部**只调 SetSize**（虚分派——TextBox override 既有语义）；
	///       float → int 向零截断（v1 不引入 rounding policy）
	/// @note ★★ **Phase 41 契约变更**（旧契约「由调用方自行排布与刷新」已推翻——★ 旧句**故意不复述**：
	///       它是本 Phase 的机检靶词（要求全库零命中），原样拼进新注释会让判据自己命中自己；
	///       原文全句见 `docs/dev/phase41-frame-layout-invalidation-pipeline-*-design.md` 的引文块）：
	///       本方法**自身**仍不 Arrange、不 `Invalidate`——但 `SetSize` 现在是**失效源**
	///       （重绘 + 布局双通道，见 `SetSize` 与类头「布局 / 重绘失效」）。
	///       ⇒ 尺寸真变化后，**下一帧首**自动完成重排与重绘，调用方**无需**再补
	///       `Arrange()` / `Invalidate()`；尺寸未变化时两条通道都不动（同值守卫）。
	///       ★ 仍**不挂钩 `SetText`**：文本变长不会自动触发 AutoSize——那是**内容语义**，
	///       不是几何失效（框架不替调用方决定「文本该不该撑开控件」）。
	bool AutoSize();

	// ── 内容偏移接缝（Phase 15 D1-A：视口偏移——内容坐标 → 视口坐标的变换）──
	// 语义：本控件「子内容」相对视口的位移（正 = 内容被向左 / 向上推出）。
	// ⚠️ 不影响自身：自身 m_geometry 与 Paint 的 PushClip 一律用**视口矩形**，绝不叠加本偏移。
	// ⚠️ 三处必须同变换：Paint（子偏移）/ HitTest（子局部）/ GetAbsolutePosition（父链累加）。
	// 默认 0 ⇒ 非容器控件零回归（既有行为逐位不变）。

	/// @brief 内容偏移 X（默认 0——仅滚动容器的内容节点 override 返回非 0）
	/// @details **override 归属**：只有「内容坐标空间的根」（`ScrollContent`）才 override；
	/// `ScrollView` 自身**不得** override——否则滚动条作为直接子也会被偏移（Phase 15 详设 §3.1.1）。
	[[nodiscard]] virtual int GetContentOffsetX() const noexcept { return 0; }

	/// @brief 内容偏移 Y（默认 0）
	[[nodiscard]] virtual int GetContentOffsetY() const noexcept { return 0; }

	/// @brief 是否把子节点的命中约束在本控件矩形内（Phase 15 D2——默认 false 零回归）
	/// @details 语义：**命中点**须落在此容器矩形内，否则**不递归其子树**——不是「限制子控件几何」。
	/// 检查发生在**递归子节点之前**；逐层检查自动等价于「所有祖先裁剪域的交集」
	/// （与 Paint 的嵌套 PushClip 同构）。
	/// ⚠️ **不得用 ContainsPoint 实现本门控**：`Panel::ContainsPoint` 恒 `false`，会拦死自身整棵子树。
	/// 实际判据走**非虚** `ContainsRect`。
	[[nodiscard]] virtual bool ClipsChildren() const noexcept { return false; }

	int GetX() const noexcept { return static_cast<int>(m_geometry.x); }

	int GetY() const noexcept { return static_cast<int>(m_geometry.y); }

	int GetWidth() const noexcept { return static_cast<int>(m_geometry.width); }

	int GetHeight() const noexcept { return static_cast<int>(m_geometry.height); }


	// ── State ───────────────────────────────────────

	bool IsVisible() const noexcept { return m_state.visible; }

	/// @brief 设置可见性（★ 审计 M9 修复：内建 `Invalidate()`）
	/// @details ★★ **为何从「裸赋值」改为「同值守卫 + Invalidate」**（修复前 = `m_state.visible = v;`）：
	///   修复前本 setter **只赋值、不请求重绘**，而同族 setter（`SetStyle` / `ApplyTheme` /
	///   `TextWidget::SetWordWrap`）**全都内建 `Invalidate()`** ⇒ 应用侧
	///   `SetVisible(false)` 后视觉不消失、`SetVisible(true)` 后不出现——**静默不刷新**。
	///   这与条 65 记录的 `SetText` 陷阱**同族**（非鼠标事件驱动的 UI 更新漏了 Invalidate）。
	///   ★ 三条关键设计：
	///   ① ★★ **同值守卫是正确性要求、不是优化**（条 134 同族）：`TextBox::SyncScrollBar`
	///      在 `OnPaint` 起始**每帧**调用 `m_vBar->SetVisible(need)`（`TextBox.cpp:562`）——
	///      若无同值守卫，内建的 `Invalidate` 会让**每帧都请求重绘** ⇒ **自激励重绘循环**
	///      （该处已有「四元组值比较」守卫，本处再补一层是为了让**任何**调用方都安全）。
	///   ② **不扩大到 `SetEnabled`**：实测 `IsEnabled()` **无任何绘制消费者**——全库只 gate
	///      命中（`Widget.cpp:142`）/ 焦点收集（`Window.cpp:43`）/ 键派发（`Window.cpp:677`）⇒
	///      给它加 `Invalidate` 只是**语义为空的重绘请求**（如实记账：审计原文将两者并列）。
	///   ③ ★★ **Phase 41 契约变更——本条旧豁免已撤销**：原豁免的依据是「几何 setter 由 `Layout`
	///      逐帧批量调用（加了会每帧重绘）」。⇒ 该理由的**事实基础已不成立**：`Win32PlatformWindow`
	///      的 `WM_PAINT` **不调 `BeginPaint`/`EndPaint`**（决策 39）⇒ update region **从不 validate**
	///      ⇒ **每帧本来就在全量重绘**，`Invalidate` 的**边际**代价 ≈ 0。真正的代价项是
	///      「每帧 O(N) 布局」，而它由「布局失效」的**门控 + 帧首结算**挡掉（见类头「布局 / 重绘失效」）。
	///      ⇒ 现行为**两条通道、两套判据**：
	///        · **重绘通道** = 值真的变了 ⇒ `Invalidate()`（本条即该通道的先例，M9 只先落了这一格）；
	///        · **布局通道** = 值真的变了 **且** 某个 `Layout` 会读它 ⇒ 置根布局脏，帧首结算。
	///      ⇒ 可见性**两条都走**：它既是绘制门控（`Widget.cpp:489` 的 Paint 前置 / `:314` 的
	///      HitTest 门控），也是**布局输入**（`Layout` 对不可见子跳过槽位并停泊到 bbox 负区
	///      ——C-VIS-2/4）。
	///   ⚠️ **窗口外（无头）为 no-op**：`Widget::Invalidate()` 沿父链上溯取 Window，
	///      取不到即什么都不做（测试/构造期路径零副作用）。
	void SetVisible(bool v);

	bool IsEnabled() const noexcept { return m_state.enabled; }

	/// @brief 设置启用状态（**故意不内建 `Invalidate`**——见 `SetVisible` 注释 ②：
	///        `IsEnabled()` 无任何绘制消费者，加它只会产生语义为空的重绘请求）
	void SetEnabled(bool e) { m_state.enabled = e; }

	// ── Event Handling（子类 override 以响应事件）────

	/// @brief 鼠标移动
	virtual void OnMouseMove(const MouseMoveEvent& event);

	/// @brief 鼠标按键按下
	virtual void OnMouseButtonDown(const MouseButtonDownEvent& event);

	/// @brief 鼠标按键释放
	virtual void OnMouseButtonUp(const MouseButtonUpEvent& event);

	/// @brief 鼠标滚轮
	virtual void OnMouseWheel(const MouseWheelEvent& event);

	/// @brief 键盘按键按下
	virtual void OnKeyDown(const KeyDownEvent& event);

	/// @brief 键盘按键释放
	virtual void OnKeyUp(const KeyUpEvent& event);

	/// @brief 字符输入
	virtual void OnCharInput(const CharInputEvent& event);

	/// @brief 周期定时器触发（8.5.1；焦点控件可 override——TextBox 光标闪烁）
	/// @details 空实现——无定时器需求的控件不感知；Event 原则"语义由消费者解释"
	virtual void OnTimer(const TimerEvent& event);

	/// @brief 文件拖入（Phase 14 R9——bubbling 调用点；默认不响应）
	/// @details 与鼠标事件同族（Application::OnDropFiles 经 HitTest 找到目标后沿 Parent 链派发）；
	/// 平台层已完成 DragFinish（R10）——事件携带的是已脱离系统资源的纯数据。
	virtual void OnDropFiles(const DropFilesEvent& event){}

	// ── Hover（9.5 R4：Hover 状态变化事实——9.6 动画前置）────────
	// 职责：只产生"已发生的事实"（进入/离开），不负责视觉过渡/动画/Hover Style（语义推迟消费者）
	// 契约：脱树导致的失效目标不派发 OnMouseLeave（Application::IsWidgetInTree 验证）

	/// @brief 鼠标移入通知（Hover 状态变化事实——9.6 动画前置）
	/// @details 默认空实现——无 hover 需求的控件零感知；事件驱动语义：进入 = 状态变化事实，非坐标事实
	virtual void OnMouseEnter();

	/// @brief 鼠标移出通知（Hover 状态变化事实）
	/// @details 默认空实现；契约：脱树导致的失效不派发此方法（Application 验证）
	virtual void OnMouseLeave();

	/// @brief 该控件悬停时应显示的光标形态（★ #72-②）
	/// @details 平台层经 `WM_SETCURSOR` → `PlatformWindowHost::OnQueryCursor` →
	///          窗口 HitTest 后询问**命中控件**；默认箭头，TextBox 等文本编辑区
	///          override 为 `IBeam`。与 OnMouseEnter/Leave 同族——只自述、不持有状态
	///          （光标切换由平台层每次 WM_SETCURSOR 重查，无框架侧缓存可失配）。
	[[nodiscard]] virtual CursorShape GetCursor() const noexcept{ return CursorShape::Arrow; }

	// ── Focus ──────────────────────────────────────

	/// @brief 是否可以获得键盘焦点
	/// @return 默认 false（不可聚焦），子类 override 返回 true
	/// @details 只声明能力，visible/enabled 判断由事件系统前置处理
	virtual bool CanFocus() const noexcept { return false; }

	/// @brief 该控件是否消费鼠标输入（Phase 13 D9——NCHITTEST 委托判据）
	/// @details 语义：「被 HitTest 命中」**不等于**「应阻止系统的标题栏拖拽」。默认 false（纯显示控件）——
	/// Label / Panel / ProgressBar / CollapsiblePanel 命中后，caption 区仍返回 HTCAPTION
	/// （保证「拖标题文字可移动窗口」）；交互控件（Button / StateWidget / TextBox，及内部 CaptionButton）
	/// override 返回 true。
	/// ⚠️ **坐标无关**：HitTest 只返回控件指针、不返回局部坐标（若做成点查询需二次坐标换算）⇒
	/// 局部可交互需求由**复合控件**表达（如 CaptionBar 把按钮做成子控件——子命中拿到的就是按钮自身）。
	/// ⚠️ **与 CanFocus() 无关**：后者是键盘焦点语义，两者恰好近似但不可互推（复用会让概念互相绑架）。
	virtual bool ConsumesMouseInput() const noexcept { return false; }

	/// @brief 获得键盘焦点通知（5.4.3；由 Window::SetFocusedWidget 触发）
	/// @details 子类 override 响应——TextBox 显示光标 / Button 画焦点框等；默认空实现
	virtual void OnFocusGained() {}

	/// @brief 失去键盘焦点通知（5.4.3；由 Window::SetFocusedWidget 触发）
	/// @details 子类 override 响应——TextBox 清 Selection 等；默认空实现
	virtual void OnFocusLost() {}

	// ── 交互/重绘（5.4.1）────────────────────────────

	/// @brief 请求重绘（上溯到根 → Window::Invalidate → WM_PAINT → PaintFrame）
	/// @details 数据改变后调用（SetText / m_pressed / 未来 InsertCharacter 全走它）
	void Invalidate();

	/// @brief **整棵所属树**是否有待结算的布局失效（`Window::PaintFrame` 帧首会消费它）
	/// @details ★ 语义是**整树**、不是「本节点」：脏标志寄存在**根**（D41-2 ⓐ），本方法沿父链
	///          上溯到根再读——与 `HasFocus()`（`Widget.cpp` 内同款上溯）同型。
	///          ⇒ 非根节点返回 `true` 意味着「**同树别的分支**（或本分支）有失效待结算」。
	/// @note 主要消费者 = 测试与诊断；框架自身走私有的 `SettleLayoutIfDirty`。
	[[nodiscard]] bool NeedsLayout() const noexcept;

	/// @brief 当前 Widget 是否拥有键盘焦点（上溯到根查 Window 的焦点状态）
	bool HasFocus() const noexcept;

	// ── 聚焦框开关（9.6 收尾，方案 A——行为开关，与 Style 体系正交）──

	/// @brief 设置是否显示聚焦框（视觉开关；默认 true——既有行为不变）
	/// @details 各控件 OnPaint 的焦点视觉统一走 ShowFocusRect() 门控：
	/// Button 点线框 / CheckBox·Radio 边框换色 / TextBox 边框环。
	/// 焦点状态本身（HasFocus / Tab 导航 / 键盘事件）不受影响。
	/// TextBox 的文本内缩（Caret/Selection/IME 坐标同源）不与视觉框绑定——关闭后仍保留。
	void SetShowFocusRect(bool show) noexcept{ m_showFocusRect = show; }

	/// @brief 是否显示聚焦框（默认 true）
	bool ShowFocusRect() const noexcept{ return m_showFocusRect; }

	/// @brief 获取绝对坐标（父链累加，**含沿途内容偏移**——返回视觉位置）
	/// @details Phase 15：逐层**减去**父的内容偏移。既有消费者全部需要视觉位置
	/// （TextBox 光标 3 处 / Button·CaptionButton 的 I6 判定 2 处——详设 §5 K10）。
	Point GetAbsolutePosition() const noexcept;

	// ── HitTest ─────────────────────────────────────

	/// @brief 命中测试：在 Widget 子树中找到最深层的、包含 (x,y) 的 Widget
	/// @param x 局部坐标 X
	/// @param y 局部坐标 Y
	/// @return 命中的 Widget 指针，未命中返回 nullptr
	/// @details
	/// 算法：检查可见/启用 → 逆序遍历子节点（Z-Order）→ 递归 → ContainsPoint
	/// "返回即合法"契约：返回值一定满足 Visible && Enabled && ContainsPoint
	Widget* HitTest(int x, int y)noexcept;

	void SetLayout(std::unique_ptr<Layout> layout);

	/// @brief 立即重排本子树（`m_layout->Arrange` 自上而下 + 递归子节点）
	/// @details ★ Phase 41：**帧首结算**（`Window::PaintFrame` → `SettleLayoutIfDirty`）已接管
	///          「几何变了要重排」——业务侧通常**不需要**再调本方法。
	///          ⚠️ **清脏作用域**（D41-3.2）：在**根**上调用 ⇒ 完成即清根布局脏（`PaintFrame`
	///          随后不再重排）；在**子树**上调用 ⇒ **不清**（兄弟分支可能仍脏）。
	///          ⚠️ 调用期间对**整棵树**抑制「置脏 + 经出口的 `Invalidate`」（`Layout` 写几何是
	///          布局的输出，不构成新的布局失效，且同帧即上屏）。
	void Arrange();

	void ArrangeInternal();

	Widget* GetChildAt(size_t index) noexcept;

	const Widget* GetChildAt(size_t index) const noexcept;

	void Paint(PaintContext& ctx,int offsetX,int offsetY);

	// ── 派发链（★ Phase 35：事件派发期间祖先链的存活保证）────────────
	// 背景：`Application` 的冒泡循环每次都经 `current->GetParent()` 上溯。若某回调请求移除
	//   祖先/自身，摘树后父链即断（或更糟：对象已析构）⇒ 派发栈读父指针 = UAF。
	// ★ 方案 = 「命中目标 → 根」的祖先链各节点**钉住这一次派发**：
	//   ① 链在**派发开始时**一次性冻结（`PrepareDispatchChain`）——之后摘树/挂树都不改变本次
	//      「还该向谁派发」的集合（可测试：派发开始后挂上的新祖先本次收不到事件）；
	//   ② 每个节点 `m_dispatchRefCount>0` 期间，`RequestRemove` **只摘树、不析构**；
	//   ③ 摘树的节点在链上被标记 `m_removedDuringDispatch` ⇒ 后续迭代**跳过**它
	//      （「已移除的控件不再接收事件」——与用户直觉一致，且避免向已脱离 UI 的对象派发）。
	// ⚠️ **边界（如实记账）**：钉住的是「本次派发开始时已存在的祖先链」。若回调**绕过**
	//    `RequestRemove` 直接 `parent->RemoveChild(child)` 并丢弃返回值，该对象会在回调栈帧内
	//    立即析构 —— 框架无法拦截（`RemoveChild` 是既有公共原语、其「立即交回所有权」语义不作废）。
	//    安全的删除入口 = `RequestRemove`。

private:

	/// @brief 登记「本次派发的祖先链」为存活保护（由 Application 在派发开始时调用）
	/// @param chain 命中目标 → 根 的节点列表（顺序无关；`nullptr` 元素被忽略）
	static void PrepareDispatchChain(const std::vector<Widget*>& chain) noexcept;

	/// @brief 解除一次派发的存活保护（与 `PrepareDispatchChain` **严格配对**；幂等）
	/// @details 解除时顺带清掉各节点上残留的 `m_removedDuringDispatch` 标记（下一次派发重新冻结）。
	static void ReleaseDispatchChain(const std::vector<Widget*>& chain) noexcept;

	/// @brief 本节点是否在**当前**派发中被移除（链上早退判据；非链节点恒 false）
	[[nodiscard]] bool IsRemovedDuringDispatch() const noexcept{ return m_removedDuringDispatch; }

	/// @brief 立即脱树（`RequestRemove` 的「立即」分支；★ 可能析构 `this`——返回后不得再访问）
	void DetachFromParentNow();

	/// @brief RootWidget 判断（`RequestRemove` 的终止条件——根无父可摘）
	[[nodiscard]] bool IsRoot() const noexcept{ return m_parent == nullptr && m_window != nullptr; }

	/// @brief 找到所属 Application（沿父链上溯到根取 Window → Application；无窗口返回 nullptr）
	[[nodiscard]] Application* FindApplication() noexcept;

	/// @brief 设置所属 Window（仅 Window 构造 RootWidget 后调用；friend 授权）
	void SetWindow(Window* window);

	friend class Window;	///< SetWindow 私有化的配套授权
	friend class Application;	///< ★ Phase 35：派发链 Prepare/Release + 延迟移除队列（最小 API 面）

	/// @brief 递归检测 widget 是否是 this 的后代（用于 AddChild 防环检测）
	bool Contains(const Widget* widget) const noexcept;

	/// @brief 矩形命中判定（**非虚**——供 ContainsPoint 默认实现与 ClipsChildren 门控共用）
	/// @details 抽出的动机：D2 门控**不能**走 `ContainsPoint`（虚函数、被 `Panel` 覆盖为恒 false），
	/// 但两者应共享**同一份**判定表达式（`[0, width) × [0, height)`）——否则门控与命中语义会漂移。
	[[nodiscard]] bool ContainsRect(int x, int y) const noexcept;

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

	/// @brief 几何类 setter 的**唯一**失效出口（三条语义见 `Widget.cpp` 实现处注释）
	/// @param layoutInputChanged 本次写入是否是「某个 `Layout` 会读的输入」——门控由**调用方**
	///        算好传入（出口不知道「这次改的是哪个字段」，故不能自己判）
	/// @note ★ **无条件调用**，门控只作实参：重绘通道对几何类 setter 恒发，绝不因门控为假而跳过。
	void NotifyGeometryChanged(bool layoutInputChanged) noexcept;

	/// @brief 帧首布局结算（**仅根有意义**；由 `Window::PaintFrame` 经 `friend class Window` 调用）
	void SettleLayoutIfDirty();

	Widget* m_parent = nullptr;		///< 父节点指针（RootWidget 为 nullptr）

	std::vector<std::unique_ptr<Widget>> m_children;	///< 子节点列表（有序，后添加的在上层）

	Rect m_geometry;		///< 位置与尺寸（Core 公共类型 Rect(float)，决策 1）

	WidgetState m_state;		///< 可见性与启用状态

	bool m_showFocusRect = true;	///< 聚焦框视觉开关（9.6 收尾方案 A；默认显示）

	int m_stretch = 0;		///< 剩余空间分配权重（9.7；默认 0 = 不参与分配——向后兼容锚点）

	std::unique_ptr<Layout> m_layout;

	bool m_layoutDirty = false;	///< ★ Phase 41：根的待结算布局失效（D41-2 ⓐ；**非根只在挂入瞬间被读**）

	int m_arrangeDepth = 0;	///< ★ Phase 41：结算期嵌套深度（D41-3 ⓑ 抑制；用计数而非 bool——同 `m_dispatchRefCount`）

	Window* m_window = nullptr;		///< 所属 Window（非拥有；仅 RootWidget 由 Window 设置）

	/// @brief 本次事件派发的存活钉住计数（★ Phase 35；`Application` 在派发开始/结束时 ±1）
	/// @details `>0` ⇒ 本节点处在一个已知派发栈的路径上 ⇒ `RequestRemove` 只摘树、延迟析构。
	///          用**计数**而非 bool：控件可能同时被两层派发覆盖（如 Up 回调里再派发 Timer）。
	int m_dispatchRefCount = 0;

	/// @brief 是否在**当前**派发中被移除（派发栈据此跳过本节点；`ReleaseDispatchChain` 清零）
	bool m_removedDuringDispatch = false;

protected:

	/// @brief 获取所属 Window（沿父链上溯到根；派生控件可访问——TextBox/ScrollBar 等）
	Window* GetWindow() noexcept;
	const Window* GetWindow() const noexcept;

	/// @brief 判断局部坐标 (x,y) 是否落在当前 Widget 的命中区域内
	/// @details /// 默认实现：矩形区域 [0, width) × [0, height)。
	/// 子类可 override 实现圆形、不规则形状等自定义命中区域（Template Method）。
	virtual bool ContainsPoint(int x, int y) const noexcept;

	virtual void OnPaint(PaintContext& ctx,int x,int y);
};

}
