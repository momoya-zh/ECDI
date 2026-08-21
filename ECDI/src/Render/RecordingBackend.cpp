#include "ECDI/Render/RecordingBackend.h"

namespace ECDI {

	void RecordingBackend::DrawRect(const Rect& rect, const Color& color)
	{
		draws.emplace_back(DrawCall{ rect, color });
	}

	void RecordingBackend::DrawText(const Point& pos, const std::string& text,
	                                 const Color& color, const Font& font)
	{
		textDraws.emplace_back(TextDraw{ pos, text, color, font });
	}

	void RecordingBackend::DrawLine(const Point& start, const Point& end,
	                                float width, const Color& color)
	{
		// Phase 8：原样记录（width float 契约层数据不预取整——取整是 GDI 后端细节）
		lineCalls.emplace_back(LineDraw{ start, end, width, color });
	}

	void RecordingBackend::DrawRoundedRect(const Rect& rect, float cornerRadius,
	                                       const Color& color)
	{
		roundedRectCalls.emplace_back(RoundedRectDraw{ rect, cornerRadius, color });
	}

	void RecordingBackend::DrawImage(const Rect& dest, const Image& image)
	{
		// Image 值拷贝：imageCalls 持有独立副本（值语义测试——改原图不影响记录）
		imageCalls.emplace_back(ImageDraw{ dest, image });
	}

	void RecordingBackend::PushClip(const Rect& rect)
	{
		// isPush=true：与 PopClip 共享一列，保持 Push/Pop 相对顺序（状态命令顺序有语义）
		clipOps.emplace_back(ClipOp{ rect, true });
	}

	void RecordingBackend::PopClip()
	{
		clipOps.emplace_back(ClipOp{ Rect{}, false });
	}

	void RecordingBackend::DrawFocusRect(const Rect& rect, const Color& color)
	{
		focusRectCalls.emplace_back(FocusRectDraw{ rect, color });
	}

}
