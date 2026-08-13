#pragma once

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"

#include <string>

namespace ECDI {

	/// @brief 绘制收集门面（决策 5/7/8/42，路线 X 扩展）
	/// @details 每次 Paint 在栈上创建（一次一帧，用完即毁）；
	/// 完全不认识 Widget/Renderer（纯渲染层类型）；
	/// 完全封装：只暴露绘制方法，不提供 GetCommands()（决策 8）。
	/// 路线 X：构造注入 TextMeasurer&——Widget 在 Paint 阶段可测量（对齐偏移计算）。
	class PaintContext {
	public:
		PaintContext(CommandBuffer& commands, TextMeasurer& measurer);   // 决策 42 + 路线 X

		/// @brief 绘制填充矩形（最终坐标，零坐标逻辑：原样进命令，决策 37 emplace_back）
		void DrawRect(const Rect& rect, const Color& color);

		/// @brief 绘制文本（P4：font 可省略，默认字体）
		/// @param pos 文本起点（最终坐标，对齐偏移由控件算好）
		void DrawText(const Point& pos, const std::string& text,
		              const Color& color, const Font& font = Font());

		/// @brief 测量文本尺寸（转发 m_measurer，D2 帧无关）
		Size MeasureText(const Font& font, const std::string& text);

		/// @brief 获取字体行高（转发 m_measurer；垂直居中对齐用，5.1 P7 定案）
		float LineHeight(const Font& font);

	private:
		CommandBuffer& m_commands;
		TextMeasurer& m_measurer;
	};

}
