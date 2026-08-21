#pragma once

#include "ECDI/Render/RenderCommand.h"

namespace ECDI {

	class RenderingBackend;   // 前置声明（决策 34：引用成员可前向声明）

	/// @brief 渲染命令执行器（决策 34：持引用、不拥有）
	/// @details 不认识 Widget / Backend 实现细节：
	/// - Execute 用 std::visit 分发（决策 36，穷尽性由编译器保证）
	/// - BeginFrame/EndFrame 直接转发（决策 13，两层对应不跨层）
	class Renderer {
	public:
		explicit Renderer(RenderingBackend& backend);   

		void BeginFrame();
		void Execute(const CommandBuffer& commands);
		void EndFrame();

	private:
		void ExecuteCommand(const DrawRectCommand& cmd);   // 决策 9：重载集，未来加命令只加重载
		void ExecuteCommand(const DrawTextCommand& cmd);   // D5：文本命令转发（穷尽性由 std::visit 保证）
		void ExecuteCommand(const DrawLineCommand& cmd);          // Phase 8
		void ExecuteCommand(const DrawRoundedRectCommand& cmd);  // Phase 8
		void ExecuteCommand(const DrawImageCommand& cmd);        // Phase 8
		void ExecuteCommand(const PushClipCommand& cmd);         // Phase 8（状态命令）
		void ExecuteCommand(const PopClipCommand& cmd);          // Phase 8（状态命令）
		void ExecuteCommand(const DrawFocusRectCommand& cmd);    // Phase 8
		RenderingBackend& m_backend;
	};

}
