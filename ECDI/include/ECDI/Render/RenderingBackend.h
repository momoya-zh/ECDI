#pragma once

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"

#include <string>

namespace ECDI {

/// @brief 渲染后端抽象接口（操作粒度，决策 11/14）
/// @details
/// 能力提供者，不是命令消费者：只暴露操作（BeginFrame/DrawRect/DrawText/EndFrame），
/// 不认识 RenderCommand/variant/Widget/PaintContext——任何系统都能直接用。
/// 替换具体绘制 API 时，上层渲染流程无需修改。

	class RenderingBackend {

	public:

		virtual ~RenderingBackend() = default;

		/// @brief 帧开始（后端建立绘制目标：清屏/拿 HDC/建缓冲）
		virtual void BeginFrame() = 0;

		/// @brief 绘制一个填充矩形（最终坐标）
		/// @param rect  矩形区域（Rect(float)，决策 25：转换封闭在后端内）
		/// @param color 填充颜色（决策 21/23：ToByte Clamp 在后端内）
		virtual void DrawRect(const Rect& rect, const Color& color) = 0;

		/// @brief 绘制文本（最终起点，D5 详细设计：展开参数，操作粒度）
		/// @param pos   文本起点（对齐偏移由控件用 Measure 算好，D9）
		/// @param text  UTF-8 文本（编码转换封闭在后端内，D6）
		/// @param color 前景色（背景透明，P8）
		/// @param font  字体描述符（实例化封在后端内，D1/D3）
		virtual void DrawText(const Point& pos, const std::string& text,
		                      const Color& color, const Font& font) = 0;

		/// @brief 帧结束（后端提交绘制结果：BitBlt/换缓冲）
		virtual void EndFrame() = 0;

	};



}
