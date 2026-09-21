#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{


/// @brief 垂直布局（9.7：stretch + spacing + fillCrossAxis；17：padding——diff 同构约束仅 y→x / height→width）
/// @details 职责：根据子控件 stretch 权重分配主轴（Y）尺寸 + 跨轴（X）可选填充 + spacing 间隙 + 四边内边距 padding。
/// 幂等：每次 Arrange 从头计算，不依赖子控件当前 Position（6.1 契约 1）。
class VerticalLayout : public Layout{

public:

	/// @param spacing      主轴相邻子间隙 px（默认 0 = 现状；>= 0 debug assert——负间距无合理语义）
	/// @param fillCrossAxis 跨轴填充开关（默认 false = 现状；true = 所有子跨轴 = 父跨轴 − 2×padding，跨轴坐标 = padding）
	/// @param padding      四边内边距 px（默认 0 = 现状；>= 0 debug assert / Release 钳 0）；硬 inset——空间不足时内容区退化为 0，坐标仍取 padding，不反向缩减
	explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false, int padding = 0);

	void Arrange(Widget& parent) override;

private:

	int m_spacing = 0;
	bool m_fillCrossAxis = false;
	int m_padding = 0;

};

}