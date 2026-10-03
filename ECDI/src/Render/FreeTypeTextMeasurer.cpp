#include "Render/FreeTypeTextMeasurer.h"

#include "Platform/Win32/Win32RenderContext.h"   // ★ 取 HWND（同 GDIBackend / GDITextMeasurer 先例）

#include <utility>

namespace ECDI{

FreeTypeTextMeasurer::FreeTypeTextMeasurer(std::shared_ptr<FontEngine> engine)
	: m_engine(std::move(engine))
{
}

void FreeTypeTextMeasurer::Initialize(const PlatformRenderContext& context)
{
	// ★ Phase 26：与 GDITextMeasurer::Initialize 同法——取窗口 DPI 注入共享的 FontEngine
	//   ⇒ 测量基准 = 窗口 DPI = GL 渲染链基准（D-8 在 GL 侧的闭合）。
	const HWND hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();

	int dpi = (hwnd != nullptr) ? GetDpiForWindow(hwnd) : 0;
	if (dpi <= 0)
	{
		dpi = 96;   // fail-safe（无窗口 / 失败——与 DpiConversion 一致）
	}

	if (m_engine)
	{
		m_engine->SetDpi(dpi);
	}
}

Size FreeTypeTextMeasurer::MeasureText(const Font& font, const std::string& text)
{
	// ★ 返回值恒为 **DIP**（`FontEngine` 内部折好——见其 `MeasureText` 注释）
	return m_engine ? m_engine->MeasureText(font, text) : Size{};
}

float FreeTypeTextMeasurer::LineHeight(const Font& font)
{
	return m_engine ? m_engine->LineHeight(font) : font.size;
}

}
