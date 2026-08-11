#include"ECDI/Render/Renderer.h"
#include"ECDI/Render/RenderingBackend.h"

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

}
