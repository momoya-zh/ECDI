#include "ECDI/Render/Renderer.h"

#include "ECDI/Render/RenderingBackend.h"

namespace ECDI {
	// ★★ Phase 20.1：本框架**唯一**的「框架内部(DIP) → 渲染层(物理)」几何折算实现点（契约 C-11）。
	// 纯乘法、**不取整**（契约 C-8——量化留给绘图 API：GDI 的最后一步）。
	// ⚠️ 匿名 namespace 已给内部链接，**不另写 `static`**（详设 §1.3 细化 B）；唯一消费者 = 本文件
	//    ⇒ 不建新头。⚠️ 不给 Core 的 `Rect` / `Point` 加 `operator*`——那会把「DIP → 物理」降格为
	//    几何对象的普通数学操作，极易在 Widget / Layout 里被误用（Phase 20 极力避免的正是这个）。
	namespace {

		Rect ScaleRect(const Rect& rect, float scale) noexcept
		{
			return Rect{
				rect.x * scale,
				rect.y * scale,
				rect.width * scale,
				rect.height * scale
			};
		}

		Point ScalePoint(const Point& point, float scale) noexcept
		{
			return Point{ point.x * scale, point.y * scale };
		}

		/// @brief 标量折算（`width` / `cornerRadius`）
		/// @note ★ **不得**用它折算 `Font::size`——字号由后端按窗口 DPI 换算（Phase 20 批五），
		///       在 Renderer 再折一次即**双重缩放**（Q5 / C-8）。调用点**恰好 3 处**（详设 §1.3 细化 D）
		float ScaleLength(float length, float scale) noexcept
		{
			return length * scale;
		}

	}

	Renderer::Renderer(RenderingBackend& backend)
		: m_backend(backend)
	{
	}

	void Renderer::BeginFrame(const Color& background) { m_backend.BeginFrame(background); }

	// ★ Phase 20.1：`scale` **只在此处进入流程**，逐层**显式传参**（Renderer 不存成员）；
	// `commands` 是 const 引用 + 循环体是 const 引用 ⇒ **A2 / C-10 由类型系统保证**（想改缓冲写不出来）。
	void Renderer::Execute(const CommandBuffer& commands, float scale)
	{
		for (const auto& command : commands)
		{
			std::visit([this, scale](const auto& cmd) { ExecuteCommand(cmd, scale); }, command);
		}
	}

	void Renderer::EndFrame() { m_backend.EndFrame(); }

	void Renderer::ExecuteCommand(const DrawRectCommand& cmd, float scale)
	{
		m_backend.DrawRect(ScaleRect(cmd.rect, scale), cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawTextCommand& cmd, float scale)
	{
		// D5：展开转发（不向 Backend 泄漏命令类型）；★ **只折 `pos`**——`font` 原样：
		// 字号在后端按窗口 DPI 换算（Phase 20 批五），此处再折 = **双重缩放**（Q5 / 盯防 ①）。
		m_backend.DrawText(ScalePoint(cmd.pos, scale), cmd.text, cmd.color, cmd.font);
	}

	void Renderer::ExecuteCommand(const DrawLineCommand& cmd, float scale)
	{
		// 展开转发：宽度 float 契约层数据，GDI 端取整属后端细节
		// ★ 三个几何量都要折——`width` 是最易漏的那一个（盯防 ③）
		m_backend.DrawLine(ScalePoint(cmd.start, scale), ScalePoint(cmd.end, scale),
		                   ScaleLength(cmd.width, scale), cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawRoundedRectCommand& cmd, float scale)
	{
		// ★ `rect` 与 `cornerRadius` 都要折——`cornerRadius` 是最易漏的那一个（盯防 ③）
		m_backend.DrawRoundedRect(ScaleRect(cmd.rect, scale),
		                         ScaleLength(cmd.cornerRadius, scale), cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawImageCommand& cmd, float scale)
	{
		// 展开转发：Image 值语义，仅读取（命令持有独立副本）；
		// ★ **只折 `dest`**——源图像 payload **一律不动**（`image` 仍按引用传给后端，无拷贝）
		m_backend.DrawImage(ScaleRect(cmd.dest, scale), cmd.image);
	}

	void Renderer::ExecuteCommand(const PushClipCommand& cmd, float scale)
	{
		// 状态命令：转发裁剪矩形（无像素输出）——★ 与绘制用**同一个 ScaleRect**，
		// 保证 clip 与 fill **同尺度**（否则 150% 下裁剪区与绘制区错位，而 100% 下看不出来：盯防 ④）
		m_backend.PushClip(ScaleRect(cmd.rect, scale));
	}

	void Renderer::ExecuteCommand(const PopClipCommand& cmd, float scale)
	{
		// 状态命令：无字段，直接出栈。★ **两个形参都不使用**——沿 `cmd` 的既有先例，
		// **不加 `[[maybe_unused]]`、不写 `(void)scale;`**，与其余 7 个重载保持同形（盯防 ⑥）
		m_backend.PopClip();
	}

	void Renderer::ExecuteCommand(const DrawFocusRectCommand& cmd, float scale)
	{
		m_backend.DrawFocusRect(ScaleRect(cmd.rect, scale),
		                         ScaleLength(cmd.cornerRadius, scale), cmd.color);
	}

}
