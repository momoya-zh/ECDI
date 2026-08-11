#include"ECDI/Render/RecordingBackend.h"

namespace ECDI {

	void RecordingBackend::DrawRect(const Rect& rect, const Color& color)
	{
		draws.emplace_back(DrawCall{ rect, color });
	}

}
