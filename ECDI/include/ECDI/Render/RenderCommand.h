#pragma once

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Image.h"

#include <variant>
#include <vector>

namespace ECDI {

	/// @brief 绘制矩形命令（纯数据，决策 2/37）
	/// @details 死数据：值语义、无虚函数、无执行逻辑、无 Renderer/Backend 依赖。
	/// 存最终坐标（坐标转换在 Paint 阶段完成，Renderer 零上下文执行）。
	struct DrawRectCommand
	{
		Rect rect;
		Color color;
	};

	/// @brief 绘制文本命令（纯数据，详细设计 D3）
	/// @details 死数据：起点 + UTF-8 文本 + 前景色 + 字体描述符（Font 值拷贝）。
	/// 对齐偏移由控件用 Measure 算好后填 pos（D9：Backend 不做排版）。
	struct DrawTextCommand
	{
		Point pos;           ///< 文本起点（最终坐标，对齐偏移已算好）
		std::string text;    ///< UTF-8 文本（公共层编码，转换封在平台层）
		Color color;         ///< 前景色（背景透明，由控件先画）
		Font font;
	};

	/// @brief 绘制直线命令（纯数据，Phase 8）
	/// @details 死数据：起点/终点（最终坐标）+ 宽度 + 颜色。宽度为框架层 API
	/// （float），GDI 端舍入取整属后端实现细节（详细设计 §8.1）。
	struct DrawLineCommand
	{
		Point start;          ///< 直线起点（最终坐标）
		Point end;            ///< 直线终点（最终坐标）
		float width = 1.0f;   ///< 线宽（>= 0；0/极小值 GDI 端按 1px 处理）
		Color color;          ///< 线色
	};

	/// @brief 绘制圆角矩形命令（纯数据，Phase 8）
	/// @details 死数据：边界矩形 + 圆角半径 + 填充色。仅实心填充（无边框）；
	/// cornerRadius 由后端钳制到 [0, min(w,h)/2]（详细设计 §8.2）。
	struct DrawRoundedRectCommand
	{
		Rect rect;                 ///< 边界矩形（最终坐标）
		float cornerRadius = 0.0f; ///< 圆角半径（>= 0；后端钳制）
		Color color;               ///< 填充色
	};

	/// @brief 绘制图像命令（纯数据，Phase 8）
	/// @details 死数据：目标矩形（缩放语义，整图映射到 dest）+ Image 值拷贝。
	/// 空图像（width==0 || height==0）不产生绘制；仅读取 Image，不持有引用。
	struct DrawImageCommand
	{
		Rect dest;   ///< 目标矩形（整个源图像映射到此矩形，尺寸不同则拉伸）
		Image image; ///< 像素数据值拷贝（32bpp premultiplied BGRA）
	};

	/// @brief 裁剪入栈命令（状态命令，Phase 8）
	/// @details 不产生像素输出：与后续绘制命令求交，PopClip 出栈恢复。
	/// 状态命令与绘制命令在缓冲中的相对顺序有语义，不可重排。
	struct PushClipCommand
	{
		Rect rect; ///< 裁剪矩形（最终坐标，与当前裁剪区求交）
	};

	/// @brief 裁剪出栈命令（状态命令，Phase 8）
	/// @details 恢复上一层裁剪区；缓冲为空栈时后端跳过（防御，契约层允许无损）。
	struct PopClipCommand
	{
	};

	/// @brief 绘制焦点框命令（纯数据，Phase 8；9.5 R4 后带圆角）
	/// @details 死数据：边界矩形 + 圆角半径 + 颜色。框架级"指定颜色点线框"能力——
	/// 不依赖系统 DrawFocusRect（双缓冲 + 系统样式问题），颜色由主题层控制（Phase 9）。
	/// cornerRadius > 0 时后端画圆角点线框（RoundRect + PS_DOT）——Button 圆角焦点框消费（9.5）。
	struct DrawFocusRectCommand
	{
		Rect rect;          ///< 焦点框边界（最终坐标）
		float cornerRadius; ///< 圆角半径（0 = 直角；>0 = 圆角——9.5 R4 焦点框跟随控件圆角）
		Color color;        ///< 点线颜色（主题层赋值）
	};

	/// @brief 渲染命令 = 类型安全的 variant 多类型表示（决策 3）
	/// @details 新增命令类型必须同步给 Renderer 加 ExecuteCommand 重载——
	/// std::visit 穷尽性保证漏加即编译错误（决策 36）。
	/// PushClip/PopClip 为状态命令（无像素输出）；其余为绘制命令。
	using RenderCommand = std::variant<
		DrawRectCommand, DrawTextCommand,
		DrawLineCommand, DrawRoundedRectCommand, DrawImageCommand,
		PushClipCommand, PopClipCommand, DrawFocusRectCommand>;

	/// @brief 命令缓冲 = vector（决策 4，Window 持有跨帧复用，读写靠引用形态分离）
	using CommandBuffer = std::vector<RenderCommand>;

}
