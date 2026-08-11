#pragma once

#include"ECDI/Core/Rect.h"
#include"ECDI/Core/Color.h"
#include"RenderCommand.h"

namespace ECDI {

	/// @brief 绘制收集门面（决策 5/7/8/42）
	/// @details 每次 Paint 在栈上创建（一次一帧，用完即毁）；
	/// 完全不认识 Widget/Renderer（纯渲染层类型）；
	/// 完全封装：只暴露绘制方法，不提供 GetCommands()（决策 8）。
	class PaintContext {
	public:
		explicit PaintContext(CommandBuffer& commands);   // 决策 42：私有成员，构造绑定

		/// @brief 绘制填充矩形（最终坐标，零坐标逻辑：原样进命令，决策 37 emplace_back）
		void DrawRect(const Rect& rect, const Color& color);

	private:
		CommandBuffer& m_commands;
	};

}
