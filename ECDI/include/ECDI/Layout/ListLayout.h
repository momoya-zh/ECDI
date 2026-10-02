#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{


/// @brief 索引排列布局（Phase 25）：可见子控件按序纵向排列，行几何固定
/// @details 与 VerticalLayout 的分工（初设 §3-②）：**空间分配** vs **索引排列**——
/// 本布局不读 parent 尺寸（挂载点 ScrollContent 的尺寸 = 内容 extent，读之成环）。
/// 契约：
/// - C-VIS（四层）：只排列 IsVisible() 的直接子控件（连续槽位）；**不可见子控件停泊
///   到自身 bbox 负区** `(-w, -h)`（右下角恰落 (0,0) ⇒ 对 UpdateContentExtent 贡献归零）；
///   ★ 停泊**只服务 extent**——不可命中性由 `Widget::HitTest` 入口的可见性门控保证（B5），与本布局无关；
///   实现策略可替换，行为保证（extent）不变。
/// - D9：只调 SetPosition，**不调 SetSize**（child 尺寸归消费者）。
/// - 只处理 parent 的**直接 children**，不递归孙节点（与 Vertical/Horizontal 同族）。
class ListLayout : public Layout{

public:

	/// @param rowHeight 行高（DIP，>= 1）
	/// @param spacing   行间距（DIP，>= 0）
	explicit ListLayout(int rowHeight, int spacing = 0);

	void Arrange(Widget& parent) override;

private:

	int m_rowHeight;
	int m_spacing;

};

}
