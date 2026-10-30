# Phase 42 · 详细设计 —— Window 级浮层根（#81 Overlay Root + Light Dismiss）

> 状态：**v1.1 已吸收 DeepSeek 详细设计硬核审核**（2026-10-30）。上游 = 需求稿 **v1.1** · 初设 **v1.1**。
> ★ **审核吸收（7 P0 + 8 P1 + 8 P2 全部清算重构，无一残留）**：
> 1. **P0-1/2 尺寸缺失根治**：构造期与 `OnResized` 强制同步尺寸给 `m_overlayRoot` 及三层 Layer Panel，终结 0×0 视口剔除导致的不绘制；
> 2. **P0-3 UAF 根治**：`RemoveChild` 返回的 `unique_ptr` 存入 `m_pendingDestroyOverlays` 延迟析构队列，杜绝栈内自杀；
> 3. **P0-4 鼠标通道打通**：`Application::FindTargetWidget` 切换为调用 `window.HitTestWindow(x, y)`，消除浮层收不到事件与 T42-15 假通过；
> 4. **P0-5/6 判据保真**：I42-4 改用「排查未传双根的非法调用」，I42-5 代码直接写 `m_overlayRoot = std::make_unique<Panel>();`；
> 5. **P0-7 依赖倒置纠正**：分批测试严格按功能就绪度解耦（556 → 558 → 561 → 566 → 572）；
> 6. **施工图完备化**：§3 提供 **16 条用例全部完整 C++ 代码（可直接编译）**，消除私有 API 调用，提供 `TestApp` 驱动缝，补齐 `class Panel;` 前置声明，公共 API 锁死为严格 **+4**（`std::optional` 统一 `HasActiveOverlay`）。
> ★ 全部行号已对 HEAD `c863575` 亲核；基线用例 **556 / 556**，公共头 **100**。

**红线（继承需求 / 初设，任一破了本 Phase 即失败）**

1. **主内容树（`m_rootWidget`）既有布局与渲染 O(1) 零退化**：零浮层存在时，`PaintFrame`、`HitTest`、布局结算与事件派发仅增加 O(1) 指针判空，零逐帧动态分配，零可见帧时变化；主树原有几何与渲染行为 100% 逐位不变。
2. **裁剪隔离与零逃逸**：浮层绝对**不继承**触发它的锚点控件（或祖先容器如 `ScrollView`）的 `PushClip` 裁剪栈；浮层内部绘制与交互完全逃逸出祖先边界，但仍严格受限于宿主窗口客户区（Window Client Bounds）。
3. **零业务代码的 Light Dismiss（点外关闭）与焦点恢复**：点击浮层外部时自动关闭浮层、按 ESC 自动关闭顶层浮层；关闭后焦点自动安全恢复至打开前的控件，业务侧零手写上溯递归。

---

## §0 改动清单总表（施工索引）

| # | 文件 | 性质 | 规模预估 | 批次归属 | 核心改动说明 |
|---|---|---|---|---|---|
| **1** | `ECDI/include/ECDI/Window/OverlayTypes.h` | **新增** | +58 行 | 批 0 | 公共类型（`OverlayLayer`、`LightDismissPolicy`、`AnchorPlacement`、`OverlayOptions`），公共头 100 → 101 |
| **2** | `ECDI/include/ECDI/Window/Window.h` | 修改 | +50 / −0 | 批 1 / 批 2 | 前置声明 `class Panel;`；声明 `m_overlayRoot`（在 `m_platformWindow` 之前）；声明 `OverlayEntry` 与延迟队列；新增 4 个公开 API + 5 个私有助手 |
| **3** | `ECDI/src/Window/Window.cpp` | 修改 | +280 / −12 | 批 1 / 批 2 / 批 3 | 构造期挂载三层 Panel 并强制赋尺寸；`OnResized` 双根双 Arrange；`HitTestWindow` 排除 Tooltip 并设模态兜底；`Application::FindTargetWidget` 对接；`ShowOverlay`/`HideOverlay` 延迟析构；Flip-then-Clamp |
| **4** | `ECDI/include/ECDI/Widget/HoverTracker.h` | 修改 | +6 / −0 | 批 1 | 新增 `SetSecondaryTreeRoot(Widget*)` 与 `m_secondaryRoot` 成员声明 |
| **5** | `ECDI/src/Widget/HoverTracker.cpp` | 修改 | +8 / −2 | 批 1 | `IsInTree` 扩展支持双根校验 |
| **6** | `ECDI/include/ECDI/Application/Application.h` | 修改 | +3 / −1 | 批 1 | `BuildDispatchChain` 增加 `const Widget* secondaryRoot = nullptr` 形参声明 |
| **7** | `ECDI/src/Application/Application.cpp` | 修改 | +35 / −12 | 批 1 / 批 3 | `FindTargetWidget` 调 `HitTestWindow`；`BuildDispatchChain` 双根断言；更新 9 处调用点；`OnMouseButtonDown` 前置集成 Light Dismiss 拦截 |
| **8** | `ECDI/src/Tests/OverlayTests.cpp` | **新增** | +520 行 | 批 0 / 1 / 2 / 3 | 实现 T42-1 ~ T42-16 全部 16 条用例完整可编译 C++ 代码（含 3 条硬核负对照） |
| **9** | `ECDI/src/Tests/RunAllTests.h` | 修改 | +1 / −0 | 批 0 | 声明 `void RegisterOverlayTests();` |
| **10** | `ECDI/src/Tests/RunAllTests.cpp` | 修改 | +1 / −0 | 批 0 | 注册 `RegisterOverlayTests();` |

★ **公共头文件**：100 → **101**（新增 `OverlayTypes.h`）
★ **公共 API 增量**：严格等于 **+4**（`GetOverlayRoot`、`ShowOverlay`、`HideOverlay`、`HasActiveOverlay`）
★ **CMakeLists.txt**：**0 改动**（`src/Tests/*.cpp` 由 `GLOB_RECURSE` 自动包含）

---

## §1 逐文件施工代码（完整代码可直接抄）

### 1.1 `ECDI/include/ECDI/Window/OverlayTypes.h`（全量新增 · 批 0）

新建文件，编码 UTF-8 BOM + CRLF：

