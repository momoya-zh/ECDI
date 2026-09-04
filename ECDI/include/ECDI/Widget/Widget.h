#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Widget/WidgetState.h"

#include <vector>
#include <memory>

namespace ECDI{

// 前向声明：避免头文件循环依赖
class Window;
class TimerEvent;
class MouseMoveEvent;
class MouseButtonUpEvent;
class MouseButtonDownEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;
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
	void AddChild(std::unique_ptr<Widget> child);

	/// @brief 移除指定子 Widget，返回其所有权
	/// @param child 要移除的子 Widget 指针
	/// @return 被移除的子 Widget（调用方可继续使用或销毁）
	std::unique_ptr<Widget> RemoveChild(Widget* child);

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
	void SetPosition(int x, int y) { m_geometry.x = static_cast<float>(x); m_geometry.y = static_cast<float>(y); }

	/// @brief 设置宽高
	/// @details 虚函数（9.5 R1：TextBox override 挂 Resize 后 Caret 可见——Layout 只 SetPosition 不碰尺寸，
	/// SetSize 是尺寸唯一入口）。签名不变、行为兼容；基类实现 = 直接写 m_geometry（移 Widget.cpp）
	virtual void SetSize(int w, int h);

	// ── Stretch（9.7 自适应布局：剩余空间分配权重）────────

	/// @brief 设置剩余空间分配权重（9.7——0 = 不参与分配，保持当前尺寸；>0 = 主轴尺寸由父 Layout 分配）
	/// @pre stretch >= 0（debug assert；负值无合理权重语义）
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
	/// @note §3.7 纯 geometry operation：内部只调 SetSize（虚分派——TextBox override 既有语义）；
	///       不 Arrange、不 Invalidate、不挂钩 SetText——布局/重绘由调用方负责。
	///       float → int 向零截断（v1 不引入 rounding policy）
	bool AutoSize();

	int GetX() const noexcept { return static_cast<int>(m_geometry.x); }

	int GetY() const noexcept { return static_cast<int>(m_geometry.y); }

	int GetWidth() const noexcept { return static_cast<int>(m_geometry.width); }

	int GetHeight() const noexcept { return static_cast<int>(m_geometry.height); }


	// ── State ───────────────────────────────────────

	bool IsVisible() const noexcept { return m_state.visible; }

	void SetVisible(bool v) { m_state.visible = v; }

	bool IsEnabled() const noexcept { return m_state.enabled; }

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

	// ── Hover（9.5 R4：Hover 状态变化事实——9.6 动画前置）────────
	// 职责：只产生"已发生的事实"（进入/离开），不负责视觉过渡/动画/Hover Style（语义推迟消费者）
	// 契约：脱树导致的失效目标不派发 OnMouseLeave（Application::IsWidgetInTree 验证）

	/// @brief 鼠标移入通知（Hover 状态变化事实——9.6 动画前置）
	/// @details 默认空实现——无 hover 需求的控件零感知；事件驱动语义：进入 = 状态变化事实，非坐标事实
	virtual void OnMouseEnter();

	/// @brief 鼠标移出通知（Hover 状态变化事实）
	/// @details 默认空实现；契约：脱树导致的失效不派发此方法（Application 验证）
	virtual void OnMouseLeave();

	// ── Focus ──────────────────────────────────────

	/// @brief 是否可以获得键盘焦点
	/// @return 默认 false（不可聚焦），子类 override 返回 true
	/// @details 只声明能力，visible/enabled 判断由事件系统前置处理
	virtual bool CanFocus() const noexcept { return false; }

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

	/// @brief 获取绝对坐标（父链累加；TextBox 光标/ScrollBar/Popup/Tooltip 未来用）
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

	void Arrange();

	void ArrangeInternal();

	Widget* GetChildAt(size_t index) noexcept;

	const Widget* GetChildAt(size_t index) const noexcept;

	void Paint(PaintContext& ctx,int offsetX,int offsetY);

private:

	/// @brief 设置所属 Window（仅 Window 构造 RootWidget 后调用；friend 授权）
	void SetWindow(Window* window);

	friend class Window;	///< SetWindow 私有化的配套授权

	/// @brief 递归检测 widget 是否是 this 的后代（用于 AddChild 防环检测）
	bool Contains(const Widget* widget) const noexcept;

	Widget* m_parent = nullptr;		///< 父节点指针（RootWidget 为 nullptr）

	std::vector<std::unique_ptr<Widget>> m_children;	///< 子节点列表（有序，后添加的在上层）

	Rect m_geometry;		///< 位置与尺寸（Core 公共类型 Rect(float)，决策 1）

	WidgetState m_state;		///< 可见性与启用状态

	bool m_showFocusRect = true;	///< 聚焦框视觉开关（9.6 收尾方案 A；默认显示）

	int m_stretch = 0;		///< 剩余空间分配权重（9.7；默认 0 = 不参与分配——向后兼容锚点）

	std::unique_ptr<Layout> m_layout;

	Window* m_window = nullptr;		///< 所属 Window（非拥有；仅 RootWidget 由 Window 设置）

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
