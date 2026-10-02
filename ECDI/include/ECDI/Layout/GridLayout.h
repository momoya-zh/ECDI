#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{


/// @brief 网格排列布局（Phase 25）：可见子控件按序填充固定 cell 网格，末行左对齐
/// @details ★ cellWidth/cellHeight = 布局槽位的几何间距，**不是 child 尺寸约束**
/// （D9：child 大于 cell = 溢出，由消费者自治，本布局不裁不缩）。
/// 契约同 ListLayout（C-VIS / D9 / 只处理直接 children）。
/// 自适应列宽 = 重启条件（随 desktopnest M2 立项取证——初设 §3-⑤）。
class GridLayout : public Layout{

public:

	/// @param columns 列数（>= 1）；row = index / columns，col = index % columns
	explicit GridLayout(int columns, int cellWidth, int cellHeight, int spacing = 0);

	void Arrange(Widget& parent) override;

private:

	int m_columns;
	int m_cellWidth;
	int m_cellHeight;
	int m_spacing;

};

}
