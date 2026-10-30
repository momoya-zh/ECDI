#include "ECDI/Widget/HoverTracker.h"

namespace ECDI{

void HoverTracker::Update(Widget* newTarget){

	// 同目标不重复通知
	if (m_hoverWidget == newTarget){

		return;

	}

	Widget* oldHover = m_hoverWidget;
	m_hoverWidget = newTarget;   // 先更新（回调内部可能查 hover 状态）

	// 契约 D：Leave → Enter 严格顺序
	if (oldHover != nullptr && IsInTree(oldHover)){

		oldHover->OnMouseLeave();   // 正常离开派发

	}

	if (newTarget != nullptr){

		newTarget->OnMouseEnter();   // 进入派发

	}

}

bool HoverTracker::IsInTree(Widget* widget) const noexcept{

	// 沿 Parent 链上溯，只有最终可达当前树根或副树根才视为属于当前树
	if (widget == nullptr || (m_treeRoot == nullptr && m_secondaryRoot == nullptr)){

		return false;

	}

	Widget* current = widget;

	while (current != nullptr){

		if (current == m_treeRoot || (m_secondaryRoot != nullptr && current == m_secondaryRoot)){

			return true;   // 到达树根或副树根 = 仍在树

		}

		current = current->GetParent();

	}

	return false;   // 上溯到 null 仍未到树根 = 已脱树

}

void HoverTracker::ForgetSubtree(const Widget* subtreeRoot) noexcept{

	if (m_hoverWidget == nullptr || subtreeRoot == nullptr){

		return;

	}

	// 判据 = 「当前 Hover 目标是否落在该子树内」（含子树根自身）——沿父链上溯即可，
	// 与 `IsInTree` 同一上溯形式（此处不直接调 `IsInTree`：它判的是"是否仍在**树**"，
	// 而本方法判的是"是否在该**子树**内"，两者端点不同）。
	bool inside = false;

	for (Widget* current = m_hoverWidget; current != nullptr; current = current->GetParent()){

		if (current == subtreeRoot){

			inside = true;

			break;

		}

	}

	if (inside){

		// ★ 契约 C：脱树失效**不补发** OnMouseLeave（`Update` 里的同款判据是"正常离开"路径）
		m_hoverWidget = nullptr;

	}

}

}
