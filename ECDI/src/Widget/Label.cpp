#include "ECDI/Widget/Label.h"

#include <utility>

namespace ECDI{

Label::Label(const std::string& text): TextWidget(text){

}

Label::Label(std::string&& text): TextWidget(std::move(text)){

}

void Label::OnPaint(PaintContext& ctx, int x, int y){

	// L3：只画文本、透明背景；用 TextWidget 默认对齐（左对齐 + 垂直居中）
	DrawTextContent(ctx, x, y);

}

}
