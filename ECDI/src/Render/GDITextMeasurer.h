#pragma once

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // skill 9：含 Windows.h 的头必须紧跟宏防护（防污染 TextMeasurer 声明）
#endif

#include "ECDI/Render/TextMeasurer.h"

#include <map>
#include <string>

namespace ECDI{

/// @brief GDI 文本测量器（7.1.4 拆类：测量与渲染分离——单一职责，用户决策 A）
/// @details 纯测量零 hwnd：GetDC(NULL) 临时屏幕 DC（帧无关——测量不依赖窗口）。
/// 与 GDIBackend 拆开（GDIBackend 只做渲染）——fontCache 各持一份
/// （GetOrCreateFont 同逻辑——技术债已记：未来字体增长/第三消费者时提取 FontCache 共享）。
class GDITextMeasurer : public TextMeasurer{
public:
	GDITextMeasurer() = default;

	~GDITextMeasurer() override;   ///< fontCache 清理（GDI 对象 10,000 上限纪律）

	Size MeasureText(const Font& font, const std::string& text) override;

	float LineHeight(const Font& font) override;

private:
	HFONT GetOrCreateFont(const Font& font);   ///< 与 GDIBackend 同逻辑：缓存取/建 HFONT（键 = size+family）

	std::map<std::pair<float, std::string>, HFONT> m_fontCache;   ///< Font→HFONT 缓存（键必须完整覆盖 Font 语义字段）
};

}
