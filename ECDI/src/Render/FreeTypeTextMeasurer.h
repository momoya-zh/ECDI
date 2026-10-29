#pragma once

#include "ECDI/Render/TextMeasurer.h"
#include "Render/FontEngine.h"

#include <cstdint>   // ★ Phase 40：5 参重载的 byteOffsets 表项
#include <memory>
#include <vector>    // ★ Phase 40：5 参重载的 cps / byteOffsets 入参

namespace ECDI{

/// @brief FreeType 文本测量器（`TextMeasurer` 的 GL 侧实现）
/// @details 与 `GLRenderer` **共享同一个 `FontEngine` 实例**（**同源同基准**——Phase 26 结论 S6）。
///          本类只管「`FontEngine` metrics → `TextMeasurer` 契约（**DIP 返回值**）」的映射：
///          **不持 GPU 资源、不碰 Widget / RenderCommand**。
/// @note ★ `MeasureText` / `LineHeight` 的返回值**恒为 DIP**（`FontEngine` 内部按 `pixelSize`
//        度量后折回）——对基准 DPI 近似不敏感（沿 `TextMeasurer.h` 的既有契约表述）。
class FreeTypeTextMeasurer : public TextMeasurer{
public:
	explicit FreeTypeTextMeasurer(std::shared_ptr<FontEngine> engine);

	/// @brief 平台上下文注入（Phase 26：取窗口 DPI ⇒ 测量基准 = 窗口 DPI）
	/// @details ★ 与 `RenderingBackend::Initialize` **结构对称**（详设契约 C-3）；
	///          但**职责不同**——本方法**可选**（不调用时 `FontEngine` 的 dpi 保持默认 96）。
	void Initialize(const PlatformRenderContext& context) override;

	Size MeasureText(const Font& font, const std::string& text) override;

	/// @brief 断行适配（★ Phase 29 △4：**转发** `FontEngine::FitText`——advance memo 生产路径）
	/// @details ★ 不走 `TextMeasurer.h` 的默认二分体（兼容 fallback）——本类与 `GDITextMeasurer`
	///          同为**生产链**，各自原生实现（FT = memo 累积 / GDI = `GetTextExtentExPointW`）。
	TextFit FitText(const Font& font, const std::string& text,
	                std::size_t startCp, float maxWidth) override;

	/// @brief 断行适配（★ Phase 40：表感知重载——转发 `FontEngine::FitText` 的 5 参）
	/// @details 与 4 参同语义；无 engine 时兜底「放行全串、宽 0」，但码点数取**表**（O(1)）而非
	///          `ByteOffsetToCodepointIndex(text, text.size())`（O(N)）。
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
	                const std::vector<char32_t>& cps,
	                const std::vector<std::uint32_t>& byteOffsets) override;

	float LineHeight(const Font& font) override;

private:
	/// ★ 与 `GLRenderer` **共享**（外部评审 §9/§10：保持 `shared_ptr`——不引入更复杂的所有权）
	std::shared_ptr<FontEngine> m_engine;
};

}
