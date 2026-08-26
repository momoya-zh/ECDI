#include "ECDI/Render/Renderer.h"

#include "ECDI/Render/RenderingBackend.h"

namespace ECDI {

	Renderer::Renderer(RenderingBackend& backend)
		: m_backend(backend)
	{
	}

	void Renderer::BeginFrame() { m_backend.BeginFrame(); }

	void Renderer::Execute(const CommandBuffer& commands)
	{
		for (const auto& command : commands)
		{
			std::visit([this](const auto& cmd) { ExecuteCommand(cmd); }, command);
		}
	}

	void Renderer::EndFrame() { m_backend.EndFrame(); }

	void Renderer::ExecuteCommand(const DrawRectCommand& cmd)
	{
		m_backend.DrawRect(cmd.rect, cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawTextCommand& cmd)
	{
		// D5：展开转发（不向 Backend 泄漏命令类型；参数原样传递）
		m_backend.DrawText(cmd.pos, cmd.text, cmd.color, cmd.font);
	}

	void Renderer::ExecuteCommand(const DrawLineCommand& cmd)
	{
		// 展开转发：宽度 float 契约层数据，GDI 端取整属后端细节
		m_backend.DrawLine(cmd.start, cmd.end, cmd.width, cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawRoundedRectCommand& cmd)
	{
		m_backend.DrawRoundedRect(cmd.rect, cmd.cornerRadius, cmd.color);
	}

	void Renderer::ExecuteCommand(const DrawImageCommand& cmd)
	{
		// 展开转发：Image 值语义，仅读取（命令持有独立副本）
		m_backend.DrawImage(cmd.dest, cmd.image);
	}

	void Renderer::ExecuteCommand(const PushClipCommand& cmd)
	{
		// 状态命令：转发裁剪矩形（无像素输出）
		m_backend.PushClip(cmd.rect);
	}

	void Renderer::ExecuteCommand(const PopClipCommand& cmd)
	{
		// 状态命令：无字段，直接出栈
		m_backend.PopClip();
	}

	void Renderer::ExecuteCommand(const DrawFocusRectCommand& cmd)
	{
		m_backend.DrawFocusRect(cmd.rect, cmd.cornerRadius, cmd.color);
	}

}
