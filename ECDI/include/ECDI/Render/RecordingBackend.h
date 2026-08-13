#pragma once

#include "ECDI/Render/RenderingBackend.h"
#include "ECDI/Render/TextMeasurer.h"

#include <string>
#include <vector>

namespace ECDI {

	/// @brief 测试后端（决策 12 + D4）：记录绘制调用 + 固定测量值
	/// @details 同时实现 RenderingBackend 与 TextMeasurer 是**测试便利**，
	/// 不改变两个接口的独立关系（正交能力）。textDraws 记录完整参数——
	/// 既能测"画了文本"，又能验证 Renderer 原样转发（D4 约束 2）。
	class RecordingBackend : public RenderingBackend, public TextMeasurer {

	public:
		struct DrawCall
		{

			Rect rect;

			Color color;

		};

		struct TextDraw
		{
			Point pos;          ///< 文本起点
			std::string text;   ///< 文本内容（原样转发验证）
			Color color;
			Font font;
		};

		std::vector<DrawCall> draws;   ///< DrawRect 记录（公开，测试断言）

		std::vector<TextDraw> textDraws;   ///< DrawText 记录（公开，测试断言）

		void BeginFrame() override {}           

		void DrawRect(const Rect& rect, const Color& color) override;     

		void DrawText(const Point& pos, const std::string& text,
		              const Color& color, const Font& font) override;    // textDraws.emplace_back

		void EndFrame() override {}

		// TextMeasurer：固定值（测试断言"测量被调用"，不关心精度——那是 GDIBackend 真实 GDI 的事）
		Size MeasureText(const Font&, const std::string&) override { return { 10.0f, 14.0f }; }

		float LineHeight(const Font&) override { return 14.0f; }
	};


}
