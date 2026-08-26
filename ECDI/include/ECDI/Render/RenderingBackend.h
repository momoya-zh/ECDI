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
#include "ECDI/Core/Image.h"

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

		/// @brief 绘制直线（Phase 8，详细设计 §8.1）
		/// @param start 起点（最终坐标）
		/// @param end   终点（最终坐标）
		/// @param width 线宽（框架层 API 为 float；GDI 端 lround 取整 + 下限 1px
		///             属后端实现细节——Phase 8 不做亚像素线宽）
		/// @param color 线色
		virtual void DrawLine(const Point& start, const Point& end,
		                      float width, const Color& color) = 0;

		/// @brief 绘制圆角矩形（Phase 8，详细设计 §8.2）
		/// @param rect         边界矩形（最终坐标）
		/// @param cornerRadius 圆角半径（后端钳制到 [0, min(w,h)/2]，保证 GDI 行为确定）
		/// @param color        填充色（实心填充，无边框）
		virtual void DrawRoundedRect(const Rect& rect, float cornerRadius,
		                             const Color& color) = 0;

		/// @brief 绘制图像（Phase 8，详细设计 §8.3）
		/// @param dest  目标矩形（整图映射到 dest，尺寸不同则拉伸——值语义，仅读取）
		/// @param image 像素数据（32bpp premultiplied BGRA；空图像不绘制）
		virtual void DrawImage(const Rect& dest, const Image& image) = 0;

		/// @brief 裁剪入栈（Phase 8，状态命令——无像素输出）
		/// @param rect 裁剪矩形（与当前裁剪区求交；后续绘制命令受其约束）
		virtual void PushClip(const Rect& rect) = 0;

		/// @brief 裁剪出栈（Phase 8，状态命令）
		/// @details 恢复上一层裁剪区；栈为空时跳过（防御，契约层允许无损）。
		virtual void PopClip() = 0;

		/// @brief 绘制焦点框（Phase 8，详细设计 §8.4；9.5 R4 加圆角）
		/// @param rect         焦点框边界（最终坐标）
		/// @param cornerRadius 圆角半径（0 = 直角；>0 = 圆角点线框——Button 圆角焦点消费）
		/// @param color        点线颜色（主题层赋值——Phase 9）
		virtual void DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color) = 0;

		/// @brief 帧结束（后端提交绘制结果：BitBlt/换缓冲）
		virtual void EndFrame() = 0;

	};



}
