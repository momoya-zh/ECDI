#include "Render/GDITextMeasurer.h"

#include "ECDI/Core/String.h"

#include <algorithm>
#include <cmath>

namespace ECDI{

GDITextMeasurer::~GDITextMeasurer()
{
	// D1：字体缓存统一清理（GDI 对象 10,000 上限纪律）
	for (auto& entry : m_fontCache)
	{
		DeleteObject(entry.second);
	}
	m_fontCache.clear();
}

HFONT GDITextMeasurer::GetOrCreateFont(const Font& font, int dpi)
{
	// ★ Phase 20（△21）：与 GDIBackend 同逻辑，但**基准 DPI 不同**——本类"纯测量零 hwnd"，
	//   基准 = **调用点传入的测量 DC 的 `LOGPIXELSX`**（屏幕 DC）。
	//   测量链路"自己进、自己出"用同一基准 ⇒ 返回值对基准 DPI **近似不敏感**（初设 §2.2.4）；
	//   与渲染侧（窗口 DC 基准）的差异是**已记的近似**（详设 O1）。
	// D1：缓存键 = size + family + **dpi**（键隔离——与 GDIBackend 同理）
	const int effectiveDpi = (dpi > 0) ? dpi : 96;

	const auto key = std::make_tuple(font.size, font.family, effectiveDpi);
	auto it = m_fontCache.find(key);
	if (it != m_fontCache.end())
	{
		return it->second;
	}

	// D3：CreateFontIndirectW + LOGFONTW（零初始化）
	LOGFONTW lf{};
	// ★ Phase 20：DIP → 物理像素（float 口径）——使本地 HFONT 与渲染侧同尺度
	lf.lfHeight = -static_cast<LONG>(std::lround(font.size * effectiveDpi / 96.0));
	lf.lfCharSet = DEFAULT_CHARSET;                             // 关键：中文/Unicode 正常
	lf.lfWeight = FW_NORMAL;

	if (!font.family.empty())
	{
		// D3 约束 5：family UTF-8 → UTF-16，封闭在平台层
		const std::wstring wideFamily = UTF8ToWide(font.family);
		// D3 约束 7：LF_FACESIZE 长度限制 + 结尾 L'\0'（(std::min) 括号抑制 Windows 的 min 宏）
		const size_t length = (std::min)(wideFamily.size(), static_cast<size_t>(LF_FACESIZE - 1));
		wideFamily.copy(lf.lfFaceName, length);
		lf.lfFaceName[length] = L'\0';
	}

	HFONT hfont = CreateFontIndirectW(&lf);
	if (!hfont)
	{
		return nullptr;   // 决策 30：创建失败跳过（不缓存失败）
	}

	m_fontCache.emplace(key, hfont);
	return hfont;
}

Size GDITextMeasurer::MeasureText(const Font& font, const std::string& text)
{
	// D2：帧无关测量——GetDC(NULL) 临时屏幕 DC（仅测量，不承担绘制职责）
	Size result{};

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return result;
	}

	// ★ Phase 20（△21）：**测量基准 DPI = 本测量 DC 的 `LOGPIXELSX`**
	//   （契约 C7：`GetDeviceCaps` 在全库**仅此一处**，用途即"测量基准"）。
	const int dpi = GetDeviceCaps(measureDC, LOGPIXELSX);

	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		// D2 补充 3：SelectObject 后恢复原字体（GDI 纪律）
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		const std::wstring wideText = UTF8ToWide(text);
		SIZE extent{};
		if (!wideText.empty() && GetTextExtentPoint32W(measureDC, wideText.c_str(),
		                                               static_cast<int>(wideText.size()), &extent))
		{
			// ★ Phase 20：物理像素 → **DIP**（`TextMeasurer` 的接口语义）。
			//   保留 **float**——测量链路口径（详设 §3.5：**不得**与 `PixelsToDip` 的整数口径合并）。
			const float toDip = 96.0f / static_cast<float>((dpi > 0) ? dpi : 96);

			result.width = static_cast<float>(extent.cx) * toDip;
			result.height = static_cast<float>(extent.cy) * toDip;
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return result;
}

float GDITextMeasurer::LineHeight(const Font& font)
{
	// D2：同 MeasureText 的帧无关测量模式；GetTextMetrics 精确行高（P7）
	float height = font.size;   // 兜底：字号（**已是 DIP**——与返回值单位一致）

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return height;
	}

	// ★ Phase 20（△21）：与 MeasureText 同一基准（本测量 DC 的 LOGPIXELSX）
	const int dpi = GetDeviceCaps(measureDC, LOGPIXELSX);

	HFONT hfont = GetOrCreateFont(font, dpi);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		TEXTMETRICW metrics{};
		if (GetTextMetricsW(measureDC, &metrics))
		{
			// ★ Phase 20：物理像素 → DIP（保留 float——测量链路口径）
			height = static_cast<float>(metrics.tmHeight)
				* (96.0f / static_cast<float>((dpi > 0) ? dpi : 96));
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return height;
}

}
