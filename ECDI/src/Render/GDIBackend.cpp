#include "ECDI/Render/GDIBackend.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/String.h"
#include "ECDI/Platform/Win32/Win32RenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ECDI {

GDIBackend::GDIBackend() = default;

GDIBackend::~GDIBackend()
{
	// 决策 31：缓冲统一释放（空句柄防御在 ReleaseBackBuffer 内部）
	ReleaseBackBuffer();

	// D1：字体缓存统一清理（GDI 对象 10,000 上限纪律——渲染 DrawText 用）
	for (auto& entry : m_fontCache)
	{
		DeleteObject(entry.second);
	}
	m_fontCache.clear();
}

void GDIBackend::Initialize(const PlatformRenderContext& context)
{
	// 7.1.4：平台句柄注入（取代 SetHwnd 过渡）——static_cast 体系内约定：
	// GDIBackend 是 Win32 后端，识别 Win32RenderContext 是"同体系内"（非跨层 dynamic_cast）
	m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
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

void GDIBackend::DrawLine(const Point& start, const Point& end,
                          float width, const Color& color)
{
	// Phase 8 §8.1：宽度 lround 取整 + 下限 1px（lround(0.4f)=0 → CreatePen(0) 实为 1px cosmetic pen，
	// 显式下限使契约确定）；所有坐标同样 lround（亚像素线段 Phase 8 不做）
	const LONG penWidth = (std::max)(1L, std::lround(width));
	HPEN pen = CreatePen(PS_SOLID, penWidth, ToColorRef(color));
	if (!pen)
	{
		return;   // 决策 30：局部失败跳过
	}

	HPEN oldPen = static_cast<HPEN>(SelectObject(m_memoryDC, pen));
	MoveToEx(m_memoryDC, std::lround(start.x), std::lround(start.y), nullptr);
	LineTo(m_memoryDC, std::lround(end.x), std::lround(end.y));

	// 决策 24 风格：GDI 对象每次创建/销毁（避免 10,000 句柄上限）
	SelectObject(m_memoryDC, oldPen);
	DeleteObject(pen);
}

void GDIBackend::DrawRoundedRect(const Rect& rect, float cornerRadius,
                                 const Color& color)
{
	// Phase 8 §8.2：空矩形 no-op（契约层确定边界，避免 GDI 未定义行为）
	if (rect.width <= 0.0f || rect.height <= 0.0f)
	{
		return;
	}

	// 半径钳制到 [0, min(w,h)/2]：GDI RoundRect 对过大半径行为未定义（§8.2 契约）
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);
	const LONG clampedRadius = std::clamp(std::lround(cornerRadius),
	                                      0L, (std::min)(w, h) / 2);

	// 实心填充：NULL_PEN 无边框 + 实心画刷 + RoundRect
	HPEN nullPen = CreatePen(PS_NULL, 0, 0);
	HBRUSH brush = CreateSolidBrush(ToColorRef(color));
	if (!nullPen || !brush)
	{
		if (nullPen) DeleteObject(nullPen);
		if (brush) DeleteObject(brush);
		return;
	}

	HPEN oldPen = static_cast<HPEN>(SelectObject(m_memoryDC, nullPen));
	HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(m_memoryDC, brush));

	// 决策 24 风格：left/top/right/bottom 开区间 [x, x+width)（与 DrawRect 一致）
	RECT rc{};
	rc.left = static_cast<LONG>(rect.x);
	rc.top = static_cast<LONG>(rect.y);
	rc.right = static_cast<LONG>(rect.x + rect.width);
	rc.bottom = static_cast<LONG>(rect.y + rect.height);
	RoundRect(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom,
	          clampedRadius, clampedRadius);

	SelectObject(m_memoryDC, oldPen);
	SelectObject(m_memoryDC, oldBrush);
	DeleteObject(nullPen);
	DeleteObject(brush);
}

