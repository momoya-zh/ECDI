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

	// 沿 Parent 链上溯，只有最终可达当前树根才视为属于当前树
	if (widget == nullptr || m_treeRoot == nullptr){

		return false;

	}

	Widget* current = widget;

	while (current != nullptr){

		if (current == m_treeRoot){

			return true;   // 到达树根 = 仍在树

		}

		current = current->GetParent();

	}

	return false;   // 上溯到 null 仍未到树根 = 已脱树

}

}
