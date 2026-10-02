#include "ECDI/Layout/ListLayout.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

ListLayout::ListLayout(int rowHeight, int spacing)
	: m_rowHeight(rowHeight), m_spacing(spacing)
{
	FRAMEWORK_ASSERT(rowHeight >= 1);
	FRAMEWORK_ASSERT(spacing >= 0);
}

void ListLayout::Arrange(Widget& parent){

	int slot = 0;   // 可见序号（C-VIS-1：连续槽位）

	for (size_t i = 0; i < parent.GetChildCount(); ++i){

		Widget* child = parent.GetChildAt(i);

		if (!child->IsVisible()){
			child->SetPosition(-child->GetWidth(), -child->GetHeight());   // C-VIS-4：停泊到自身 bbox 负区
			continue;
		}

		child->SetPosition(0, slot * (m_rowHeight + m_spacing));   // D9：只置位、不改尺寸
		++slot;

	}

}

}
