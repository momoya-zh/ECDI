#include "Widget/ScrollContent.h"   // 内部头（src 内相对 include——同 CaptionButton 先例）

#include "ECDI/Widget/ScrollView.h"

namespace ECDI{

ScrollContent::ScrollContent(ScrollView& owner)
	: m_owner(owner){

	// 无内容：几何由 ScrollView::SetContentExtent 同步（= 内容 extent）

}

int ScrollContent::GetContentOffsetX() const noexcept{

	return m_owner.GetScrollOffsetX();

}

int ScrollContent::GetContentOffsetY() const noexcept{

	return m_owner.GetScrollOffsetY();

}

}