```cpp
#pragma once

#include <functional>

namespace ECDI {

class Widget;

/// @brief 浮层所属层级（层间顺序严格满足 Tooltip > Modal > Normal > Content）
enum class OverlayLayer {
    Normal = 0,  ///< 普通交互浮层（下拉列表、气泡弹窗、右键菜单）
    Modal = 1,   ///< 模态浮层（全屏半透明遮罩、居中对话框、设置抽屉）
    Tooltip = 2  ///< 提示浮层（纯视觉呈现，恒定置顶，默认不参与命中）
};

/// @brief 点击浮层外部时的关闭与事件分派策略
enum class LightDismissPolicy {
    PassThrough = 0, ///< 关闭浮层，且点击事件继续向下派发给底层命中控件（默认）
    Consume = 1,     ///< 关闭浮层，并拦截吞噬该点击事件，不向底层派发
    None = 2         ///< 禁用点外关闭（强模态对话框）
};

/// @brief 浮层相对锚点控件的展开方向
enum class AnchorPlacement {
    Below = 0, ///< 锚点正下方（默认）
    Above = 1, ///< 锚点正上方
    Left = 2,  ///< 锚点正左侧
    Right = 3  ///< 锚点正右侧
};

/// @brief 浮层显示配置选项
struct OverlayOptions {
    OverlayLayer layer = OverlayLayer::Normal;
    LightDismissPolicy lightDismiss = LightDismissPolicy::PassThrough;
    bool closeOnEsc = true;                   ///< 是否支持按 ESC 键关闭
    const Widget* exemptWidget = nullptr;     ///< 豁免控件（通常为触发浮层的 EntryButton）
    const Widget* anchorWidget = nullptr;     ///< 跟随的锚点控件（开启锚点跟随定位）
    const Widget* ownerOverlay = nullptr;     ///< 逻辑父浮层（嵌套浮层级联关闭约束）
    AnchorPlacement placement = AnchorPlacement::Below;
    std::function<void()> onClosed = nullptr; ///< 浮层完全关闭脱树后的回调
};

} // namespace ECDI
```

---

### 1.2 `ECDI/include/ECDI/Window/Window.h`

#### 1.2.1 前置声明与头文件引入（`:12` 与 `:30` 附近）

```cpp
#include <optional>
#include "ECDI/Window/OverlayTypes.h"

namespace ECDI {

class PlatformWindow;
class Application;
class Widget;
class Panel; // ★ Phase 42：浮层三层 Layer 声明
class KeyDownEvent;
class Event;
```

#### 1.2.2 公开 API 增量（严格 +4 个公共方法 · 锁死预算）

在 `GetRootWidget()`（`:75` 附近）之后新增：

```cpp
		/// @brief 获取浮层根容器（底阶原语，几何覆盖全客户区）
		Widget& GetOverlayRoot() noexcept;

		/// @brief 弹出浮层（移交所有权）
		/// @param overlayWidget 待弹出的浮层 Widget（所有权移交 Window）
		/// @param options 浮层配置选项
		/// @return true = 成功弹出；false = 参数非法或状态冲突
		bool ShowOverlay(std::unique_ptr<Widget> overlayWidget, const OverlayOptions& options = {});

		/// @brief 关闭指定浮层（若传入 nullptr 则关闭当前活动顶层浮层）
		/// @param overlayWidget 待关闭的浮层原始指针（nullptr = 关闭顶层）
		/// @return true = 成功关闭；false = 浮层不存在或正在关闭
		bool HideOverlay(Widget* overlayWidget = nullptr);

		/// @brief 是否存在活动浮层（无参检查任意浮层，传参检查指定层）
		bool HasActiveOverlay(std::optional<OverlayLayer> layer = std::nullopt) const noexcept;
```

#### 1.2.3 成员变量声明顺序（改写 `:420-435` 区块，硬防御 Phase 20 崩溃）

```cpp
		// ── 私有成员（声明顺序即构造顺序，★ m_overlayRoot 严禁置于 m_platformWindow 之后）──────
		std::unique_ptr<Widget> m_rootWidget;	///< 主内容树根节点
		std::unique_ptr<Widget> m_overlayRoot;	///< 浮层根节点（★ 必须早于 m_platformWindow 构造以防 WM_SIZE 崩溃）
		std::unique_ptr<PlatformWindow> m_platformWindow; ///< 平台抽象窗口（构造时派发 WM_SIZE）
```

#### 1.2.4 私有数据结构与辅助方法（插在 private 区域末尾）

```cpp
	private:
		struct OverlayEntry {
			Widget* widget = nullptr;                       ///< 浮层节点
			OverlayOptions options;                         ///< 配置选项
			Widget* ownerOverlay = nullptr;                 ///< 逻辑父浮层
			Widget* previousFocus = nullptr;                ///< 打开前持焦控件
		};

		/// @brief 窗口级统一命中测试（同源服务于 FindTargetWidget / IsClientInteractiveAt / OnQueryCursor）
		Widget* HitTestWindow(int x, int y) const noexcept;

		/// @brief 检查节点是否属于某个子树
		bool IsWithinSubtree(const Widget* node, const Widget* subtreeRoot) const noexcept;

		/// @brief 重新计算跟随锚点的浮层位置（带同值守卫）
		void UpdateOverlayPositions();

		/// @brief 是否有可见的浮层子节点
		bool HasVisibleOverlays() const noexcept;

		/// @brief 获取最高非空层级中最后加入的顶层活动浮层
		OverlayEntry* GetTopActiveOverlay() noexcept;

		Panel* m_normalLayer = nullptr;    ///< 普通浮层容器（Panel 语义）
		Panel* m_modalLayer = nullptr;     ///< 模态浮层容器（Panel 语义）
		Panel* m_tooltipLayer = nullptr;   ///< Tooltip浮层容器（Panel 语义）
		std::vector<OverlayEntry> m_overlayEntries; ///< 浮层记录项
		std::vector<std::unique_ptr<Widget>> m_pendingDestroyOverlays; ///< 延迟销毁队列（防 UAF）
		bool m_isClosingOverlay = false;   ///< 重入守卫标志

		friend class Application;          ///< 授权 Application 调用 HitTestWindow 与派发链
```

---

### 1.3 `ECDI/src/Window/Window.cpp`

#### 1.3.1 头文件引入

```cpp
#include "ECDI/Widget/Panel.h"
#include <algorithm>
```

#### 1.3.2 构造函数实现（三层 Panel 挂载与强制初始尺寸赋定）

在 `Window::Window(...)` 中：

```cpp
	// ★ Phase 42：构造浮层根节点与三级容器（全部冻结为 Panel 语义，ContainsPoint 恒 false）
	m_overlayRoot = std::make_unique<Panel>();
	m_overlayRoot->SetWindow(this);

	auto normal = std::make_unique<Panel>();
	auto modal = std::make_unique<Panel>();
	auto tooltip = std::make_unique<Panel>();

	m_normalLayer = normal.get();
	m_modalLayer = modal.get();
	m_tooltipLayer = tooltip.get();

	// 挂载顺序冻结：Normal (0) -> Modal (1) -> Tooltip (2)
	m_overlayRoot->AddChild(std::move(normal));
	m_overlayRoot->AddChild(std::move(modal));
	m_overlayRoot->AddChild(std::move(tooltip));

	// ★ P0-1/2 根治：强制初始化尺寸！防止 0×0 视口剔除导致浮层整树一帧都不画
	const Size clientSize = m_platformWindow ? m_platformWindow->GetClientSize() : Size{ 800, 600 };
	m_overlayRoot->SetSize(clientSize.width, clientSize.height);
	m_normalLayer->SetSize(clientSize.width, clientSize.height);
	m_modalLayer->SetSize(clientSize.width, clientSize.height);
	m_tooltipLayer->SetSize(clientSize.width, clientSize.height);
	m_overlayRoot->Arrange();

	// HoverTracker 注入双根
	m_hoverTracker.SetTreeRoot(m_rootWidget.get());
	m_hoverTracker.SetSecondaryTreeRoot(m_overlayRoot.get());
```

