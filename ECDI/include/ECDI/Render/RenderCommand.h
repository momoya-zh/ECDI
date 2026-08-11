#pragma once

#include"ECDI/Core/Rect.h"
#include"ECDI/Core/Color.h"

#include<variant>
#include<vector>

namespace ECDI {

	/// @brief 绘制矩形命令（纯数据，决策 2/37）
	/// @details 死数据：值语义、无虚函数、无执行逻辑、无 Renderer/Backend 依赖。
	/// 存最终坐标（坐标转换在 Paint 阶段完成，Renderer 零上下文执行）。
	struct DrawRectCommand
	{
		Rect rect;
		Color color;
	};

	/// @brief 渲染命令 = 类型安全的 variant 多类型表示（决策 3）
	/// @details 第一版单成员，未来扩展
	using RenderCommand = std::variant<DrawRectCommand>;

	/// @brief 命令缓冲 = vector（决策 4，Window 持有跨帧复用，读写靠引用形态分离）
	using CommandBuffer = std::vector<RenderCommand>;

}