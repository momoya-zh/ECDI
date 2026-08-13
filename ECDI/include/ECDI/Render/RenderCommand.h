#pragma once

#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"

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

	/// @brief 渲染命令 = 类型安全的 variant 多类型表示（决策 3）
	/// @details 新增命令类型必须同步给 Renderer 加 ExecuteCommand 重载——
	/// std::visit 穷尽性保证漏加即编译错误（决策 36）
	using RenderCommand = std::variant<DrawRectCommand, DrawTextCommand>;

	/// @brief 命令缓冲 = vector（决策 4，Window 持有跨帧复用，读写靠引用形态分离）
	using CommandBuffer = std::vector<RenderCommand>;

}
