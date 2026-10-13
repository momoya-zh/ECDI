#include "ECDI/Layout/VerticalLayout.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Widget.h"

#include <algorithm>

namespace ECDI{

VerticalLayout::VerticalLayout(int spacing, bool fillCrossAxis, int padding)
	: m_spacing(spacing < 0 ? 0 : spacing), m_fillCrossAxis(fillCrossAxis), m_padding(padding < 0 ? 0 : padding)
{
	// ★ 审计 M20：`spacing < 0` 与 `padding < 0` 同族同处——Release 下断言为空 ⇒
	//   负 spacing 会让 `remaining` 反而变大（:49-50 的 `- m_spacing * gapCount`）
	//   且 `y += m_spacing` 让游标**往回走** ⇒ 兄弟控件重叠。钳制（初始化列表）即契约本体，
	//   与 GridLayout 的 `columns` 钳制 / 本类的 padding 钳制同形（V/H 无断言）。
	FRAMEWORK_ASSERT(spacing >= 0);
	FRAMEWORK_ASSERT(padding >= 0);   // 与 spacing 同型；Release 侧由初始化列表的三元钳 0（17 详设 △1）
}

void VerticalLayout::Arrange(Widget& parent){

	// ★ Phase 30 △7（C-VIS 对齐——Phase 25 ListLayout 四层契约补齐到老 H/V 布局）：
	//   隐藏子**不占槽、不吃间隙、不参与 stretch 权重**，停泊自身 bbox 负区 (−w, −h)（C-VIS-4）。
	//   全可见时 gapCount/求和集合/累进与旧实现**逐位等价**（C30-5——专测 T30-4 锚定）。

	// ① 第一遍：**可见子**统计（隐藏子跳过——stretch/固定尺寸均不进求和）
	const size_t count = parent.GetChildCount();
	if (count == 0) return;

	int fixedTotal = 0;
	int totalStretch = 0;
	size_t visibleCount = 0;
	size_t visibleStretchCount = 0;
	for (size_t i = 0; i < count; ++i){
		const Widget* child = parent.GetChildAt(i);
		if (!child->IsVisible()){
			continue;   // C-VIS-1：隐藏子不进任何统计
		}
		++visibleCount;
		if (child->GetStretch() > 0){
			totalStretch += child->GetStretch();
			++visibleStretchCount;
		}
		else{
			fixedTotal += child->GetHeight();   // stretch=0 的可见子（主轴保持当前尺寸）
		}
	}

	// ② 剩余空间（F4：负值钳 0 既有语义不变；★ D30-G 乘法守卫：visibleCount == 0 时
	//    `spacing × (−1)` 会反向增大 remaining ⇒ gapCount 钳 0；间隙只落**可见对**之间）
	const int gapCount = (visibleCount > 0) ? static_cast<int>(visibleCount - 1) : 0;
	const int remaining = (std::max)(0, parent.GetHeight() - 2 * m_padding - fixedTotal
	                                        - m_spacing * gapCount);

	// ③ 跨轴可用尺寸（17：padding 是硬 inset ⇒ 负值钳 0——详设 △4；循环外算一次，跨轴取值的唯一入口）
	const int cross = (std::max)(0, parent.GetWidth() - 2 * m_padding);

	// ④ 分配 + 定位（F2：尺寸一律走 SetSize 虚分派；D2：截断 + **末位可见** stretch 吃余数）
	int y = m_padding;
	int allocated = 0;
	size_t stretchSeen = 0;
	bool prevVisible = false;
	for (size_t i = 0; i < count; ++i){
		Widget* child = parent.GetChildAt(i);

		// ★ D30-I 停泊三步（顺序不可换——先取**当前**尺寸再停泊，任何 SetSize 不得插入）：
		//   隐藏子不 SetSize（跨轴 fill 也不刷——保持现值）、不参与分配、不吃 spacing。
		if (!child->IsVisible()){
			const int parkW = child->GetWidth();
			const int parkH = child->GetHeight();
			child->SetPosition(-parkW, -parkH);
			continue;
		}

		if (prevVisible) y += m_spacing;   // ★ 间隙只落可见对之间（before-add 形式——全可见时与旧「尾加」逐点等价）
		prevVisible = true;

		if (child->GetStretch() > 0){
			++stretchSeen;
			// ★★ 审计 L2：`remaining × GetStretch()` 用 **long long** 承积。
			//   `remaining` 可达 ~2^31（父高 − padding − fixedTotal）、`GetStretch()` 是调用方
			//   给的任意 int ⇒ **int × int 可溢出**（UB）。同族先例：`ScrollBar.cpp:121`
			//   「中间乘积一律 long long 防溢出」（详设 §3.4）。
			//   ★ 零行为变化：合法范围内的值（|remaining|、|stretch| 均 < 2^16 时）
			//   int 与 long long 运算逐位相同；溢出只可能发生在**极端输入**上，
			//   而那里的旧行为本身就是 UB ⇒ 改后是"从 UB 变为确定值"，不是改行为。
			int height = (stretchSeen == visibleStretchCount)   // ★ 盯防②：比较对象 = **可见**计数
				? remaining - allocated                                  // 末位**可见** stretch 吃余数——Σ == remaining
				: static_cast<int>(
					static_cast<long long>(remaining) * child->GetStretch() / totalStretch);  // 整数除法截断（既有 D2 不变）
			allocated += height;
			child->SetSize(m_fillCrossAxis ? cross : child->GetWidth(), height);
		}
		else if (m_fillCrossAxis){
			child->SetSize(cross, child->GetHeight());       // stretch=0 可见子：主轴不动，跨轴强制填充
		}

		child->SetPosition(m_padding, y);                                // 跨轴坐标 = padding（契约 4 的推广——17 详设 △8）
		y += child->GetHeight();
	}

}

}