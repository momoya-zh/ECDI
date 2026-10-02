#include "ECDI/Layout/GridLayout.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

GridLayout::GridLayout(int columns, int cellWidth, int cellHeight, int spacing)
	: m_columns(columns), m_cellWidth(cellWidth), m_cellHeight(cellHeight), m_spacing(spacing)
{
	FRAMEWORK_ASSERT(columns >= 1);
	FRAMEWORK_ASSERT(cellWidth >= 1);
	FRAMEWORK_ASSERT(cellHeight >= 1);
	FRAMEWORK_ASSERT(spacing >= 0);
}

void GridLayout::Arrange(Widget& parent){

	int slot = 0;   // 可见序号（C-VIS-1：连续槽位）

	for (size_t i = 0; i < parent.GetChildCount(); ++i){

		Widget* child = parent.GetChildAt(i);

		if (!child->IsVisible()){
			child->SetPosition(-child->GetWidth(), -child->GetHeight());   // C-VIS-4：停泊到自身 bbox 负区
			continue;
		}

		const int col = slot % m_columns;
		const int row = slot / m_columns;
		child->SetPosition(col * (m_cellWidth + m_spacing),           // D9：只置位、不改尺寸
		                   row * (m_cellHeight + m_spacing));
		++slot;

	}

}

}
