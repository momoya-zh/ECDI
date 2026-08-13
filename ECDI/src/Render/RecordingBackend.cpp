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

}
