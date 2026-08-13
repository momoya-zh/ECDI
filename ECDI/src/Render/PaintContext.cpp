// PaintContext.cpp

#include "ECDI/Render/PaintContext.h"

namespace ECDI {

	PaintContext::PaintContext(CommandBuffer& commands, TextMeasurer& measurer)
		: m_commands(commands)
		, m_measurer(measurer)
	{
	}

	void PaintContext::DrawRect(const Rect& rect, const Color& color){

		m_commands.emplace_back(DrawRectCommand{ rect, color });

	}

	void PaintContext::DrawText(const Point& pos, const std::string& text,
	                            const Color& color, const Font& font){

		// 命令顺序 = 绘制顺序（控件先 DrawRect 背景再 DrawText 文本 → 文本叠背景）
		m_commands.emplace_back(DrawTextCommand{ pos, text, color, font });

	}

	Size PaintContext::MeasureText(const Font& font, const std::string& text){

		// 转发测量器（D2：测量帧无关，任何时刻可测）
		return m_measurer.MeasureText(font, text);

	}

	float PaintContext::LineHeight(const Font& font){

		// 转发测量器（P7：行高与 Measure 同源，垂直居中用）
		return m_measurer.LineHeight(font);

	}

}
