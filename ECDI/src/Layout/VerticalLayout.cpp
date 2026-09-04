#include "ECDI/Layout/VerticalLayout.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Widget.h"

#include <algorithm>

namespace ECDI{

VerticalLayout::VerticalLayout(int spacing, bool fillCrossAxis)
	: m_spacing(spacing), m_fillCrossAxis(fillCrossAxis)
{
	FRAMEWORK_ASSERT(spacing >= 0);
}

void VerticalLayout::Arrange(Widget& parent){

	// ① 一次遍历：stretch 总权重 + stretch=0 子当前主轴尺寸和
	const size_t count = parent.GetChildCount();
	if (count == 0) return;

	int fixedTotal = 0;
	int totalStretch = 0;
	size_t stretchCount = 0;
	for (size_t i = 0; i < count; ++i){
		const Widget* child = parent.GetChildAt(i);
		if (child->GetStretch() > 0){
			totalStretch += child->GetStretch();
			++stretchCount;
		}
		else{
			fixedTotal += child->GetHeight();   // stretch=0 的子（主轴保持当前尺寸）
		}
	}

	// ② 剩余空间（F4：负值钳 0；spacing = n−1 个间隙——详设 §2.3）
	const int remaining = (std::max)(0, parent.GetHeight() - fixedTotal
	                                        - m_spacing * static_cast<int>(count - 1));

	// ③ 分配 + 定位（F2：尺寸一律走 SetSize 虚分派；D2：截断 + 末位吃余数）
	int y = 0;
	int allocated = 0;
	size_t stretchSeen = 0;
	for (size_t i = 0; i < count; ++i){
		Widget* child = parent.GetChildAt(i);

		if (child->GetStretch() > 0){
			++stretchSeen;
			int height = (stretchSeen == stretchCount)
				? remaining - allocated                                  // 末位吃余数——Σ == remaining
				: remaining * child->GetStretch() / totalStretch;        // 整数除法截断
			allocated += height;
			child->SetSize(m_fillCrossAxis ? parent.GetWidth() : child->GetWidth(), height);
		}
		else if (m_fillCrossAxis){
			child->SetSize(parent.GetWidth(), child->GetHeight());       // stretch=0 子：主轴不动，跨轴强制填充
		}

		child->SetPosition(0, y);                                        // 跨轴坐标 0（契约 4，现状不变）
		y += child->GetHeight() + ((i + 1 < count) ? m_spacing : 0);
	}

}

}