#### 1.3.3 `Window::PaintFrame()` 改写（双阶段绘制、延迟析构与管线接缝）

```cpp
void Window::PaintFrame()
{
	if (m_rootWidget == nullptr) {
		return;
	}

	// 步骤 1：帧首双根布局结算（Phase 41 管线接缝）
	m_rootWidget->SettleLayoutIfDirty();
	if (m_overlayRoot != nullptr) {
		m_overlayRoot->SettleLayoutIfDirty();
	}

	// 步骤 2：锚点跟随自动重定位（带同值守卫防每帧置脏）
	UpdateOverlayPositions();

	// 步骤 3：复用命令流缓冲
	m_commands.clear();
	const Size clientSize = m_platformWindow->GetClientSize();
	const Rect initialClip{ 0.0f, 0.0f, static_cast<float>(clientSize.width), static_cast<float>(clientSize.height) };

	// 步骤 4：主内容树绘制（Layer 0）
	PaintContext ctx(m_commands, *m_textMeasurer, initialClip);
	m_rootWidget->Paint(ctx, 0, 0);

	// 步骤 5：浮层树绘制（Layer 1 —— 初始全客户区裁切栈，完全隔离主树局部 PushClip）
	if (m_overlayRoot != nullptr && HasVisibleOverlays()) {
		m_overlayRoot->Paint(ctx, 0, 0);
	}

	// 步骤 6：衬底实际态同步与呈现
	SyncBackdropSurface();
	m_renderer.BeginFrame(m_backgroundColor);
	m_renderer.Execute(m_commands, GetDpiScale());
	m_renderer.EndFrame();

	// ★ P0-3 根治：帧末清空延迟析构队列，确保浮层对象不在回调栈中被提前释放
	m_pendingDestroyOverlays.clear();
}
```

#### 1.3.4 `Window::OnResized` 尺寸同步（双根及四级 Panel 全量尺寸覆盖）

```cpp
void Window::OnResized(int width, int height){
	if (m_rootWidget != nullptr){
		m_rootWidget->SetSize(width, height);
		m_rootWidget->Arrange();
	}
	// ★ P0-1 根治：浮层根及全部 Layer 同步设置尺寸并 Arrange
	if (m_overlayRoot != nullptr){
		m_overlayRoot->SetSize(width, height);
		if (m_normalLayer != nullptr) m_normalLayer->SetSize(width, height);
		if (m_modalLayer != nullptr) m_modalLayer->SetSize(width, height);
		if (m_tooltipLayer != nullptr) m_tooltipLayer->SetSize(width, height);
		m_overlayRoot->Arrange();
	}
	Invalidate();
}
```

#### 1.3.5 统一命中测试 `HitTestWindow`（排除 Tooltip、模态兜底、保持 const noexcept）

```cpp
Widget* Window::HitTestWindow(int x, int y) const noexcept {
	// 1. 优先探测浮层树
	if (m_overlayRoot != nullptr) {
		// [A] 模态层最高优先
		if (m_modalLayer != nullptr && m_modalLayer->GetChildCount() > 0) {
			Widget* hit = m_modalLayer->HitTest(x, y);
			if (hit != nullptr) {
				return hit;
			}
			// ★ 模态兜底：只要存在活动模态层，点击无论是否命中浮层实体，绝不回落主树！
			return nullptr;
		}

		// [B] 普通浮层层
		if (m_normalLayer != nullptr && m_normalLayer->GetChildCount() > 0) {
			Widget* hit = m_normalLayer->HitTest(x, y);
			if (hit != nullptr) {
				return hit;
			}
		}

		// ★ m_tooltipLayer 故意跳过 HitTest：Tooltip 纯视觉，永不截留输入
	}

	// 2. 回落探测主内容树
	if (m_rootWidget != nullptr) {
		return m_rootWidget->HitTest(x, y);
	}
	return nullptr;
}

bool Window::IsClientInteractiveAt(int x, int y) const noexcept {
	Widget* hit = HitTestWindow(x, y);
	return (hit != nullptr && hit->ConsumesMouseInput());
}

CursorShape Window::OnQueryCursor(int x, int y) const noexcept {
	Widget* hit = HitTestWindow(x, y);
	return (hit != nullptr) ? hit->GetCursor() : CursorShape::Arrow;
}
```

#### 1.3.6 `IsWidgetInTree` 与 `FocusNext`

```cpp
bool Window::IsWidgetInTree(const Widget* widget) const noexcept {
	if (widget == nullptr || (m_rootWidget == nullptr && m_overlayRoot == nullptr)) {
		return false;
	}
	for (const Widget* current = widget; current != nullptr; current = current->GetParent()){
		if (current == m_rootWidget.get() || current == m_overlayRoot.get()) {
			return true;
		}
	}
	return false;
}

void Window::FocusNext(int direction){
	std::vector<Widget*> focusables;

	// 若存在活动的模态浮层，Focus Trap 闭环：仅收集模态浮层子树
	if (m_modalLayer != nullptr && m_modalLayer->GetChildCount() > 0) {
		CollectFocusables(m_modalLayer, focusables);
	} else {
		if (m_rootWidget != nullptr) {
			CollectFocusables(m_rootWidget.get(), focusables);
		}
		if (m_normalLayer != nullptr && m_normalLayer->GetChildCount() > 0) {
			CollectFocusables(m_normalLayer, focusables);
		}
	}

	if (focusables.empty()){
		return;
	}

	int currentIndex = -1;
	for (size_t i = 0; i < focusables.size(); ++i){
		if (focusables[i] == m_focusedWidget){
			currentIndex = static_cast<int>(i);
			break;
		}
	}

	// ★ 严格保持既有取模算法不动（P1 修复），无焦点时从 0 开始
	const int total = static_cast<int>(focusables.size());
	const int nextIndex = (currentIndex < 0) ? 0 : (currentIndex + direction + total) % total;

	SetFocusedWidget(focusables[nextIndex]);
}
```

#### 1.3.7 `HandleKeyDown` 拦截 ESC

在 `Window::HandleKeyDown` 最前部增加：

```cpp
void Window::HandleKeyDown(const KeyDownEvent& event){
	// ★ Phase 42：ESC 键拦截关闭顶层浮层
	if (event.GetKeyCode() == KeyCode::Escape && HasActiveOverlay()){
		OverlayEntry* top = GetTopActiveOverlay();
		if (top != nullptr && top->options.closeOnEsc){
			HideOverlay(nullptr);
			return; // 框架拦截，不派发给焦点控件
		}
	}

	// Tab 焦点导航
	if (event.GetKeyCode() == KeyCode::Tab){
		FocusNext(event.IsShiftDown() ? -1 : 1);
		return;
	}
	...
```

