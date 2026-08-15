#include "ECDI/Layout/HorizontalLayout.h"

#include "ECDI/Widget/Widget.h"

namespace ECDI{

void HorizontalLayout::Arrange(Widget& parent){

	// 与 VerticalLayout 镜像（diff 同构约束：仅 y→x / height→width）——
	// 幂等：每次从 currentX=0 开始，不依赖子控件当前 Position（设计契约 1）
	int currentX = 0;

	size_t count = parent.GetChildCount();

	for (size_t i = 0; i < count; i++){

		Widget* child = parent.GetChildAt(i);

		child->SetPosition(currentX, 0);   // 完全接管 Position（契约 2）；顶部对齐 y=0（契约 4）

		currentX += child->GetWidth();     // 累加宽度（契约 3）；不修改子尺寸（契约 10）

	}

}

}
