#pragma once

#include "ECDI/Widget/Widget.h"

namespace ECDI{

/// @brief Hover 状态机（9.5 R4 方案 A 提取——纯逻辑单元，不依赖 Window/平台）
/// @details
/// 职责：Hover 目标状态追踪 + Enter/Leave 事实通知（契约 C/D）。
/// 与 Window 解耦：测试注入普通 Widget 树根即可无窗口验证（7.2 测试体系）。
/// Window 组合 HoverTracker（has-a）——Application::OnMouseMove 经 Window::UpdateHoverState 委托。
/// 生命周期契约：m_hoverWidget 非拥有指针；树根/目标生命周期由外部（Window/调用方）保证。
class HoverTracker{
public:

	/// @brief 设置树根（契约 C 验证锚点——Widget 树构造后调用；Window 构造体内 SetTreeRoot(m_rootWidget.get())）
	/// @param treeRoot 当前 Widget 树根（非拥有指针）
	void SetTreeRoot(Widget* treeRoot) noexcept{ m_treeRoot = treeRoot; }

	/// @brief 更新 Hover 状态机（唯一入口）
	/// @param newTarget HitTest 命中的新目标（nullable）
	/// @pre newTarget == nullptr 或 属于当前树（调用方保证——HitTest 结果必然属于当前 Window Tree）
	/// @details 契约 D：目标切换 A→B 时严格 A.OnMouseLeave() → B.OnMouseEnter()；
	/// 契约 C：oldHover 已脱树（IsInTree false）→ 置空不派发 Leave（区分正常离开 vs 异常失效）。
	void Update(Widget* newTarget);

	/// @brief 获取当前 Hover 目标（非拥有指针，可能为 nullptr）
	Widget* GetHoverWidget() const noexcept{ return m_hoverWidget; }

private:

	/// @brief 沿 Parent 链上溯，只有最终可达当前树根才视为属于当前树（契约 C 验证）
	bool IsInTree(Widget* widget) const noexcept;

	Widget* m_treeRoot = nullptr;		///< 树根（契约 C 验证锚点，非拥有指针）

	Widget* m_hoverWidget = nullptr;	///< 当前 Hover 目标（非拥有指针——与 m_focusedWidget 同族）

};

}