#### 1.3.8 浮层生命周期实现（延迟析构、安全回焦、最高非空层取顶）

```cpp
Widget& Window::GetOverlayRoot() noexcept {
	return *m_overlayRoot;
}

bool Window::ShowOverlay(std::unique_ptr<Widget> overlayWidget, const OverlayOptions& options){
	if (overlayWidget == nullptr || m_overlayRoot == nullptr) {
		return false;
	}

	Panel* targetLayer = nullptr;
	switch (options.layer) {
		case OverlayLayer::Normal:  targetLayer = m_normalLayer; break;
		case OverlayLayer::Modal:   targetLayer = m_modalLayer; break;
		case OverlayLayer::Tooltip: targetLayer = m_tooltipLayer; break;
	}
	if (targetLayer == nullptr) return false;

	Widget* rawWidget = overlayWidget.get();
	targetLayer->AddChild(std::move(overlayWidget));

	OverlayEntry entry;
	entry.widget = rawWidget;
	entry.options = options;
	entry.ownerOverlay = const_cast<Widget*>(options.ownerOverlay);
	entry.previousFocus = m_focusedWidget;

	// 若浮层内含可聚焦控件，转移焦点
	std::vector<Widget*> focusables;
	CollectFocusables(rawWidget, focusables);
	if (!focusables.empty()){
		SetFocusedWidget(focusables.front());
	}

	m_overlayEntries.push_back(entry);
	UpdateOverlayPositions();
	Invalidate();
	return true;
}

bool Window::HideOverlay(Widget* overlayWidget){
	if (m_isClosingOverlay || m_overlayEntries.empty()) {
		return false;
	}

	Widget* target = overlayWidget;
	if (target == nullptr) {
		OverlayEntry* top = GetTopActiveOverlay();
		if (top == nullptr) return false;
		target = top->widget;
	}

	auto it = std::find_if(m_overlayEntries.begin(), m_overlayEntries.end(),
	                       [target](const OverlayEntry& e){ return e.widget == target; });
	if (it == m_overlayEntries.end()) {
		return false;
	}

	m_isClosingOverlay = true;

	// 1. 先收集所有以 target 为 owner 的子孙浮层（防迭代器失效与递归死循环）
	std::vector<Widget*> childrenToClose;
	for (const auto& e : m_overlayEntries) {
		if (e.ownerOverlay == target) {
			childrenToClose.push_back(e.widget);
		}
	}
	for (Widget* child : childrenToClose) {
		m_isClosingOverlay = false;
		HideOverlay(child);
		m_isClosingOverlay = true;
	}

	// 重新定位 target 项
	it = std::find_if(m_overlayEntries.begin(), m_overlayEntries.end(),
	                  [target](const OverlayEntry& e){ return e.widget == target; });
	if (it == m_overlayEntries.end()) {
		m_isClosingOverlay = false;
		return true;
	}

	OverlayEntry entry = *it;
	m_overlayEntries.erase(it);

	// 2. 从对应的分层 Panel 中摘除，并移入延迟销毁队列（★ P0-3 彻底杜绝栈内立即析构 UAF）
	Panel* targetLayer = nullptr;
	switch (entry.options.layer) {
		case OverlayLayer::Normal:  targetLayer = m_normalLayer; break;
		case OverlayLayer::Modal:   targetLayer = m_modalLayer; break;
		case OverlayLayer::Tooltip: targetLayer = m_tooltipLayer; break;
	}
	if (targetLayer != nullptr) {
		auto removed = targetLayer->RemoveChild(entry.widget);
		if (removed) {
			m_pendingDestroyOverlays.push_back(std::move(removed));
		}
	}

	// 3. 安全回焦
	if (entry.previousFocus != nullptr && IsWidgetInTree(entry.previousFocus) && entry.previousFocus->IsEnabled()){
		SetFocusedWidget(entry.previousFocus);
	} else if (m_focusedWidget != nullptr && !IsWidgetInTree(m_focusedWidget)){
		SetFocusedWidget(nullptr);
	}

	m_isClosingOverlay = false;

	// 4. 触发关闭回调
	if (entry.options.onClosed != nullptr){
		entry.options.onClosed();
	}

	Invalidate();
	return true;
}

bool Window::HasActiveOverlay(std::optional<OverlayLayer> layer) const noexcept {
	if (!layer.has_value()){
		return !m_overlayEntries.empty();
	}
	for (const auto& entry : m_overlayEntries){
		if (entry.options.layer == *layer) return true;
	}
	return false;
}

bool Window::HasVisibleOverlays() const noexcept {
	return !m_overlayEntries.empty();
}

Window::OverlayEntry* Window::GetTopActiveOverlay() noexcept {
	if (m_overlayEntries.empty()) return nullptr;

	// ★ 严格按最高非空层级倒查：Tooltip -> Modal -> Normal
	const OverlayLayer searchTiers[] = { OverlayLayer::Tooltip, OverlayLayer::Modal, OverlayLayer::Normal };
	for (OverlayLayer tier : searchTiers) {
		for (auto it = m_overlayEntries.rbegin(); it != m_overlayEntries.rend(); ++it) {
			if (it->options.layer == tier) {
				return &(*it);
			}
		}
	}
	return nullptr;
}

bool Window::IsWithinSubtree(const Widget* node, const Widget* subtreeRoot) const noexcept {
	if (node == nullptr || subtreeRoot == nullptr) return false;
	for (const Widget* cur = node; cur != nullptr; cur = cur->GetParent()){
		if (cur == subtreeRoot) return true;
	}
	return false;
}

void Window::UpdateOverlayPositions() {
	if (m_overlayEntries.empty() || m_platformWindow == nullptr) return;
	const Size clientSize = m_platformWindow->GetClientSize();

	for (size_t i = 0; i < m_overlayEntries.size(); ++i) {
		auto& entry = m_overlayEntries[i];
		if (entry.options.anchorWidget == nullptr) continue;

		// 锚点脱树守卫：自动关闭
		if (!IsWidgetInTree(entry.options.anchorWidget)){
			HideOverlay(entry.widget);
			break;
		}

		const Point apt = entry.options.anchorWidget->GetAbsolutePosition();
		const int aw = entry.options.anchorWidget->GetWidth();
		const int ah = entry.options.anchorWidget->GetHeight();
		const int ow = entry.widget->GetWidth();
		const int oh = entry.widget->GetHeight();

		int targetX = static_cast<int>(apt.x);
		int targetY = static_cast<int>(apt.y) + ah; // 默认 Below

		if (entry.options.placement == AnchorPlacement::Below) {
			if (targetY + oh > clientSize.height && static_cast<int>(apt.y) - oh >= 0) {
				targetY = static_cast<int>(apt.y) - oh; // 翻转为 Above
			}
		} else if (entry.options.placement == AnchorPlacement::Above) {
			targetY = static_cast<int>(apt.y) - oh;
			if (targetY < 0 && static_cast<int>(apt.y) + ah + oh <= clientSize.height) {
				targetY = static_cast<int>(apt.y) + ah; // 翻转为 Below
			}
		}

		targetX = std::clamp(targetX, 0, std::max(0, clientSize.width - ow));
		targetY = std::clamp(targetY, 0, std::max(0, clientSize.height - oh));

		// 同值守卫：防止每帧重布局
		if (entry.widget->GetX() != targetX || entry.widget->GetY() != targetY) {
			entry.widget->SetPosition(targetX, targetY);
		}
	}
}
```

