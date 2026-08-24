#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Widget/Widget.h"

#include <string>

namespace ECDI{

class PaintContext;

/// @brief 文本控件基类（B1：第二个文本控件出现时抽取）
/// @details 职责：文本数据/字体/颜色/文本绘制/位置计算；
/// Widget = 几何/可见性/事件；Label/Button/TextBox = 自己的行为。
class TextWidget: public Widget{

public:

	TextWidget() = default;   // 空文本控件（Label/Button 的 = default 依赖基类可默认构造）

	explicit TextWidget(const std::string& text);

	explicit TextWidget(std::string&& text);

	void SetText(const std::string& text);

	void SetText(std::string&& text);

	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);

	const Color& GetTextColor() const noexcept;

	/// @brief 设置字体（8.5.1；Font 是纯数据值语义——m_font = font 值拷贝）
	/// @details m_font 原为"预留"成员（Phase 5.5 注释：未来 SetFont 一行接入，OnPaint 零改动）——
	/// 8.5.1 兑现：赋值 + Invalidate。Label/Button 同享基类能力。
	void SetFont(const Font& font);

protected:

	/// @brief 文本绘制位置（B3：对齐策略虚方法，不写死）
	/// @param textWidth 文本宽度——供派生类使用（水平居中需要；基类默认左对齐不用）
	/// @param lineHeight 行高——所有文本控件使用（垂直居中）
	/// @return 默认：水平左对齐 + 垂直居中（P7 定案）
	virtual Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const;

	/// @brief 绘制文本（空文本跳过 → MeasureText 宽高 → CalculateTextPosition → DrawText）
	void DrawTextContent(PaintContext& ctx, int x, int y);

	std::string m_text;

	Color m_textColor = Color::Black();

	Font m_font{};  // 预留：未来 SetFont() 一行接入，OnPaint 零改动

};

}
