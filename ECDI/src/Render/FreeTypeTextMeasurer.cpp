#include "Render/FreeTypeTextMeasurer.h"

#include "ECDI/Core/UTF8.h"   // ★ Phase 29 △4：无 engine 兜底路径的码点数上界换算
#include "Platform/Win32/Win32RenderContext.h"   // ★ 取 HWND（同 GDIBackend / GDITextMeasurer 先例）

#include <limits>   // ★ Phase 29 △4：numeric_limits（码点数上界探测）
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

TextFit FreeTypeTextMeasurer::FitText(const Font& font, const std::string& text,
                                      std::size_t startCp, float maxWidth)
{
	// ★ Phase 29 △4：FT 生产路径 = 转发 `FontEngine::FitText`（advance memo 累积，O(n)——
	//   **不走** `TextMeasurer.h` 的默认二分体）。★ 无 engine ⇒ 放行全串（宽度 0）——
	//   与 `MeasureText` 返回 `Size{}` 的无 engine 口径同族（调用方自行收敛）。
	if (!m_engine)
	{
		const std::size_t totalCp = ByteOffsetToCodepointIndex(text, text.size());
		return TextFit{ (startCp < totalCp) ? (totalCp - startCp) : 0, 0.0f };
	}
	return m_engine->FitText(font, text, startCp, maxWidth);
}

float FreeTypeTextMeasurer::LineHeight(const Font& font)
{
	return m_engine ? m_engine->LineHeight(font) : font.size;
}

}