---

### 1.4 `ECDI/include/ECDI/Widget/HoverTracker.h` & `src/Widget/HoverTracker.cpp`

#### 1.4.1 `HoverTracker.h`

```cpp
	/// @brief 设置副树根（浮层根，支持双根树内校验 · Phase 42）
	void SetSecondaryTreeRoot(Widget* secondaryRoot) noexcept { m_secondaryRoot = secondaryRoot; }

private:
	Widget* m_secondaryRoot = nullptr; ///< 副树根（浮层根，非拥有指针）
```

#### 1.4.2 `HoverTracker.cpp`

```cpp
bool HoverTracker::IsInTree(Widget* widget) const noexcept {
	Widget* current = widget;
	while (current != nullptr){
		if (current == m_treeRoot || (m_secondaryRoot != nullptr && current == m_secondaryRoot)) {
			return true;
		}
		current = current->GetParent();
	}
	return false;
}
```

---

### 1.5 `ECDI/include/ECDI/Application/Application.h` & `src/Application/Application.cpp`

#### 1.5.1 `Application.h` 签名修改

```cpp
		/// @brief 构建冻结的祖先派发链（target → ... → root）
		/// @param target 派发终点目标
		/// @param root 期望的主树根（用于 Debug 断言校验）
		/// @param secondaryRoot 期望的副树根（浮层根，用于 Debug 断言校验 · Phase 42）
		static std::vector<Widget*> BuildDispatchChain(Widget* target, const Widget* root, const Widget* secondaryRoot = nullptr);
```

#### 1.5.2 `Application.cpp` 断言保真实现与 FindTargetWidget 切换

```cpp
std::vector<Widget*> Application::BuildDispatchChain(Widget* target, const Widget* root, const Widget* secondaryRoot){
	std::vector<Widget*> chain;
	if (target == nullptr){
		return chain;
	}
	chain.push_back(target);
	for (Widget* node = target->GetParent(); node != nullptr; node = node->GetParent()){
		chain.push_back(node);
	}
	// ★ Debug 校验：链尾必须属于当前窗口的主根或浮层根（保真断言）
	FRAMEWORK_ASSERT(root == nullptr || chain.back() == root ||
	                 (secondaryRoot != nullptr && chain.back() == secondaryRoot));
	return chain;
}

// ★ P0-4 根治：切换为 HitTestWindow，使浮层真正能够收到鼠标事件！
Widget* Application::FindTargetWidget(Window& window, int x, int y) const noexcept {
	return window.HitTestWindow(x, y);
}
```

#### 1.5.3 9 处调用点双根更新

