#include "ECDI/Layout/HorizontalLayout.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Widget.h"

#include <algorithm>

namespace ECDI{

HorizontalLayout::HorizontalLayout(int spacing, bool fillCrossAxis, int padding)
	: m_spacing(spacing), m_fillCrossAxis(fillCrossAxis), m_padding(padding < 0 ? 0 : padding)
{
	FRAMEWORK_ASSERT(spacing >= 0);
	FRAMEWORK_ASSERT(padding >= 0);   // 与 spacing 同型；Release 侧由初始化列表的三元钳 0（17 详设 △1）
}

void HorizontalLayout::Arrange(Widget& parent){

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
			fixedTotal += child->GetWidth();   // stretch=0 的子（主轴保持当前尺寸）
		}
	}

	// ② 剩余空间（F4：负值钳 0；spacing = n−1 个间隙——详设 §2.3；padding 先扣 2×——17 详设 △3）
	const int remaining = (std::max)(0, parent.GetWidth() - 2 * m_padding - fixedTotal
	                                       - m_spacing * static_cast<int>(count - 1));

	// ③ 跨轴可用尺寸（17：padding 是硬 inset ⇒ 负值钳 0——详设 △4；循环外算一次，跨轴取值的唯一入口）
	const int cross = (std::max)(0, parent.GetHeight() - 2 * m_padding);

	// ④ 分配 + 定位（F2：尺寸一律走 SetSize 虚分派；D2：截断 + 末位吃余数）
	int x = m_padding;
	int allocated = 0;
	size_t stretchSeen = 0;
	for (size_t i = 0; i < count; ++i){
		Widget* child = parent.GetChildAt(i);

		if (child->GetStretch() > 0){
			++stretchSeen;
			int width = (stretchSeen == stretchCount)
				? remaining - allocated                                  // 末位吃余数——Σ == remaining
				: remaining * child->GetStretch() / totalStretch;        // 整数除法截断
			allocated += width;
			child->SetSize(width, m_fillCrossAxis ? cross : child->GetHeight());
		}
		else if (m_fillCrossAxis){
			child->SetSize(child->GetWidth(), cross);       // stretch=0 子：主轴不动，跨轴强制填充
		}

		child->SetPosition(x, m_padding);                                // 跨轴坐标 = padding（契约 4 的推广——17 详设 △8）
		x += child->GetWidth() + ((i + 1 < count) ? m_spacing : 0);
	}

}

}