#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Core/Font.h"

#include <string>

namespace ECDI {

	/// @brief 文本测量能力接口（独立于 RenderingBackend，路线 X 定案）
	/// @details
	/// 只知道 Font + text → 尺寸/行高；不接触 Widget/PaintContext/RenderCommand；
	/// 与 RenderingBackend **无继承关系**（正交能力接口，测试类可同时实现二者——纯测试便利）。
	class TextMeasurer {
	public:
		virtual ~TextMeasurer() = default;

		/// @brief 测量文本尺寸（控件对齐偏移计算依赖此，D5 职责确认）
		virtual Size MeasureText(const Font& font, const std::string& text) = 0;

		/// @brief 字体行高（单行文本垂直居中用——精确值，非字号估算，P7）
		virtual float LineHeight(const Font& font) = 0;
	};

}
