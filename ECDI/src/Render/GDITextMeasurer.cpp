#include "ECDI/Render/GDITextMeasurer.h"

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

HFONT GDITextMeasurer::GetOrCreateFont(const Font& font)
{
	// D1：缓存键 = size + family（必须完整覆盖 Font 语义字段——未来加字段同步扩展键；
	// 与 GDIBackend::GetOrCreateFont 同逻辑——拆类后各持一份缓存，技术债已记）
	const auto key = std::make_pair(font.size, font.family);
	auto it = m_fontCache.find(key);
	if (it != m_fontCache.end())
	{
		return it->second;
	}

	// D3：CreateFontIndirectW + LOGFONTW（零初始化）
	LOGFONTW lf{};
	lf.lfHeight = -static_cast<LONG>(std::lround(font.size));   // 负值 = 字符高度；lround 非截断
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

	HFONT hfont = GetOrCreateFont(font);
	if (hfont)
	{
		// D2 补充 3：SelectObject 后恢复原字体（GDI 纪律）
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		const std::wstring wideText = UTF8ToWide(text);
		SIZE extent{};
		if (!wideText.empty() && GetTextExtentPoint32W(measureDC, wideText.c_str(),
		                                               static_cast<int>(wideText.size()), &extent))
		{
			result.width = static_cast<float>(extent.cx);
			result.height = static_cast<float>(extent.cy);
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return result;
}

float GDITextMeasurer::LineHeight(const Font& font)
{
	// D2：同 MeasureText 的帧无关测量模式；GetTextMetrics 精确行高（P7）
	float height = font.size;   // 兜底：字号

	HDC measureDC = GetDC(nullptr);
	if (!measureDC)
	{
		return height;
	}

	HFONT hfont = GetOrCreateFont(font);
	if (hfont)
	{
		HGDIOBJ oldFont = SelectObject(measureDC, hfont);

		TEXTMETRICW metrics{};
		if (GetTextMetricsW(measureDC, &metrics))
		{
			height = static_cast<float>(metrics.tmHeight);
		}

		SelectObject(measureDC, oldFont);
	}

	ReleaseDC(nullptr, measureDC);
	return height;
}

}
