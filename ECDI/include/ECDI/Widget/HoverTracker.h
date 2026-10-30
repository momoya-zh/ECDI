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

	/// @brief 设置副树根（浮层根，支持双根树内校验 · Phase 42）
	void SetSecondaryTreeRoot(Widget* secondaryRoot) noexcept{ m_secondaryRoot = secondaryRoot; }

	/// @brief 更新 Hover 状态机（唯一入口）
	/// @param newTarget HitTest 命中的新目标（nullable）
	/// @pre newTarget == nullptr 或 属于当前树（调用方保证——HitTest 结果必然属于当前 Window Tree）
	/// @details 契约 D：目标切换 A→B 时严格 A.OnMouseLeave() → B.OnMouseEnter()；
	/// 契约 C：oldHover 已脱树（IsInTree false）→ 置空不派发 Leave（区分正常离开 vs 异常失效）。
	void Update(Widget* newTarget);

	/// @brief 获取当前 Hover 目标（非拥有指针，可能为 nullptr）
	Widget* GetHoverWidget() const noexcept{ return m_hoverWidget; }

	/// @brief 注销某个子树上的 Hover 目标（★ 审计 #59/H6：摘树时的失效清理）
	/// @param subtreeRoot 即将脱树的子树根（`nullptr` = no-op）
	/// @details 若当前 Hover 目标落在 `subtreeRoot` 子树内 ⇒ **置空且不派发 `OnMouseLeave`**。
	///          ★ 语义 = **契约 C 的直接延伸**（`HoverTracker.cpp:18` 已有同款判据）：
	///          脱树是「**异常失效**」而非「正常离开」，补发 Leave 会制造语义歧义
	///          （控件已不在树上，却收到"鼠标离开"）⇒ 一律不补发。
	///          ⚠️ 必须由**摘树方**在清除父指针**之前**调用——`IsInTree` 依赖父链上溯，
	///          链一断就无法判定包含关系（这也是本方法存在的根本原因）。
	void ForgetSubtree(const Widget* subtreeRoot) noexcept;

private:

	/// @brief 沿 Parent 链上溯，只有最终可达当前树根才视为属于当前树（契约 C 验证）
	bool IsInTree(Widget* widget) const noexcept;

	Widget* m_treeRoot = nullptr;		///< 树根（契约 C 验证锚点，非拥有指针）
	Widget* m_secondaryRoot = nullptr;	///< 副树根（浮层根，非拥有指针 · Phase 42）

	Widget* m_hoverWidget = nullptr;	///< 当前 Hover 目标（非拥有指针——与 m_focusedWidget 同族）

};

}
