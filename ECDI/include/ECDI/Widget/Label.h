#pragma once

#include "ECDI/Widget/TextWidget.h"

namespace ECDI{

/// @brief 文本标签控件（第一个文本消费者，5.2）
/// @details 透明背景、只画文本；继承 TextWidget 默认对齐（左对齐 + 垂直居中）。
class Label: public TextWidget{

public:

	Label() = default;

	explicit Label(const std::string& text);

	explicit Label(std::string&& text);

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

};

}
