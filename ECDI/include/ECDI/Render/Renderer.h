#pragma once

#include "ECDI/Render/RenderCommand.h"

namespace ECDI {

	class RenderingBackend;   // 前置声明（决策 34：引用成员可前向声明）

	/// @brief 渲染命令执行器（决策 34：持引用、不拥有）
	/// @details 不认识 Widget / Backend 实现细节：
	/// - Execute 用 std::visit 分发（决策 36，穷尽性由编译器保证）
	/// - BeginFrame/EndFrame 直接转发（决策 13，两层对应不跨层）
	/// - ★ Phase 20.1：`Execute` 携带**坐标缩放因子** `scale`，把**框架几何(DIP)** 折算为
	///   **物理像素**后交给后端——这是本框架**唯一**的「框架内部 → 渲染层」换算边。
	///   ⚠️ `Renderer` **不保存** `scale`（它是**本次 Execute 调用的执行上下文参数**，
	///   不是帧状态）⇒ **不存在 `BeginFrame` → `Execute` 的顺序依赖**；
	///   ⚠️ `Renderer` **只知道 `scale`、不知道 DPI**（`scale` 恒为 `float`，不依赖 Windows）。
	class Renderer {
	public:
		explicit Renderer(RenderingBackend& backend);   

		void BeginFrame(const Color& background);   ///< Phase 18：透明转发（背景色是决策层输入，Renderer 不持有）
		/// @brief 执行一批命令（★ Phase 20.1：`scale` = 本次执行的 DIP → 物理 几何缩放因子）
		/// @param commands 命令缓冲（几何恒为 **DIP**；★ 本函数**不修改**它——A2 / 契约 C-10）
		/// @param scale 坐标缩放因子（**finite + positive**；★ 语义 = **本次 Execute 调用**的
		///        上下文参数，**不是** `Renderer` 的状态、也不要求 `BeginFrame` 先被调用）。
		///        默认 `1.0f` = 恒等（既有调用点零改动 + 100% DPI 下逐位零回归）。
		///        ⚠️ 本类**只做** `geometry * scale`，**不认识 DPI**——1.25 是 120 DPI 还是别的
		///        变换，本类不关心、也无法知道（平台无关性的落点）。
		void Execute(const CommandBuffer& commands, float scale = 1.0f);
		void EndFrame();

	private:
		void ExecuteCommand(const DrawRectCommand& cmd, float scale);   // 决策 9：重载集，未来加命令只加重载
		void ExecuteCommand(const DrawTextCommand& cmd, float scale);   // D5：文本命令转发（穷尽性由 std::visit 保证）
		void ExecuteCommand(const DrawLineCommand& cmd, float scale);          // Phase 8
		void ExecuteCommand(const DrawRoundedRectCommand& cmd, float scale);  // Phase 8
		void ExecuteCommand(const DrawImageCommand& cmd, float scale);        // Phase 8
		void ExecuteCommand(const PushClipCommand& cmd, float scale);         // Phase 8（状态命令）
		void ExecuteCommand(const PopClipCommand& cmd, float scale);          // Phase 8（状态命令）
		void ExecuteCommand(const DrawFocusRectCommand& cmd, float scale);    // Phase 8
		RenderingBackend& m_backend;
	};

}