void GDIBackend::DrawImage(const Rect& dest, const Image& image)
{
	// Phase 8 §8.3：空图像 no-op（width == 0 || height == 0 → 不绘制，契约层确定边界）
	if (image.width == 0 || image.height == 0)
	{
		return;
	}

	// 契约防御：stride >= width*4 且 pixels >= stride*height（§3.1）——不满足 = 数据损坏，跳过
	const int width = image.width;
	const int height = image.height;
	if (image.stride < width * 4 ||
	    image.pixels.size() < static_cast<size_t>(image.stride) * static_cast<size_t>(height))
	{
		return;
	}

	// 32bpp 顶向下 DIB：biHeight 取负 → DIB row 0 = 图像顶行（无需行翻转，§8.3）
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC dibDC = CreateCompatibleDC(m_memoryDC);
	if (!dibDC)
	{
		return;
	}
	void* dibBits = nullptr;
	HBITMAP dibBitmap = CreateDIBSection(dibDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
	if (!dibBitmap || !dibBits)
	{
		if (dibBitmap) DeleteObject(dibBitmap);
		DeleteDC(dibDC);
		return;
	}

	// 逐行拷贝：按 row*stride 定位（§8.3 不能整体 memcpy——stride 可能大于 width*4）
	const int dibStride = width * 4;
	BYTE* dst = static_cast<BYTE*>(dibBits);
	for (int row = 0; row < height; ++row)
	{
		memcpy(dst + static_cast<size_t>(row) * dibStride,
		       image.pixels.data() + static_cast<size_t>(row) * image.stride,
		       static_cast<size_t>(dibStride));
	}

	HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(dibDC, dibBitmap));

	// AlphaBlend（msimg32）：AC_SRC_OVER + AC_SRC_ALPHA = 源含 premultiplied alpha（§8.3）
	// dest 为拉伸语义：整图映射到 dest 矩形
	BLENDFUNCTION blend{};
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;
	AlphaBlend(m_memoryDC,
	           std::lround(dest.x), std::lround(dest.y),
	           std::lround(dest.width), std::lround(dest.height),
	           dibDC, 0, 0, width, height, blend);

	SelectObject(dibDC, oldBitmap);
	DeleteObject(dibBitmap);
	DeleteDC(dibDC);
}

void GDIBackend::PushClip(const Rect& rect)
{
	// Phase 8 §8.5：SaveDC 失败返回 0——0 不是合法 RestoreDC ID，不入栈（防御）
	const int savedId = SaveDC(m_memoryDC);
	if (savedId == 0)
	{
		return;
	}

	// 与当前裁剪区求交（默认裁剪区 = 整个内存缓冲）；最终坐标 lround 统一
	RECT rc{};
	rc.left = std::lround(rect.x);
	rc.top = std::lround(rect.y);
	rc.right = std::lround(rect.x + rect.width);
	rc.bottom = std::lround(rect.y + rect.height);
	IntersectClipRect(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom);

	m_clipStack.push_back(savedId);
}

void GDIBackend::PopClip()
{
	// Phase 8 §8.5：栈空跳过（防御）+ savedId != 0 双重校验（RestoreDC 合法 ID）
	if (m_clipStack.empty())
	{
		return;
	}
	const int savedId = m_clipStack.back();
	m_clipStack.pop_back();
	if (savedId != 0)
	{
		RestoreDC(m_memoryDC, savedId);
	}
}

void GDIBackend::DrawFocusRect(const Rect& rect, const Color& color)
{
	// Phase 8 §8.4：框架级"指定颜色点线框"——CreatePen(PS_DOT) + NULL_BRUSH + Rectangle。
	// 不用系统 DrawFocusRect（User32 XOR 绘制与双缓冲/自绘样式冲突；颜色由主题层控制）
	HPEN pen = CreatePen(PS_DOT, 1, ToColorRef(color));
	if (!pen)
	{
		return;
	}

	HPEN oldPen = static_cast<HPEN>(SelectObject(m_memoryDC, pen));
	HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(m_memoryDC, GetStockObject(NULL_BRUSH)));

	RECT rc{};
	rc.left = std::lround(rect.x);
	rc.top = std::lround(rect.y);
	rc.right = std::lround(rect.x + rect.width);
	rc.bottom = std::lround(rect.y + rect.height);
	Rectangle(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom);

	SelectObject(m_memoryDC, oldPen);
	SelectObject(m_memoryDC, oldBrush);
	DeleteObject(pen);
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
