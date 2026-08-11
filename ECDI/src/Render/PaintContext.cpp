// PaintContext.cpp
#include"ECDI/Render/PaintContext.h"

namespace ECDI {

	PaintContext::PaintContext(CommandBuffer& commands): m_commands(commands){}

	void PaintContext::DrawRect(const Rect& rect, const Color& color){

		m_commands.emplace_back(DrawRectCommand{ rect, color });

	}

}
