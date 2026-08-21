#pragma once

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Image.h"
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

		/// @brief 绘制直线（Phase 8：原样进命令，GDI 端取整属后端细节）
		/// @param width 线宽（框架层 float API；Phase 8 不做亚像素线宽）
		void DrawLine(const Point& start, const Point& end,
		              float width, const Color& color);

		/// @brief 绘制圆角矩形（Phase 8：实心填充，半径钳制封在后端）
		void DrawRoundedRect(const Rect& rect, float cornerRadius,
		                     const Color& color);

		/// @brief 绘制图像（Phase 8：Image 值拷贝进命令——命令持有独立副本）
		/// @param dest 目标矩形（整图映射到 dest，尺寸不同则拉伸）
		void DrawImage(const Rect& dest, const Image& image);

		/// @brief 裁剪入栈（Phase 8：状态命令，写入缓冲维持与绘制命令的相对顺序）
		void PushClip(const Rect& rect);

		/// @brief 裁剪出栈（Phase 8：状态命令，与 PushClip 成对使用）
		void PopClip();

		/// @brief 绘制焦点框（Phase 8：指定颜色点线框，颜色由主题层赋值——Phase 9）
		void DrawFocusRect(const Rect& rect, const Color& color);

		/// @brief 测量文本尺寸（转发 m_measurer，D2 帧无关）
		Size MeasureText(const Font& font, const std::string& text);

		/// @brief 获取字体行高（转发 m_measurer；垂直居中对齐用，5.1 P7 定案）
		float LineHeight(const Font& font);

	private:
		CommandBuffer& m_commands;
		TextMeasurer& m_measurer;
	};

}
