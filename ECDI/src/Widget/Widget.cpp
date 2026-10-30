#include "ECDI/Widget/Widget.h"

#include "ECDI/Application/Application.h"   // ★ Phase 35：RequestRemove 需要 IsDispatching/DrainDeferredRemoval
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/Layout.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <utility>

namespace ECDI{

Widget::Widget() = default;

Widget::~Widget() = default;

bool Widget::Contains(const Widget* widget) const noexcept{

	if (this == widget){

		return true;

	}

	// 递归搜索所有后代
	for (const auto& child : m_children){

		if (child->Contains(widget)){

			return true;

		}

	}

	return false;
}

void Widget::AddChild(std::unique_ptr<Widget> child){

	// ★★ 审计 #63 / §7.3：三条前置条件**全部补 Release 侧运行时分支**。
	//   背景：`FRAMEWORK_ASSERT` 在 Release 下展开为 `((void)0)`（`ECDIAssert.h:41`）
	//   ⇒ 只写断言 = Release 下契约完全失守。本项目已在两处确立
	//   「**不把契约建立在断言上**」的纪律——`ScrollView::SetScrollStep:154-158`
	//   （负步长 return）与 `Widget::GetChildAt:198-201`（越界返 nullptr，审计 L11）。
	//   本条把同一条纪律补到 `Widget` 的树维护入口。
	//
	// ★★ **三条为何不能只留断言**（Release 下的真实后果，逐条不同）：
	//   ① **child 非空**：空指针 ⇒ `child->m_parent = this` 直接崩（写空指针地址）。
	//   ② **无父（禁多父）**：同一个节点挂进两棵树 ⇒ **两棵树各持一份所有权**
	//      ⇒ 双重释放（double-free）——这是三条里最重的：**内存损坏**，且症状
	//      远离触发点（极难定位）。
	//   ③ **子树不含 this（防环）**：树成环 ⇒ `ArrangeInternal` / `Paint` /
	//      `HitTest` / `Contains` 四处**无限递归** ⇒ 栈溢出崩溃。
	//
	// ★ **返回值语义（如实记账）**：本方法返回 void ⇒ 拒绝时**静默丢弃传入的
	//   unique_ptr**（child 在此析构）。这是**有意的两难取舍**：改签名会让
	//   每一个调用点都要处理返回值（违背「加能力 ≠ 改已有调用点」），而非法
	//   调用本身是框架级误用（app 需持同一节点指针才能构造出多父/环）。
	//   ⇒ Debug 侧仍由 FRAMEWORK_ASSERT **暴露**（弹框 + 终止），Release 侧
	//   **拒绝执行**（不崩、不损坏内存）。这与 `GetChildAt` 「Debug 断言 +
	//   Release 返 nullptr」的双轨形态一致。
	//
	// ⚠️ **顺序不可换**：三条检查须在 `m_children.emplace_back(std::move(child))`
	//   之前——早退时 child 必须仍是**本函数的栈上对象**（否则 move 后无权释放它）。

	// 前置条件 ①：child 非空（Release 下同样拒绝——空指针写 m_parent 会崩）
	if (!child){
		FRAMEWORK_ASSERT(false);
		return;   // 静默丢弃（见上方「返回值语义」记账）
	}

	// 前置条件 ②：child 尚无父节点（禁止多父）
	//   ⚠️ 必须先于 ③：`Contains` 走子链，若 child 已有父则它不可能是 this 的后代，
	//      顺序不影响正确性；但把「多父」放在前面，其诊断信息更贴近根因。
	if (child->m_parent != nullptr){
		FRAMEWORK_ASSERT(false);
		return;
	}

	// 前置条件 ③：child 的子树中不能包含 this（防环）
	//   ⚠️ 这是 O(child 子树) 的遍历——与原先断言内的 `Contains(this)` **同一次遍历**，
	//      没有新增复杂度（原先算一次，现在还是一次）。
	if (child->Contains(this)){
		FRAMEWORK_ASSERT(false);
		return;
	}

	// ★★ Phase 41（D41-6）：**子树脏向新根合并**。
	//   业务侧的真实建树路径是「先离线搭子树、再挂进窗口」——离线期 `AddChild`/`SetLayout`
	//   把脏写在该子树的**临时根**上；根单标志只看真根 ⇒ 不合并就是**挂入后首帧不排**
	//   （几何停在初始值）。★ 无法靠 `SetWindow` 兜底：它是 private、唯一调用点是
	//   `Window` 构造第 113 行（对刚 new 出来的空 root），不是业务路径。
	Widget* const raw = child.get();

	const bool subtreeDirty = raw->m_layoutDirty;

	raw->m_layoutDirty = false;   // 已由真根承载；临时根不再被查询（防「二次挂入」伪脏）

	raw->m_parent = this;

	m_children.emplace_back(std::move(child));

	// ★★ 失效出口**无条件调用**，门控只作**实参**：
	//   ① 子树脏：无条件合并（它代表一条**真实**失效，不需再过门控）；
	//   ② 自身置脏：子集合的唯一读者是**我的** Layout。
	//   ⚠️ 绝不可写成 `if (subtreeDirty || m_layout){ ... }` 把整句包起来——那会让「往**无 Layout**
	//   的父节点加控件」连**重绘通道**都被跳过（新控件要等下一次偶然重绘才出现）。
	//   ★ 这是**行为新增**：Phase 41 之前 AddChild 不发 Invalidate。
	NotifyGeometryChanged(subtreeDirty || m_layout != nullptr);

}

std::unique_ptr<Widget>Widget::RemoveChild(Widget* child){

	// ★★ 审计 #63 / §7.3：两条前置条件补 Release 侧运行时分支。
	//   Release 下的真实后果（原实现只写断言 ⇒ 断言为空 ⇒ 直接踩 UB）：
	//   ① **child 为 null**：`std::find_if` 拿 nullptr 去比 ⇒ 必然不命中 ⇒ `it == end()`，
	//      接着 `std::move(*it)` 对 `end()` 解引用 = **未定义行为**（实际常表现崩溃）。
	//   ② **child 不是本节点的直接子节点**：同上，`it == end()` ⇒ `*it` UB。
	//   ⇒ 修法 = 「**先判后解引用**」：两态合并为一次 `it == end()` 检查
	//   （null 天然不会命中），Debug 侧断言仍区分具体是哪一条。
	//
	// ★ **返回 nullptr 的语义**（与 `GetChildAt` 越界返 nullptr 的先例一致）：
	//   「没有可移除的东西」是**可表示的结果**，调用方本就要检查
	//   （既有用法 `auto removed = root.RemoveChild(p);` 随后用 `removed`）。
	//   ⚠️ 不抛异常：框架全库无异常契约（`noexcept` 邻域），抛会改变错误模型。

	if (child == nullptr){
		FRAMEWORK_ASSERT(false);             // Debug：区分「空指针」
		return nullptr;
	}

	// 在子节点列表中查找目标
	auto it = std::find_if(
		m_children.begin(),
		m_children.end(),
		[child](const auto& item){
		
			return item.get() == child;

		}
	);

	// ★ 审计 #63：未命中 ⇒ Release 下不得对 end() 解引用（`std::move(*it)` = UB）
	if (it == m_children.end()){
		FRAMEWORK_ASSERT(false);             // Debug：区分「非直接子节点」
		return nullptr;
	}

	// 取出 unique_ptr 所有权并从列表中删除
	std::unique_ptr<Widget> result=std::move(*it);

	m_children.erase(it);

	// ★ Phase 41：子集合变化 ⇒ 走失效出口（门控只说「**我的** Layout 会读子集合」，重绘无条件是发）。
	//   ⚠️ 同样**不得**套 `if`——从**无 Layout** 的容器摘子也必须重绘。
	//   ★ 插入点等价性：出口里的 `Invalidate()` 是从 **`this`** 上溯取 Window，与被摘节点的
	//   `m_parent`（下文才清）无关 ⇒ 早一处/晚一处结果相同；放这里只是让「子集合刚变」
	//   这个因果在代码里读得出来。此时 `result` 仍持有所有权、`this` 的父链完好。
	NotifyGeometryChanged(m_layout != nullptr);

	// ★★ 审计 #59 / H6：**清除父指针之前**注销该子树上的一切非拥有登记。
	// 背景：框架有三处非拥有 Widget 指针（Window 的焦点/捕获 + HoverTracker 的 hover），
	//   而本方法原只清 `m_parent` ⇒ 三条 UAF 路径（丢弃返回值即析构 ⇒ 下次按键/移动/
	//   悬浮打在死对象上）。
	// ⚠️ **顺序不可换**：注销判据是「目标是否落在被摘子树内」，靠**父链上溯**实现
	//   （`Window::ForgetSubtree` → `IsWithinSubtree`）；下面一行清掉 `m_parent` 后
	//   链即断裂、再也判不出包含关系。这正是必须在"摘树时刻"注销、而不是在 `~Widget`
	//   里注销的根本原因（那时节点早已脱树，找不到所属 Window）。
	// ★ **为何不在 `~Widget` 里做**：`m_children` 是 private `vector<unique_ptr>`，
	//   外部无法在不经本方法的情况下销毁**仍在树上**的节点 ⇒ 本方法与整树析构是仅有的
	//   两条销毁路径，而整树析构时 Window 自身也在拆（无需清理）。故 `~Widget` 通知是冗余的
	//   ——这一取舍已记入 `roadmap-deferred.md` #59（含被否决的「每节点常驻 Window*」方案）。
	if (Window* window = GetWindow()){

		window->ForgetSubtree(result.get());

	}

	// 清除父指针（已脱离树）
	result->m_parent = nullptr;

	// ★ Phase 35：本次派发内的摘树 —— 相关节点**已钉住**（Application 派发开始时登记了
	//   命中目标 → 根 的整条祖先链），故此处绝不析构（`result` 由 RemoveChild 的返回值持有者
	//   负责释放：正常情况是 `RequestRemove` 的延迟队列，最终由 DrainDeferredRemoval 析构）。
	//   ⚠️ 这里只打标记、不动 `m_dispatchRefCount`——本次派发栈仍可能上溯到它，
	//   计数必须由 `ReleaseDispatchChain` 在派发真正结束时统一平衡。
	if (result->m_dispatchRefCount > 0){

		result->m_removedDuringDispatch = true;

	}

	return result;
}

// ── ★ Phase 35：派发期间的存活保护 + 子方安全删除入口 ──────────────────────

void Widget::PrepareDispatchChain(const std::vector<Widget*>& chain) noexcept{

	// 每个节点 +1：本次派发期间 `RequestRemove` 只摘树不析构
	for (Widget* node : chain){

		if (node != nullptr){

			++node->m_dispatchRefCount;

			node->m_removedDuringDispatch = false;   // 新一轮派发：标记复位（防御——正常已由 Release 清）

		}

	}

}

void Widget::ReleaseDispatchChain(const std::vector<Widget*>& chain) noexcept{

	// 与 Prepare 严格配对（同一份 chain 指针列表，顺序无关——每次都作用在同一组节点上）
	for (Widget* node : chain){

		if (node == nullptr){

			continue;

		}

		if (node->m_dispatchRefCount > 0){

			--node->m_dispatchRefCount;

		}

		node->m_removedDuringDispatch = false;

	}

}

Application* Widget::FindApplication() noexcept{

	if (Window* window = GetWindow()){

		return &window->GetApplication();

	}

	return nullptr;

}

bool Widget::RequestRemove(){

	// RootWidget：无父可摘、且是 Window 的拥有对象 —— 拒绝（不崩、不静默析构整棵树）
	if (m_parent == nullptr){

		FRAMEWORK_ASSERT(false);   // Debug 暴露调用方错误（摘根应走 Window::Release）
		return false;

	}

	// ⚠️ Application 的私有派发状态**只能**经 `ApplicationAccess` 访问（见 `Application.h` 的
	//   访问权限备注）：`friend class Widget` 在 clang-cl 下不覆盖 Widget 的成员函数体。
	Application* app = FindApplication();

	// ① 不在派发中（含无窗口/无头控件）：立即脱树（与既有 RemoveChild 语义一致）
	if (app == nullptr || !ApplicationAccess::IsDispatching(*app)){

		DetachFromParentNow();

		return false;   // ★ 返回后 this 已析构

	}

	// ② 派发中：摘树 + 把所有权交给延迟队列（★ 此刻**不析构**）
	//    为何先摘树再入队：`RemoveChild` 是唯一的脱树原语（负责 ForgetSubtree 注销焦点/捕获/hover），
	//    且它会把 `m_removedDuringDispatch` 打好 ⇒ 本次派发栈跳过本节点。
	//    ⚠️ **入队失败（bad_alloc）**：`removed` 在异常展开中析构 ⇒ 对象已脱树并销毁。
	//    此时派发栈持有的 `current` 已失效（本次派发随即异常退出），属「内存耗尽」下的
	//    最后手段——框架其余分配失败同样不吞 `bad_alloc`（口径一致）。
	std::unique_ptr<Widget> removed = m_parent->RemoveChild(this);

	if (!removed){

		return false;   // 防御：父指针与子列表不一致（不应发生）——对象未脱树、也未析构

	}

	ApplicationAccess::DeferDestroy(*app, std::move(removed));

	return true;

}

void Widget::DetachFromParentNow(){

	Widget* parent = m_parent;

	if (parent == nullptr){

		return;   // 根/已脱树：无事可做（调用方已在上游拦根）

	}

	// ★ `res` 是本函数的局部对象：函数返回时析构 ⇒ 调用方（`RequestRemove`）返回后 this 已失效
	std::unique_ptr<Widget> res = parent->RemoveChild(this);

	(void)res;

}

bool Widget::ContainsPoint(int x, int y) const noexcept{

	// 默认命中区域：矩形 [0, width) × [0, height)——判定表达式见 ContainsRect
	// Phase 15：表达式抽到**非虚** ContainsRect——D2 门控与命中判定共用同一份判据
	return ContainsRect(x, y);

}

bool Widget::ContainsRect(int x, int y) const noexcept{

	// 默认命中区域：矩形 [0, width) × [0, height)
	// ⚠️ 用 m_geometry.width（float）而非 GetWidth()（int 截断）——保持与既有判定逐位一致
	return x >= 0 &&y >= 0 &&x < m_geometry.width &&y < m_geometry.height;

}

Widget* Widget::HitTest(int x, int y) noexcept {

	// 不可见的 Widget 不参与命中测试
	if (!IsVisible()){

		return nullptr;

	}

	// 禁用的 Widget 不参与命中测试
	if (!IsEnabled()) {

		return nullptr;

	}

	// Phase 15 D2：裁剪容器门控——**必须在递归子节点之前**（放后面等于没约束）
	// 判据走非虚 ContainsRect（不可用 ContainsPoint：Panel 恒 false 会拦死自身整棵子树）
	if (ClipsChildren() && !ContainsRect(x, y)){

		return nullptr;

	}

	// 逆序遍历子节点（后添加的在上层，优先命中）
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
	{
		Widget* child = it->get();

		// 坐标转换：从当前 Widget 的局部坐标转到子 Widget 的局部坐标
		// Phase 15：**加**回自身内容偏移（与 Paint 的"减"反向同源——详设 §3.2 符号冻结）
		const int localX = x - static_cast<int>(child->m_geometry.x) + GetContentOffsetX();
		const int localY = y - static_cast<int>(child->m_geometry.y) + GetContentOffsetY();

		// 递归检测子节点
		if (Widget* target = child->HitTest(localX, localY))
		{
			return target;
		}
	}

	// 子节点均未命中，检测自身
	if (ContainsPoint(x, y))
	{
		return this;
	}

	return nullptr;

}

void Widget::SetLayout(std::unique_ptr<Layout> layout){

	m_layout = std::move(layout);

	// ★ Phase 41：换布局器 ⇒ **我的子树**必须重算（无条件置脏——连「移除布局」也置，
	//   因为「此前由 Layout 摆出的几何是否仍然合法」无法在 O(1) 内判定）。
	//   ⚠️ 若将来出现「在 Layout::Arrange 内部换布局器」的调用（当前不存在），该写入会被
	//   结算期抑制吞掉 ⇒ 要等下一条失效才结算。
	NotifyGeometryChanged(true);

}

Widget* Widget::GetChildAt(size_t index) noexcept{

	// ★ 审计 L11：Release 下 `FRAMEWORK_ASSERT(index < m_children.size())` 展开为空
	//   ⇒ 直接 `m_children[index]` = **越界访问**（vector operator[] 不查边界）。
	//   与 `Application.cpp:176-178` 对同类场景**保留运行时分支**的做法对齐：
	//   断言留在 Debug（暴露框架自身缺陷），Release 返回 nullptr 让调用方自行收敛
	//   ——不把「契约靠断言维护」押在 Release 上（§7.3 同族）。
	//   ⚠️ 返回值语义变化：越界时由 UB 改为 nullptr。既有调用点全部由 `GetChildCount()`
	//   定界（Layout / Paint / HitTest / CollectFocusables），不受影响。
	if (index >= m_children.size()){
		FRAMEWORK_ASSERT(false);
		return nullptr;
	}

	return m_children[index].get();

}


const Widget* Widget::GetChildAt(size_t index) const noexcept{

	if (index >= m_children.size()){
		FRAMEWORK_ASSERT(false);
		return nullptr;
	}

	return m_children[index].get();

}

void Widget::ArrangeInternal(){

	if (m_layout){

		m_layout->Arrange(*this);

	}


	for (auto& child : m_children){

		child->ArrangeInternal();

	}
}

void Widget::Arrange(){

	Widget* const root = GetRootNode();

	// ★ D41-3 ⓑ：结算期抑制对**整棵树**生效（标志寄存在根）
	++root->m_arrangeDepth;

	ArrangeInternal();

	--root->m_arrangeDepth;

	// ★ D41-3.2：**根** Arrange 完成即清根脏；**子树** Arrange **不清**（兄弟分支可能仍脏）
	//   ⇒ 这是「一次 WM_SIZE 只 Arrange 一次」的唯一成立条件：
	//     `Window::OnResized` 先同步 Arrange（此处清脏）⇒ 随后 `PaintFrame` 看到干净 ⇒ 不再重排。
	//   ⚠️ 第二个条件（**深度已归零**）是**防御性**：嵌套时 `root == this` 本来就为假，
	//   写出来是让「结算完全退出后才消费脏」这条语义在代码里可见。
	if (root == this && root->m_arrangeDepth == 0){

		root->m_layoutDirty = false;

	}

}

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

void Widget::SetVisible(bool v){

	// ★★ 同值守卫（★ 审计 M9 修复的正确性前提，条 134 同族形态）：
	// 本 setter 被 `TextBox::SyncScrollBar` 在 `OnPaint` 起始**每帧**调用（`TextBox.cpp:562`）。
	// 若在此处无条件 `Invalidate()` ⇒ 每帧请求重绘 ⇒ **自激励重绘循环**（永远等不到空闲队列）。
	// ⇒ 「值变化才失效」是**正确性要求**，不是优化。重复 `SetVisible(true)` 零副作用
	//   （同族先例 `TextWidget::SetWordWrap:102-105`）。
	if (m_state.visible == v){
		return;
	}

	m_state.visible = v;

	// ★ 请求重绘：可见性是**绘制门控**（`Widget.cpp:489` 的 Paint 前置 / `:314` 的 HitTest 门控）
	//   ⇒ 状态变化必须让新状态上屏。此前的「裸赋值」与 `SetStyle`/`ApplyTheme`（内建 Invalidate）
	//   不同族 ⇒ 应用侧改可见性常静默不刷新（= 审计 M9 本体）。
	// ★★ Phase 41：它同时是**布局输入**（`Layout` 对不可见子跳过槽位并停泊到 bbox 负区
	//   ——C-VIS-2/4）⇒ 门控 = 父是否有 Layout。这是「SetVisible 之后兄弟不重排」的修复点。
	// ⚠️ 无窗口（无头测试 / 构造期）时 `Invalidate()` 内部上溯取不到 Window ⇒ 自然 no-op。
	NotifyGeometryChanged(ParentHasLayout());

}

void Widget::SetStretch(int stretch) noexcept{

	// ★★ 审计 #63 / §7.3：补 Release 侧钳制（原实现只有断言 ⇒ Release 下负值直落 `m_stretch`）。
	// ★ **选「钳 0」而非「拒绝」**——三条同族先例的形态按语义各取其宜：
	//   `ScrollView::SetScrollStep`（负步长 **return**：该值是"一次滚动多少"，
	//   负步长无替代语义 ⇒ 拒绝保留旧值）；`Layout` 的 spacing/padding（**三元钳 0**：
	//   0 有明确语义="无间隙/无内边距"）；本处的 **stretch** 同样——**0 = 不参与分配**
	//   是有定义、且正确的语义（`GetStretch() > 0` 才是参与条件）。
	//   ⇒ 钳 0 后：既不让负权重污染分配算法，也保持「调用即生效」的 setter 语义。
	// ★ **Debug 侧断言保留**：暴露"传了负值"这一调用方错误（框架自身不会传负值）。
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

Size Widget::GetPreferredSize() const{

	return Size{ static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

}

bool Widget::AutoSize(){

	if (GetStretch() > 0)
		return false;                       // §3.5 条 1：stretch 互斥——调用时判断（SetStretch(0) 后重新生效）

	const Size preferred = GetPreferredSize();
	const int w = static_cast<int>(preferred.width);   // float → int 向零截断（详设 v1.1——v1 无 rounding policy）
	const int h = static_cast<int>(preferred.height);

	if (w == GetWidth() && h == GetHeight())
		return false;                       // 同尺寸 no-op

	SetSize(w, h);                          // 虚分派——TextBox::SetSize 的 EnsureCaretVisible+Invalidate 为 SetSize 既有语义
	return true;

}

void Widget::Paint(PaintContext& ctx,int offsetX,int offsetY){
	
	if (!IsVisible())
		return;   // 9.5 R1：唯一例外路径——不 Push 不 Pop（天然平衡）

	int x = offsetX + static_cast<int>(m_geometry.x);
	int y = offsetY + static_cast<int>(m_geometry.y);

	// Phase 27：视口剔除——`self` 为判据与下方 PushClip 消费的同一份几何
	// （D27-B 单表达式，盯防⑦：出现第二个构造表达式即违规）。
	// 交集宽、高严格 > 0（Rect::Intersects）；false = 整段跳过：
	// PushClip/OnPaint/children/PopClip 均不发生（C27-2），HitTest 不受影响（初设 D27-8）
	const Rect self{ static_cast<float>(x), static_cast<float>(y),
	                   m_geometry.width, m_geometry.height };
	if (!ctx.IsRectVisible(self))
		return;

	// 9.5 R1：自身边界入栈（绝对坐标 = Window 客户区；与 OnPaint 的 x/y 同源——不变量 I3）
	// 嵌套交集：子控件被"父边界 ∩ 自身边界"自动裁剪（后端 IntersectClipRect 天然语义）
	ctx.PushClip(self);

	OnPaint(ctx,x,y);

	// Phase 15：子控件的视觉原点 = 自身视觉位置 − 自身内容偏移
	// 默认偏移 0 ⇒ childOriginX/Y 与 x/y 逐位相同（零回归的结构保证）
	const int childOriginX = x - GetContentOffsetX();
	const int childOriginY = y - GetContentOffsetY();

	for(auto& child : m_children){

		child->Paint(ctx,childOriginX,childOriginY);

	}

	ctx.PopClip();   // 严格配对（不变量 I1：Paint 内无其他 return 路径）

}

// ── 事件处理默认实现（空函数，子类按需 override）────────────

void Widget::OnPaint(PaintContext&,int,int){}

void Widget::OnMouseMove(const MouseMoveEvent&) {}

void Widget::OnMouseButtonDown(const MouseButtonDownEvent&) {}

void Widget::OnMouseButtonUp(const MouseButtonUpEvent&) {}

void Widget::OnMouseWheel(const MouseWheelEvent&) {}

void Widget::OnKeyDown(const KeyDownEvent&) {}

void Widget::OnKeyUp(const KeyUpEvent&) {}

void Widget::OnCharInput(const CharInputEvent&) {}

void Widget::OnTimer(const TimerEvent&) {}

void Widget::OnMouseEnter() {}

void Widget::OnMouseLeave() {}

// ── 交互/重绘（5.4.1）───────────────────────────────

Window* Widget::GetWindow() noexcept{

	Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window;

}

const Window* Widget::GetWindow() const noexcept{

	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window;

}

void Widget::SetWindow(Window* window){

	m_window = window;

}

void Widget::Invalidate(){

	if (Window* window = GetWindow()){

		window->Invalidate();

	}

}

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

	// 脏标志寄存在根 ⇒ 查询必须上溯（与 HasFocus 同型）
	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_layoutDirty;

}

bool Widget::HasFocus() const noexcept{

	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window && root->m_window->GetFocusedWidget() == this;

}

Point Widget::GetAbsolutePosition() const noexcept{

	Point pos{

		static_cast<float>(GetX()),

		static_cast<float>(GetY())

	};

	const Widget* parent = m_parent;

	while (parent){

		// Phase 15：逐层**减去**父的内容偏移（返回视觉位置——详设 §3.2）
		pos.x += static_cast<float>(parent->GetX() - parent->GetContentOffsetX());

		pos.y += static_cast<float>(parent->GetY() - parent->GetContentOffsetY());

		parent = parent->GetParent();

	}

	return pos;

}

}
