#pragma once

#include "ECDI/Theme/CheckBoxStyle.h"
#include "ECDI/Widget/StateWidget.h"

namespace ECDI{

class PaintContext;

/// @brief 复选按钮（6.2；方框 + 勾）
/// @details 独立状态（不互斥）；OnClickToggle 用基类默认（反转）。
/// 文字视觉来自 TextWidget::m_style（TextStyle）；框/勾视觉来自 CheckBoxStyle。
class CheckBox: public StateWidget{

public:

	CheckBox();                                   // 默认构造——注入 CheckBoxStyle
	explicit CheckBox(const std::string& text);

	using TextWidget::SetStyle;   // 防名字隐藏——保留 TextStyle 设置（StateWidget 未定义 SetStyle，直接引用 TextWidget 基类）

	void ApplyTheme(const Theme& theme) override;   // TextWidget::ApplyTheme（TextStyle）+ CheckBoxStyle
	void SetStyle(CheckBoxStyleOverride override);  // CheckBox 专属覆盖

protected:

	/// @brief CheckBox 专属样式（protected——测试派生类可访问；不含 foreground——TextStyle 是文字唯一来源）
	CheckBoxStyle m_style;

	/// @brief 文本位置（override——与状态框垂直中心对齐，非控件中心：状态框 16px 在顶部，默认居中会偏下半个字）
	/// @details DrawTextContent 的 x 已含框偏移（x+boxSize+4）；本方法只修正垂直偏移 = (boxSize - lineHeight)/2
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

	void OnPaint(PaintContext& ctx, int x, int y) override;

};

}
