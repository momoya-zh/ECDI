#pragma once

#include "Geometry.h"
#include "WidgetState.h"

#include <vector>
#include <memory>

// Phase 3 GDI 临时桥梁：必须保持在全局作用域（HDC 是 Windows.h 的全局类型；
// 放进 namespace ECDI 会声明 ECDI::HDC__（与 ::HDC__ 不同的类）→ 类型不匹配编译错误。
// Phase 4 删除此桥梁（见 docs/phase4-renderer-design.md §13）。
struct HDC__;
using HDC = HDC__*;

namespace ECDI
{

// 前向声明：避免头文件循环依赖
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
class Widget
{

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
	const Geometry& GetGeometry() const noexcept { return m_geometry; }

	/// @brief 设置相对于父 Widget 的位置
	void SetPosition(int x, int y) { m_geometry.x = x; m_geometry.y = y; }

	/// @brief 设置宽高
	void SetSize(int w, int h) { m_geometry.width = w; m_geometry.height = h; }

	int GetX() const noexcept { return m_geometry.x; }

	int GetY() const noexcept { return m_geometry.y; }

	int GetWidth() const noexcept { return m_geometry.width; }

	int GetHeight() const noexcept { return m_geometry.height; }


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
	// ── Focus ──────────────────────────────────────

	/// @brief 是否可以获得键盘焦点
	/// @return 默认 false（不可聚焦），子类 override 返回 true
	/// @details 只声明能力，visible/enabled 判断由事件系统前置处理
	virtual bool CanFocus() const noexcept { return false; }

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

	void Paint(HDC hdc,int offsetX,int offsetY);

private:

	/// @brief 递归检测 widget 是否是 this 的后代（用于 AddChild 防环检测）
	bool Contains(const Widget* widget) const noexcept;

	Widget* m_parent = nullptr;		///< 父节点指针（RootWidget 为 nullptr）

	std::vector<std::unique_ptr<Widget>> m_children;	///< 子节点列表（有序，后添加的在上层）

	Geometry m_geometry;		///< 位置与尺寸

	WidgetState m_state;		///< 可见性与启用状态

	std::unique_ptr<Layout> m_layout;

protected:

	/// @brief 判断局部坐标 (x,y) 是否落在当前 Widget 的命中区域内
	/// @details
	/// 默认实现：矩形区域 [0, width) × [0, height)。
	/// 子类可 override 实现圆形、不规则形状等自定义命中区域（Template Method）。
	virtual bool ContainsPoint(int x, int y) const noexcept;

	virtual void OnPaint(HDC hdc,int x,int y);
};

}
