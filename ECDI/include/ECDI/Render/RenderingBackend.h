#pragma once

#ifdef DrawText
#undef DrawText   // 防御性 undef（2026-08-16）：本头声明 DrawText 方法（公共抽象接口）——
// 用户业务代码若先 include Windows.h（wWinMain 入口），DrawText 宏会污染本头声明 →
// override 不匹配/链接错；#ifdef 包裹 = 用户零负担（宏未定义则无副作用，已定义则自动免疫）
#endif

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"

#include <string>

namespace ECDI {

class PlatformRenderContext;   // 前置声明（Initialize 参数 const&——零 include 依赖，7.1.4）

/// @brief 渲染后端抽象接口（操作粒度，决策 11/14）
/// @details
/// 能力提供者，不是命令消费者：只暴露操作（Initialize/BeginFrame/DrawRect/DrawText/EndFrame），
/// 不认识 RenderCommand/variant/Widget/PaintContext——任何系统都能直接用。
/// 替换具体绘制 API 时，上层渲染流程无需修改。

	class RenderingBackend {

	public:

		virtual ~RenderingBackend() = default;

		/// @brief 平台句柄注入（7.1.4：决策 35 代价解决——后端可替换的接入点）
		/// @param context 平台渲染上下文（Win32 后端 static_cast 取句柄——体系内约定，
		/// 非跨层 dynamic_cast；无需平台句柄的后端（如 RecordingBackend）继承默认空实现零改动）
		virtual void Initialize(const PlatformRenderContext& context) {}

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
