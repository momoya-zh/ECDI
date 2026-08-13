#include "ECDI/Render/GDIBackend.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/String.h"

#include <algorithm>
#include <cmath>

namespace ECDI {

GDIBackend::GDIBackend() = default;

GDIBackend::~GDIBackend()
{
	// 决策 31：缓冲统一释放（空句柄防御在 ReleaseBackBuffer 内部）
	ReleaseBackBuffer();

	// D1：字体缓存统一清理（GDI 对象 10,000 上限纪律）
	for (auto& entry : m_fontCache)
	{
		DeleteObject(entry.second);
	}
	m_fontCache.clear();
}

void GDIBackend::SetHwnd(HWND hwnd)
{
	m_hwnd = hwnd;
}

void GDIBackend::BeginFrame()
{
	// 决策 32：Begin/End 严格配对
	FRAMEWORK_ASSERT(!m_inFrame);
	m_inFrame = true;

	// 决策 17：帧 DC 从 BeginPaint 获取，EndFrame 严格配对释放
	m_windowDC = BeginPaint(m_hwnd, &m_ps);

	// 决策 15/26/38：懒创建 + 尺寸自检重建（先建后替，失败时旧资源仍在）
	EnsureBackBuffer();

	// 决策 27：完整客户区（rcPaint 无效区域不能当 Buffer 尺寸）
	RECT client{};
	GetClientRect(m_hwnd, &client);

	// 决策 16：清屏白（Root 白底是平台语义，不是 Widget 命令）
	FillRect(m_memoryDC, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
}

void GDIBackend::EnsureBackBuffer()
{
	RECT client{};
	GetClientRect(m_hwnd, &client);
	const int width = client.right - client.left;
	const int height = client.bottom - client.top;

	// 尺寸未变且有缓冲：复用（决策 15/26）
	if (m_memoryDC && m_bitmap && m_bitmapWidth == width && m_bitmapHeight == height)
	{
		return;
	}

	// 决策 38：先创建新资源 → 全部成功 → 替换成员 → 释放旧（事务性）
	HDC newDC = CreateCompatibleDC(m_windowDC);
	HBITMAP newBitmap = CreateCompatibleBitmap(m_windowDC, width, height);
	if (!newDC || !newBitmap)
	{
		// 决策 30：Back Buffer 创建失败 = 框架级错误（旧资源未动）
		FRAMEWORK_ASSERT(false);
		if (newDC) DeleteDC(newDC);
		if (newBitmap) DeleteObject(newBitmap);
		return;
	}

	// 决策 20：保存新 DC 的 old 选择（释放时先恢复再删，GDI 铁律）
	HBITMAP oldInNewDC = static_cast<HBITMAP>(SelectObject(newDC, newBitmap));

	// 替换成员前先释放旧资源（决策 20/31 严格逆序）
	ReleaseBackBuffer();

	m_memoryDC = newDC;
	m_bitmap = newBitmap;
	m_oldBitmap = oldInNewDC;
	m_bitmapWidth = width;
	m_bitmapHeight = height;
}

void GDIBackend::ReleaseBackBuffer()
{
	// 决策 20/31：严格逆序 —— 不能删除 selected 对象
	if (m_memoryDC)
	{
		SelectObject(m_memoryDC, m_oldBitmap);
	}
	if (m_bitmap)
	{
		DeleteObject(m_bitmap);
	}
	if (m_memoryDC)
	{
		DeleteDC(m_memoryDC);
	}
	m_memoryDC = nullptr;
	m_bitmap = nullptr;
	m_oldBitmap = nullptr;
	m_bitmapWidth = 0;
	m_bitmapHeight = 0;
}

void GDIBackend::DrawRect(const Rect& rect, const Color& color)
{
	// 决策 21/23：Color→COLORREF 转换封闭在此（ToByte Clamp 在消费边界，DrawText 共用）
	const COLORREF colorRef = ToColorRef(color);

	// 决策 25：Rect→RECT 直接截断；决策 24 实现：right/left 开区间 [x, x+width)
	RECT rc{};
	rc.left = static_cast<LONG>(rect.x);
	rc.top = static_cast<LONG>(rect.y);
	rc.right = static_cast<LONG>(rect.x + rect.width);
	rc.bottom = static_cast<LONG>(rect.y + rect.height);

	// 决策 24：画刷每次创建/销毁（无缓存）；决策 30：局部失败跳过
	HBRUSH brush = CreateSolidBrush(colorRef);
	if (!brush)
	{
		return;
	}
	FillRect(m_memoryDC, &rc, brush);
	DeleteObject(brush);
}

COLORREF GDIBackend::ToColorRef(const Color& color)
{
	// 决策 21/23：ToByte Clamp 在消费边界（DrawRect/DrawText 共用）
	const auto ToByte = [](float v)
	{
		const float clamped = std::clamp(v, 0.0f, 1.0f);
		return static_cast<BYTE>(clamped * 255.0f + 0.5f);
	};
	return RGB(ToByte(color.r), ToByte(color.g), ToByte(color.b));
}

void GDIBackend::DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font)
{
	// D6：公共层 UTF-8 → Win32 UTF-16（复用 Core/String.h，封闭在平台层）
	const std::wstring wideText = UTF8ToWide(text);
	if (wideText.empty())
	{
		return;
	}

	// D1+D3：缓存取/建 HFONT（失败跳过，决策 30 风格）
	HFONT hfont = GetOrCreateFont(font);
	if (!hfont)
	{
		return;
	}

	SelectObject(m_memoryDC, hfont);
	SetBkMode(m_memoryDC, TRANSPARENT);           // P5：背景透明（文本叠在控件背景上）
	SetTextColor(m_memoryDC, ToColorRef(color));  // P8：前景色

	// D6 细节：坐标截断（决策 25 统一）；TextOutW 长度是 wchar 数（非字节）
	TextOutW(m_memoryDC,
	         static_cast<LONG>(pos.x),
	         static_cast<LONG>(pos.y),
	         wideText.c_str(),
	         static_cast<int>(wideText.size()));
}

HFONT GDIBackend::GetOrCreateFont(const Font& font)
{
	// D1：缓存键 = size + family（必须完整覆盖 Font 语义字段——未来加字段同步扩展键）
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

Size GDIBackend::MeasureText(const Font& font, const std::string& text)
{
	// D2：帧无关测量——Paint 阶段测量发生在 BeginFrame 前、帧内 DC 不存在，
	// 故用 GetDC(NULL) 临时屏幕 DC（仅测量，不承担绘制职责）
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

float GDIBackend::LineHeight(const Font& font)
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

void GDIBackend::EndFrame()
{
	// 决策 32：Begin/End 严格配对
	FRAMEWORK_ASSERT(m_inFrame);
	m_inFrame = false;

	// 决策 29：完整 BitBlt（SRCCOPY）——与完整重绘模型一致
	if (m_memoryDC && m_windowDC)
	{
		BitBlt(m_windowDC, 0, 0, m_bitmapWidth, m_bitmapHeight, m_memoryDC, 0, 0, SRCCOPY);
	}

	// 决策 17：BeginPaint/EndPaint 严格配对
	EndPaint(m_hwnd, &m_ps);
}

}
