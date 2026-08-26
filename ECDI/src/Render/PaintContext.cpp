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

	void PaintContext::DrawLine(const Point& start, const Point& end,
	                            float width, const Color& color){

		// 原样进命令：宽度 float 契约层数据，GDI 端 lround 取整（后端实现细节）
		m_commands.emplace_back(DrawLineCommand{ start, end, width, color });

	}

	void PaintContext::DrawRoundedRect(const Rect& rect, float cornerRadius,
	                                   const Color& color){

		// 半径钳制封在后端（[0, min(w,h)/2]）——契约层只保证 cornerRadius >= 0
		m_commands.emplace_back(DrawRoundedRectCommand{ rect, cornerRadius, color });

	}

	void PaintContext::DrawImage(const Rect& dest, const Image& image){

		// Image 值拷贝进命令：命令持有独立副本，调用方随后修改/释放不影响绘制
		m_commands.emplace_back(DrawImageCommand{ dest, image });

	}

	void PaintContext::PushClip(const Rect& rect){

		// 状态命令：缓冲中的位置 = 生效范围起点（与其后绘制命令求交）
		m_commands.emplace_back(PushClipCommand{ rect });

	}

	void PaintContext::PopClip(){

		// 状态命令：缓冲中的位置 = 裁剪区恢复点
		m_commands.emplace_back(PopClipCommand{});

	}

	void PaintContext::DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color){

		m_commands.emplace_back(DrawFocusRectCommand{ rect, cornerRadius, color });

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
