#pragma once

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // skill 9：含 Windows.h 的头必须紧跟宏防护（防污染 TextMeasurer 声明）
#endif

#include "ECDI/Render/TextMeasurer.h"

#include <cstddef>
#include <map>
#include <string>
#include <tuple>

namespace ECDI{

/// @brief GDI 文本测量器（7.1.4 拆类：测量与渲染分离——单一职责，用户决策 A）
/// @details ★ **Phase 26 起持 HWND**（此前「纯测量零 hwnd」）——**测量基准 DPI = 窗口 DPI**
///          （`GetDpiForWindow`），与 `GDIBackend` 同基准 ⇒ **闭合审计 D-8**（测量链原用屏幕 DC
///          的 `LOGPIXELSX`，与渲染链的窗口 DPI 基准不一致）。★ 代价如实登记：本类不再是
///          「零平台句柄」——`Initialize` 注入句柄；未注入 / 无效时 dpi 按 96 fail-safe。
///          ★ Phase 26 另加**测量结果缓存**（键含 DPI；命中时连 `GetDC` 都省）。
/// 与 GDIBackend 拆开（GDIBackend 只做渲染）——fontCache 各持一份
/// （GetOrCreateFont 同逻辑——技术债已记：未来字体增长/第三消费者时提取 FontCache 共享）。
class GDITextMeasurer : public TextMeasurer{
public:
	GDITextMeasurer() = default;

	~GDITextMeasurer() override;   ///< fontCache 清理（GDI 对象 10,000 上限纪律）

	/// @brief 平台句柄注入（Phase 26：与 `RenderingBackend::Initialize` **结构对称**）
	/// @details 取窗口 HWND ⇒ 测量基准 DPI = `GetDpiForWindow`（与 `GDIBackend` 同基准——D-8 闭合）。
	///          ★ 本方法**可选**（不调用时 dpi 按 96 fail-safe，行为等同 100% 缩放）。
	void Initialize(const PlatformRenderContext& context) override;

	Size MeasureText(const Font& font, const std::string& text) override;

	float LineHeight(const Font& font) override;

	/// @brief 测量缓存未命中计数（★ **观测缝**——命中与否的**返回值相同**，无法凭返回值区分）
	/// @details ★ 内部件的公开方法，**非公共 API**；服务 T26-12 断言 + 基准指标。
	std::size_t MeasureCacheMissCount() const noexcept { return m_measureCacheMissCount; }

private:
	/// @brief 缓存取/建 HFONT（与 GDIBackend 同逻辑；键 = size+family+**dpi**）
	/// @param dpi ★ **测量基准 DPI**（Phase 26 起 = **窗口 DPI**，`GetDpiForWindow`——
	///            此前 = 调用点测量 DC 的 `LOGPIXELSX`；见类注释 D-8）
	HFONT GetOrCreateFont(const Font& font, int dpi);

	/// ★ Phase 20（△21）：缓存键**含 DPI**——与 `GDIBackend` 同理（键隔离）。
	/// 键 = (size, family, **测量基准 dpi**)。
	std::map<std::tuple<float, std::string, int>, HFONT> m_fontCache;   ///< Font→HFONT 缓存（键含 DPI——必须完整覆盖 Font 语义字段 + 换算基准）

	HWND m_hwnd = nullptr;   ///< ★ Phase 26：窗口句柄（`Initialize` 注入——测量基准 DPI 的来源）

	/// ★ Phase 26（△9b）：**测量结果缓存**——消掉「每帧 × 每行」的
	/// `GetDC` + `SelectObject` + `GetTextExtentPoint32W`（性能方向②）。
	/// 键 = (text, size, family, **dpi**)——★ 含 DPI（跨屏自动失效），与 `m_fontCache` 同口径。
	/// ★ `font.size`（float）在生成 key 前**不得额外 rounding**（同一 `Font` 重复测量须逐位命中）。
	std::map<std::tuple<std::string, float, std::string, int>, Size> m_measureCache;

	/// ★ 上限 ⇒ 达上限 `clear()`（O(1)，防无界增长——TextBox 反复编辑会不断产生新 `text` 键）。
	/// ★ 淘汰策略（LRU）不做——记为详设开放项 O9。
	static constexpr std::size_t kMaxMeasureCache = 4096;

	std::size_t m_measureCacheMissCount = 0;   ///< ★ 观测缝（供 T26-12 + 基准指标）
};

}