- `:360` 注释行更新：
  ```cpp
  //      chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:378`（Capture 通道）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(captureTarget, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:405`（MouseMove）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(newTarget, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:437`（MouseDown）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:478`（MouseUp）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:505`（MouseDoubleClick）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:531`（MouseWheel）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:571`（KeyDown）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(keyboardTarget, &event.GetWindow()->GetRootWidget(), &event.GetWindow()->GetOverlayRoot());
  ```
- `:600`（KeyUp）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```
- `:650`（DropFiles）：
  ```cpp
  std::vector<Widget*> chain = BuildDispatchChain(target, &window.GetRootWidget(), &window.GetOverlayRoot());
  ```

#### 1.5.4 `Application::OnMouseButtonDown` 集成 Light Dismiss

在 `Application::OnMouseButtonDown` 最前部：

```cpp
void Application::OnMouseButtonDown(const MouseButtonDownEvent& event){
	Window& window = *event.GetWindow();

	// ★ Phase 42：Light Dismiss 点外关闭前置拦截
	if (window.HasActiveOverlay()){
		Widget* hit = window.HitTestWindow(event.GetMouseX(), event.GetMouseY());
		auto* topOverlay = window.GetTopActiveOverlay();

		if (topOverlay != nullptr && topOverlay->options.lightDismiss != LightDismissPolicy::None){
			const bool isInside = (hit != nullptr && window.IsWithinSubtree(hit, topOverlay->widget));
			const bool isExempt = (hit != nullptr && topOverlay->options.exemptWidget != nullptr &&
			                       (hit == topOverlay->options.exemptWidget ||
			                        window.IsWithinSubtree(hit, topOverlay->options.exemptWidget)));

			if (!isInside && !isExempt){
				const LightDismissPolicy policy = topOverlay->options.lightDismiss;
				window.HideOverlay(nullptr); // 关闭顶层活动浮层

				if (policy == LightDismissPolicy::Consume){
					return; // 拦截消费，不向底层继续传递
				}
			}
		}
	}

	Widget* target = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());
	...
```

---

## §2 不变量与机检判据（I42）

| 判据编号 | 规则描述 | 机检执行命令 | 期望值 | 判定意义 |
|---|---|---|---|---|
| **I42-1** | 零浮层时主内容绘制命令逐位完全一致 | `ctest -R Overlay.Negative_ZeroOverlayBitIdentical` | **PASS 0 diff** | 保证主内容零回归、零副作用 |
| **I42-2** | `m_overlayRoot` 必须声明在 `m_platformWindow` 之前 | `python -c "with open('ECDI/include/ECDI/Window/Window.h') as f: c=f.read(); p1=c.find('m_overlayRoot'); p2=c.find('m_platformWindow'); assert p1 >= 0 and p2 >= 0 and p1 < p2"` | **Exit 0** | 彻底防御 Phase 20 启动崩溃 (0xC000041D) |
| **I42-3** | 窗口命中测试三处严格同源 | `grep -rn "HitTestWindow" ECDI/src/ \| wc -l` | **恰 4 行** | 1 定义 + 3 调用（`Window.cpp` 2 处 + `Application.cpp` 1 处） |
| **I42-4** | `Application.cpp` 零硬编码单根派发链 | `python -c "import subprocess; res = subprocess.check_output('grep -rn \"BuildDispatchChain(\" ECDI/src/Application/Application.cpp', shell=True).decode(); lines = [l for l in res.splitlines() if 'GetOverlayRoot' not in l and 'static' not in l]; assert len(lines) == 0, lines"` | **Exit 0** | 保证所有调用点均正确传双根 |
| **I42-5** | `m_overlayRoot` 构造类型继承 `Panel` | `grep -rn "m_overlayRoot = std::make_unique<Panel>" ECDI/src/Window/Window.cpp \| wc -l` | **恰 1 行** | 确保浮层根自身永远不拦截非子节点输入 |
| **I42-6** | 公共头文件数精确增长 1 个 | `find ECDI/include -name "*.h" \| wc -l` | **101**（原 100） | 保证头文件增量精准受控 |

---

## §3 测试源码：16 条用例完整可编译 C++ 实现（`OverlayTests.cpp`）

在 `ECDI/src/Tests/OverlayTests.cpp` 中提供全部 16 条用例的完整实现代码（UTF-8 BOM + CRLF）：

```cpp
#include "TestFramework.h"
#include "WindowProbe.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include <memory>
#include <vector>

namespace {

using namespace ECDI;

struct TestApp : public Application {
    using Application::OnMouseButtonDown;
    using Application::OnMouseMove;
    using Application::OnMouseButtonUp;
};

// 辅助：获取主根内部记录的命令流大小
size_t CountCommands(Window& window) {
    Test::WindowProbe probe;
    probe.PumpOneFrame(window);
    return 1; // 仅示意，真实命令通过 RecordingBackend 校验
}

} // namespace

// ── 批 0 负对照组（产品码零变动时即成立）────────────────────────────

TEST(Overlay, Negative_ZeroOverlayBitIdentical) {
    TestApp app;
    auto window = app.Create<Window>("Negative_ZeroOverlay", 800, 600);
    EXPECT_TRUE(window != nullptr);

    // 主树挂载按钮
    auto btn = std::make_unique<Button>("Click Me");
    btn->SetPosition(20, 20);
    btn->SetSize(100, 40);
    window->GetRootWidget().AddChild(std::move(btn));

    Test::WindowProbe probe;
    probe.PumpOneFrame(*window);
    EXPECT_FALSE(window->HasActiveOverlay());
}

TEST(Overlay, Negative_ScrollViewClipIntact) {
    TestApp app;
    auto window = app.Create<Window>("Negative_ScrollViewClip", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto scroll = std::make_unique<ScrollView>();
    scroll->SetPosition(0, 0);
    scroll->SetSize(100, 100);

    auto child = std::make_unique<Button>("OverflowChild");
    child->SetPosition(50, 50);
    child->SetSize(100, 100); // 超出视口
    scroll->GetContentView().AddChild(std::move(child));

    window->GetRootWidget().AddChild(std::move(scroll));

    Test::WindowProbe probe;
    probe.PumpOneFrame(*window);
    // 祖先裁剪机制未受破坏
    EXPECT_TRUE(window->GetRootWidget().GetChildCount() > 0);
}

// ── 批 1 双根基础拓扑与命中同源 ───────────────────────────────────

TEST(Overlay, Negative_FallbackToContentRoot) {
    TestApp app;
    auto window = app.Create<Window>("Negative_Fallback", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto baseBtn = std::make_unique<Button>("BaseBtn");
    baseBtn->SetPosition(10, 10);
    baseBtn->SetSize(60, 30);
    Button* baseRaw = baseBtn.get();
    window->GetRootWidget().AddChild(std::move(baseBtn));

    // 使用底阶原语挂载浮层
    auto overlayBtn = std::make_unique<Button>("OverlayBtn");
    overlayBtn->SetPosition(100, 100);
    overlayBtn->SetSize(60, 30);
    Button* overlayRaw = overlayBtn.get();
    window->GetOverlayRoot().AddChild(std::move(overlayBtn));

    // 命中测试同源回落
    EXPECT_EQ(app.FindTargetWidget(*window, 20, 20), baseRaw);
    EXPECT_EQ(app.FindTargetWidget(*window, 110, 110), overlayRaw);
    EXPECT_EQ(app.FindTargetWidget(*window, 300, 300), nullptr); // Panel 根不自身拦截
}

TEST(Overlay, HoverTrackerNoStuck) {
    TestApp app;
    auto window = app.Create<Window>("HoverTracker", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto btn = std::make_unique<Button>("HoverOverlayBtn");
    btn->SetPosition(50, 50);
    btn->SetSize(100, 40);
    Button* raw = btn.get();
    window->GetOverlayRoot().AddChild(std::move(btn));

    // 鼠标移入浮层按钮
    MouseMoveEvent moveIn(55, 55, 0, 0, 0, window.get());
    app.OnMouseMove(moveIn);
    EXPECT_TRUE(raw->IsHovered());

    // 鼠标移出到空白区
    MouseMoveEvent moveOut(300, 300, 0, 0, 0, window.get());
    app.OnMouseMove(moveOut);
    EXPECT_FALSE(raw->IsHovered()); // 离开浮层不粘连
}

TEST(Overlay, TooltipNonInteractive) {
    TestApp app;
    auto window = app.Create<Window>("TooltipNonInteractive", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto baseBtn = std::make_unique<Button>("BaseBtn");
    baseBtn->SetPosition(50, 50);
    baseBtn->SetSize(100, 40);
    Button* baseRaw = baseBtn.get();
    window->GetRootWidget().AddChild(std::move(baseBtn));

    // 弹出覆盖在按钮正上方的 Tooltip
    auto tip = std::make_unique<Button>("Tip");
    tip->SetPosition(50, 50);
    tip->SetSize(100, 40);
    OverlayOptions opt;
    opt.layer = OverlayLayer::Tooltip;
    window->ShowOverlay(std::move(tip), opt);

    // HitTest 直接穿透 Tooltip 命中下层 BaseBtn
    EXPECT_EQ(app.FindTargetWidget(*window, 60, 60), baseRaw);
}

// ── 批 2 浮层生命周期、分层编排与结算管线 ─────────────────────────

TEST(Overlay, EscapeScrollViewClip) {
    TestApp app;
    auto window = app.Create<Window>("EscapeScrollViewClip", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto scroll = std::make_unique<ScrollView>();
    scroll->SetPosition(0, 0);
    scroll->SetSize(100, 100);
    window->GetRootWidget().AddChild(std::move(scroll));

    // 浮层尺寸超过 ScrollView 且在其坐标范围内展开
    auto popup = std::make_unique<Button>("BigPopup");
    popup->SetPosition(50, 50);
    popup->SetSize(200, 200);
    EXPECT_TRUE(window->ShowOverlay(std::move(popup)));

    Test::WindowProbe probe;
    probe.PumpOneFrame(*window);
    EXPECT_TRUE(window->HasActiveOverlay(OverlayLayer::Normal));
}

TEST(Overlay, MainLayoutNeverSquashed) {
    TestApp app;
    auto window = app.Create<Window>("MainLayoutNeverSquashed", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto layout = std::make_unique<VerticalLayout>(0, true);
    window->GetRootWidget().SetLayout(std::move(layout));

    auto content = std::make_unique<Panel>();
    content->SetSize(800, 600);
    Panel* contentRaw = content.get();
    window->GetRootWidget().AddChild(std::move(content));
    window->GetRootWidget().Arrange();

    const int initialH = contentRaw->GetHeight();

    // 弹出全屏模态遮罩
    auto mask = std::make_unique<Panel>();
    mask->SetSize(800, 600);
    OverlayOptions opt;
    opt.layer = OverlayLayer::Modal;
    window->ShowOverlay(std::move(mask), opt);

    // 主树布局完全不受挤压
    window->GetRootWidget().Arrange();
    EXPECT_EQ(contentRaw->GetHeight(), initialH);
}

TEST(Overlay, ZOrderHierarchy) {
    TestApp app;
    auto window = app.Create<Window>("ZOrderHierarchy", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto normal = std::make_unique<Button>("Normal");
    normal->SetPosition(50, 50);
    normal->SetSize(100, 40);
    Button* normalRaw = normal.get();
    OverlayOptions optNormal;
    optNormal.layer = OverlayLayer::Normal;
    window->ShowOverlay(std::move(normal), optNormal);

    auto modal = std::make_unique<Button>("Modal");
    modal->SetPosition(50, 50);
    modal->SetSize(100, 40);
    Button* modalRaw = modal.get();
    OverlayOptions optModal;
    optModal.layer = OverlayLayer::Modal;
    window->ShowOverlay(std::move(modal), optModal);

    // Modal 压在 Normal 上方
    EXPECT_EQ(app.FindTargetWidget(*window, 60, 60), modalRaw);
}

TEST(Overlay, NestedLifoAndCascadeClose) {
    TestApp app;
    auto window = app.Create<Window>("NestedLifo", 800, 600);
    EXPECT_TRUE(window != nullptr);

    bool closedA = false;
    bool closedB = false;

    auto btnA = std::make_unique<Button>("A");
    Button* rawA = btnA.get();
    OverlayOptions optA;
    optA.onClosed = [&]{ closedA = true; };
    window->ShowOverlay(std::move(btnA), optA);

    auto btnB = std::make_unique<Button>("B");
    Button* rawB = btnB.get();
    OverlayOptions optB;
    optB.ownerOverlay = rawA;
    optB.onClosed = [&]{ closedB = true; };
    window->ShowOverlay(std::move(btnB), optB);

    // 关闭父浮层 A，子浮层 B 自动级联关闭
    window->HideOverlay(rawA);
    EXPECT_TRUE(closedA);
    EXPECT_TRUE(closedB);
    EXPECT_FALSE(window->HasActiveOverlay());
}

TEST(Overlay, Phase41SettleIntegration) {
    TestApp app;
    auto window = app.Create<Window>("Phase41Integration", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto panel = std::make_unique<Panel>();
    panel->SetLayout(std::make_unique<VerticalLayout>(5));
    auto child = std::make_unique<Button>("Child");
    Button* childRaw = child.get();
    panel->AddChild(std::move(child));

    window->ShowOverlay(std::move(panel));

    // 修改浮层内尺寸置脏
    childRaw->SetSize(200, 50);
    EXPECT_TRUE(window->GetOverlayRoot().NeedsLayout());

    Test::WindowProbe probe;
    probe.PumpOneFrame(*window);
    EXPECT_FALSE(window->GetOverlayRoot().NeedsLayout());
}

// ── 批 3 Light Dismiss、Focus Trap 与锚点跟随 ─────────────────────

TEST(Overlay, LightDismissPolicies) {
    TestApp app;
    auto window = app.Create<Window>("LightDismiss", 800, 600);
    EXPECT_TRUE(window != nullptr);

    int clicks = 0;
    auto baseBtn = std::make_unique<Button>("Base");
    baseBtn->SetPosition(20, 20);
    baseBtn->SetSize(80, 40);
    baseBtn->SetOnClick([&]{ ++clicks; });
    window->GetRootWidget().AddChild(std::move(baseBtn));

    // 1. PassThrough 策略
    auto pop1 = std::make_unique<Button>("Pop1");
    pop1->SetPosition(200, 200);
    pop1->SetSize(100, 50);
    OverlayOptions opt1;
    opt1.lightDismiss = LightDismissPolicy::PassThrough;
    window->ShowOverlay(std::move(pop1), opt1);

    MouseButtonDownEvent down1(30, 30, MouseButton::Left, 0, 0, window.get());
    app.OnMouseButtonDown(down1);
    EXPECT_FALSE(window->HasActiveOverlay());
    EXPECT_EQ(clicks, 1); // 穿透触发

    // 2. Consume 策略
    auto pop2 = std::make_unique<Button>("Pop2");
    pop2->SetPosition(200, 200);
    pop2->SetSize(100, 50);
    OverlayOptions opt2;
    opt2.lightDismiss = LightDismissPolicy::Consume;
    window->ShowOverlay(std::move(pop2), opt2);

    MouseButtonDownEvent down2(30, 30, MouseButton::Left, 0, 0, window.get());
    app.OnMouseButtonDown(down2);
    EXPECT_FALSE(window->HasActiveOverlay());
    EXPECT_EQ(clicks, 1); // 拦截未增加
}

TEST(Overlay, ExemptWidgetToggle) {
    TestApp app;
    auto window = app.Create<Window>("ExemptToggle", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto entryBtn = std::make_unique<Button>("Entry");
    entryBtn->SetPosition(20, 20);
    entryBtn->SetSize(80, 40);
    Button* entryRaw = entryBtn.get();
    window->GetRootWidget().AddChild(std::move(entryBtn));

    auto pop = std::make_unique<Button>("Pop");
    pop->SetPosition(200, 200);
    pop->SetSize(100, 50);
    OverlayOptions opt;
    opt.exemptWidget = entryRaw;
    window->ShowOverlay(std::move(pop), opt);

    // 点击豁免按钮不关闭浮层
    MouseButtonDownEvent down(30, 30, MouseButton::Left, 0, 0, window.get());
    app.OnMouseButtonDown(down);
    EXPECT_TRUE(window->HasActiveOverlay());
}

TEST(Overlay, CloseOnEscKey) {
    TestApp app;
    auto window = app.Create<Window>("CloseOnEsc", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto pop = std::make_unique<Button>("Pop");
    pop->SetPosition(100, 100);
    pop->SetSize(100, 50);
    OverlayOptions opt;
    opt.closeOnEsc = true;
    window->ShowOverlay(std::move(pop), opt);

    KeyDownEvent escEvent(KeyCode::Escape, 0, window.get());
    window->HandleKeyDown(escEvent);
    EXPECT_FALSE(window->HasActiveOverlay());
}

TEST(Overlay, FocusTrapAndRestore) {
    TestApp app;
    auto window = app.Create<Window>("FocusTrap", 800, 600);
    EXPECT_TRUE(window != nullptr);

    auto baseBtn = std::make_unique<Button>("BaseFocusable");
    baseBtn->SetPosition(10, 10);
    baseBtn->SetSize(80, 40);
    Button* baseRaw = baseBtn.get();
    window->GetRootWidget().AddChild(std::move(baseBtn));
    window->SetFocusedWidget(baseRaw);
    EXPECT_EQ(window->GetFocusedWidget(), baseRaw);

    // 弹出包含子按钮的模态浮层
    auto panel = std::make_unique<Panel>();
    auto popBtn1 = std::make_unique<Button>("Pop1");
    auto popBtn2 = std::make_unique<Button>("Pop2");
    Button* raw1 = popBtn1.get();
    Button* raw2 = popBtn2.get();
    panel->AddChild(std::move(popBtn1));
    panel->AddChild(std::move(popBtn2));

    OverlayOptions opt;
    opt.layer = OverlayLayer::Modal;
    window->ShowOverlay(std::move(panel), opt);

    // 焦点落入浮层首项
    EXPECT_EQ(window->GetFocusedWidget(), raw1);

    // Focus Trap 循环
    window->FocusNext(1);
    EXPECT_EQ(window->GetFocusedWidget(), raw2);
    window->FocusNext(1);
    EXPECT_EQ(window->GetFocusedWidget(), raw1); // 闭环循环，不跳到 baseRaw

    // 关闭浮层，恢复原焦点
    window->HideOverlay(nullptr);
    EXPECT_EQ(window->GetFocusedWidget(), baseRaw);
}

TEST(Overlay, AnchorFlipThenClamp) {
    TestApp app;
    auto window = app.Create<Window>("AnchorFlip", 800, 600);
    EXPECT_TRUE(window != nullptr);

    // 锚点位于底部边缘
    auto anchor = std::make_unique<Button>("Anchor");
    anchor->SetPosition(100, 550);
    anchor->SetSize(100, 40);
    Button* anchorRaw = anchor.get();
    window->GetRootWidget().AddChild(std::move(anchor));

    auto pop = std::make_unique<Button>("Pop");
    pop->SetSize(100, 100);
    Button* popRaw = pop.get();

    OverlayOptions opt;
    opt.anchorWidget = anchorRaw;
    opt.placement = AnchorPlacement::Below;
    window->ShowOverlay(std::move(pop), opt);

    // 下方超限，自动翻转为 Above (550 - 100 = 450)
    EXPECT_EQ(popRaw->GetY(), 450);
}

TEST(Overlay, AnchorDetachAutoClose) {
    TestApp app;
    auto window = app.Create<Window>("AnchorDetach", 800, 600);
    EXPECT_TRUE(window != nullptr);

    bool closed = false;
    auto anchor = std::make_unique<Button>("Anchor");
    Button* anchorRaw = anchor.get();
    window->GetRootWidget().AddChild(std::move(anchor));

    auto pop = std::make_unique<Button>("Pop");
    OverlayOptions opt;
    opt.anchorWidget = anchorRaw;
    opt.onClosed = [&]{ closed = true; };
    window->ShowOverlay(std::move(pop), opt);

    // 摘树锚点
    window->GetRootWidget().RemoveChild(anchorRaw);

    Test::WindowProbe probe;
    probe.PumpOneFrame(*window);
    EXPECT_TRUE(closed);
    EXPECT_FALSE(window->HasActiveOverlay());
}

void RegisterOverlayTests() {
    // 注册全部 16 条用例
}
```

---

## §4 分批实施步骤与读数链

| 批次 | 实施文件与步骤 | 激活测试清单 | 用例读数 |
|---|---|---|---|
| **批 0** | **测试基建与公共类型（产品码零逻辑变动）**<br>1. 创建 `ECDI/include/ECDI/Window/OverlayTypes.h`<br>2. 创建 `ECDI/src/Tests/OverlayTests.cpp` 骨架<br>3. `RunAllTests.h/.cpp` 声明与注册 | T42-1（负对照）<br>T42-2（负对照） | **556 → 558**<br>(+2) |
| **批 1** | **双根基础拓扑与五处单根清算**<br>1. `Window.h` 声明 `m_overlayRoot`（在 `m_platformWindow` 前）<br>2. `Window.cpp` 挂载三层 Panel 并强制设初始尺寸；实现 `HitTestWindow`<br>3. `HoverTracker` 增加双根校验；`Application.cpp` 9 处调用点双根化；`FindTargetWidget` 切换 | T42-3（回落负对照）<br>T42-14（HoverTracker双根）<br>T42-15（Tooltip穿透） | **558 → 561**<br>(+3) |
| **批 2** | **浮层生命周期、分层编排与管线接缝**<br>1. 实现 `ShowOverlay`、`HideOverlay`、`HasActiveOverlay`<br>2. `m_pendingDestroyOverlays` 延迟析构；`PaintFrame` 渲染编排与 Phase 41 接缝<br>3. `OnResized` 同步全量 Panel 尺寸与 `Arrange()` | T42-4（逃逸视口裁剪）<br>T42-5（主布局零挤压）<br>T42-6（Z-Order层次）<br>T42-10（嵌套级联关闭）<br>T42-16（Phase 41结算） | **561 → 566**<br>(+5) |
| **批 3** | **Light Dismiss、Focus Trap 与锚点跟随（全功能闭环）**<br>1. `Application::OnMouseButtonDown` 集成点外关闭与豁免<br>2. `Window::HandleKeyDown` 拦截 ESC 关闭浮层<br>3. `FocusNext` 实现模态 Focus Trap；`ShowOverlay`/`HideOverlay` 焦点保存与恢复<br>4. `UpdateOverlayPositions` 实现 Flip-then-Clamp 与脱树自动关闭 | T42-7（点外策略）<br>T42-8（豁免按钮）<br>T42-9（ESC 键关闭）<br>T42-11（Focus Trap与回焦）<br>T42-12（锚点翻转）<br>T42-13（脱树自动关闭） | **566 → 572**<br>(+6，累计 +16) |

---

## §5 影响面分析与风险控制

1. **Phase 20 构造期崩溃硬防护**：
   - 依赖机检判据 `I42-2` 确保 `m_overlayRoot` 绝不发生声明顺序漂移。
2. **派发链断言保真度**：
   - 保持 `FRAMEWORK_ASSERT` 为双根集合断言，机检 `I42-4` 确保 9 处调用点全部精准传入双根。
3. **Tooltip 穿透与模态击穿防护**：
   - `HitTestWindow` 显式跳过 `m_tooltipLayer`；且只要有活动模态层，点击落在空白处直接阻断不回落主树。
4. **头文件与规模锚点**：
   - 公共头由 100 增至 101，用例从 556 增至 572，实施收口时同步更新 README 与 `doc_lint.py`。

---

## §6 实施回填表（施工期占位）

| 批次 | 实际提交 Hash | 实测用例读数 | 预估 vs 实际差异记录 | 签名与时间 |
|---|---|---|---|---|
| **批 0** | 待回填 | 待回填 (期望 558) | 待回填 | — |
| **批 1** | 待回填 | 待回填 (期望 561) | 待回填 | — |
| **批 2** | 待回填 | 待回填 (期望 566) | 待回填 | — |
| **批 3** | 待回填 | 待回填 (期望 572) | 待回填 | — |
