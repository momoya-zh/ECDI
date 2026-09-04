#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{


/// @brief 垂直布局（9.7：stretch + spacing + fillCrossAxis——diff 同构约束仅 y→x / height→width）
/// @details 职责：根据子控件 stretch 权重分配主轴（Y）尺寸 + 跨轴（X）可选填充 + spacing 间隙。
/// 幂等：每次 Arrange 从头计算，不依赖子控件当前 Position（6.1 契约 1）。
class VerticalLayout : public Layout{

public:

	/// @param spacing      主轴相邻子间隙 px（默认 0 = 现状；>= 0 debug assert——负间距无合理语义）
	/// @param fillCrossAxis 跨轴填充开关（默认 false = 现状；true = 所有子跨轴 = 父跨轴，跨轴坐标恒 0）
	explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false);

	void Arrange(Widget& parent) override;

private:

	int m_spacing = 0;
	bool m_fillCrossAxis = false;

};

}