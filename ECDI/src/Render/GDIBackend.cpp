#include "Render/GDIBackend.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Render/CoverageRaster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace ECDI {

namespace{

// ── 9.5 Alpha Primitive 补强：半透明实心合成（约束 1：预乘 BGRA + AC_SRC_ALPHA）──
// 复用 Phase 8 DrawImage §8.3 已验证链路（32bpp 顶降 DIB + AlphaBlend）。
// Phase 8.6：圆角分支接入覆盖度抗锯齿（第 8.6 阶段**修订 9.5「约束 2」**——alpha 合成与
// 几何覆盖度正交）；alpha == 1 的调用不进入此函数（走 GDI 快速路径或角补丁路径）。

/// @brief 半透明实心绘制（矩形 / 圆角矩形共用）
/// @param target 目标 DC（内存缓冲）
/// @param rect   目标矩形（最终坐标）
/// @param color  填充色（RGB 任意，alpha < 1 触发——本函数假定调用方已判 a < 1）
/// @param cornerRadius 圆角半径（0 = 矩形；>0 = 圆角矩形）
/// @param mask   覆盖度掩码（Phase 8.6；`nullptr` = 掩码不可用 → 退化既有二值判定）
/// @details 创建临时 32bpp 预乘 DIB → 逐像素填充 → AlphaBlend(AC_SRC_ALPHA) → 释放。
/// 每次调用创建/销毁 DIB（低频场景可接受，YAGNI 不做缓存池）。
void BlendAlphaSolid(HDC target, const Rect& rect, const Color& color, float cornerRadius,
                     const CornerCoverageMask* mask)
{
	// 空矩形 no-op（契约层确定边界——与 DrawRect 一致）
	if (rect.width <= 0.0f || rect.height <= 0.0f)
	{
		return;
	}

	const int width = std::lround(rect.width);
	const int height = std::lround(rect.height);
	if (width <= 0 || height <= 0)
	{
		return;
	}

	// 约束 1：预乘 BGRA（AC_SRC_ALPHA 要求 RGB 已乘 alpha——与 §8.3 同规则）
	const auto ToByte = [](float v)
	{
		const float clamped = std::clamp(v, 0.0f, 1.0f);
		return static_cast<BYTE>(clamped * 255.0f + 0.5f);
	};
	const BYTE alphaByte = ToByte(color.a);
	if (alphaByte == 0)
	{
		return;   // 全透明 no-op
	}
	const BYTE b = ToByte(color.b * color.a);
	const BYTE g = ToByte(color.g * color.a);
	const BYTE r = ToByte(color.r * color.a);

	// 32bpp 顶降 DIB（负 biHeight → 行序顶向下，与 §8.3 一致）
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC dibDC = CreateCompatibleDC(target);
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

	// 逐像素填充（BGRA 内存序：字节序 = B,G,R,A）
	BYTE* dst = static_cast<BYTE*>(dibBits);
	const int dibStride = width * 4;

	if (cornerRadius <= 0.0f)
	{
		// 矩形：整块填充预乘色
		for (int row = 0; row < height; ++row)
		{
			BYTE* line = dst + static_cast<size_t>(row) * dibStride;
			for (int col = 0; col < width; ++col)
			{
				line[col * 4 + 0] = b;
				line[col * 4 + 1] = g;
				line[col * 4 + 2] = r;
				line[col * 4 + 3] = alphaByte;
			}
		}
	}
	else
	{
		// 圆角矩形：Phase 8.6 覆盖度抗锯齿（D11——几何语义统一：所有圆角都带覆盖度）
		// 四角以掩码取覆盖度；掩码不可用时退化为既有二值判定（不越界读、不崩溃）
		const float radius = (std::min)(cornerRadius, (std::min)(static_cast<float>(width), static_cast<float>(height)) / 2.0f);
		const float radiusSq = radius * radius;
		const int R = static_cast<int>(std::lround(radius));

		// 预乘色分量（循环外）：每像素只再乘覆盖度 cov
		const float ba = color.b * color.a;
		const float ga = color.g * color.a;
		const float ra = color.r * color.a;

		// 掩码可用性：内部不变量（半径一致）。不匹配 → 退化二值（防御，不越界读）
		const bool maskUsable = (mask != nullptr && mask->radius == R);

		for (int row = 0; row < height; ++row)
		{
			BYTE* line = dst + static_cast<size_t>(row) * dibStride;
			for (int col = 0; col < width; ++col)
			{
				// 像素中心坐标（半像素偏移——避免边缘整像素误判）
				const float cx = static_cast<float>(col) + 0.5f;
				const float cy = static_cast<float>(row) + 0.5f;

				// 覆盖度：矩形主体（三条带区）恒为 1；四角由掩码给出
				float cov = 1.0f;

				CornerId corner = CornerId::TopLeft;
				int localI = 0;
				int localJ = 0;
				bool inCorner = false;

				// 左上角
				if (cx < radius && cy < radius)
				{
					inCorner = true; corner = CornerId::TopLeft;
					localI = col;                    localJ = row;
				}
				// 右上角
				else if (cx > static_cast<float>(width) - radius && cy < radius)
				{
					inCorner = true; corner = CornerId::TopRight;
					localI = col - (width - R);      localJ = row;
				}
				// 左下角
				else if (cx < radius && cy > static_cast<float>(height) - radius)
				{
					inCorner = true; corner = CornerId::BottomLeft;
					localI = col;                    localJ = row - (height - R);
				}
				// 右下角
				else if (cx > static_cast<float>(width) - radius && cy > static_cast<float>(height) - radius)
				{
					inCorner = true; corner = CornerId::BottomRight;
					localI = col - (width - R);      localJ = row - (height - R);
				}

				if (inCorner)
				{
					if (maskUsable)
					{
						cov = static_cast<float>(
							mask->At(MaskIndexX(localI, R, corner), MaskIndexY(localJ, R, corner)))
							/ 255.0f;
					}
					else
					{
						// 防御退化：既有二值语义（像素中心判定）
						const float ccx = (corner == CornerId::TopLeft || corner == CornerId::BottomLeft)
						                  ? radius : (static_cast<float>(width) - radius);
						const float ccy = (corner == CornerId::TopLeft || corner == CornerId::TopRight)
						                  ? radius : (static_cast<float>(height) - radius);
						const float dx = cx - ccx;
						const float dy = cy - ccy;
						cov = ((dx * dx + dy * dy) <= radiusSq) ? 1.0f : 0.0f;
					}
				}

				if (cov <= 0.0f)
				{
					line[col * 4 + 0] = 0;
					line[col * 4 + 1] = 0;
					line[col * 4 + 2] = 0;
					line[col * 4 + 3] = 0;   // 圆角外透明
					continue;
				}

				// 预乘 BGRA（约束 1）；覆盖度作为额外 alpha 调制（最终 alpha = color.a × cov）
				line[col * 4 + 0] = ToByte(ba * cov);
				line[col * 4 + 1] = ToByte(ga * cov);
				line[col * 4 + 2] = ToByte(ra * cov);
				line[col * 4 + 3] = ToByte(color.a * cov);
			}
		}
	}

	// AlphaBlend（AC_SRC_ALPHA：源含预乘 alpha——§8.3 同款）
	HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(dibDC, dibBitmap));
	BLENDFUNCTION blend{};
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;
	AlphaBlend(target,
	           std::lround(rect.x), std::lround(rect.y),
	           width, height,
	           dibDC, 0, 0, width, height, blend);

	SelectObject(dibDC, oldBitmap);
	DeleteObject(dibBitmap);
	DeleteDC(dibDC);
}

} // namespace

GDIBackend::GDIBackend() = default;

GDIBackend::~GDIBackend()
{
	// 决策 31：缓冲统一释放（空句柄防御在 ReleaseBackBuffer 内部）
	ReleaseBackBuffer();

	// Phase 8.6：角补丁绘制面释放（幂等——内部空指针检查）
	m_patchSurface.Release();

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

void GDIBackend::BeginFrame(const Color& background)
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

	// 决策 16（Phase 18 修订）：清屏用**本帧背景色**——默认白由 Window 给出（本层不持有默认值）
	// 决策 24：画刷每次创建/销毁（无缓存）——与 DrawRect 同款
	// 决策 21/23：ToColorRef 内含 ToByte Clamp；★ alpha 被忽略（COLORREF 无 alpha 通道）
	HBRUSH brush = CreateSolidBrush(ToColorRef(background));
	if (brush)
	{
		FillRect(m_memoryDC, &client, brush);
		DeleteObject(brush);
	}
	// else：决策 30「失败 → 局部跳过」——底色不刷新，本帧其余绘制照常（不加断言）
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
	// 9.5 Alpha 补强：a < 1 → 半透明合成（预乘 DIB + AlphaBlend）；a == 1 → 原 GDI 快速路径（零开销不回归）
	if (color.a < 1.0f)
	{
		BlendAlphaSolid(m_memoryDC, rect, color, 0.0f, nullptr);
		return;
	}

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
	// Phase 8.6：显式类型链（量化 → 上界 → 钳制 → int）——effective 整数半径 R 即掩码缓存键
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);

	const LONG roundedRadius   = static_cast<LONG>(std::lround(cornerRadius));   // ① 量化
	const LONG maxRadius       = (std::min)(w, h) / 2;                           // ② 上界
	const LONG effectiveRadius = std::clamp(roundedRadius, 0L, maxRadius);       // ③ 钳制

	const int R = static_cast<int>(effectiveRadius);   // ④ effective 整数半径 = 缓存键

	// ── 路径选择（Phase 8.6 §5.2）──
	// ① R == 0 或 AA 关闭 → legacy（与改造前逐位一致）
	if (R == 0 || !m_antiAliasing)
	{
		DrawRoundedRectLegacy(rect, cornerRadius, color);   // ⚠️ 传原始 cornerRadius（内部自行钳制）
		return;
	}

	// ② a == 1 需要角补丁绘制面——准备失败则落 legacy（fail-safe：宁可无 AA，不可缺角）
	if (color.a >= 1.0f && !m_patchSurface.Ensure(m_memoryDC, R))
	{
		Logger::Log(LogLevel::Warning,
			L"GDIBackend: patch surface unavailable - rounded rect AA skipped");
		DrawRoundedRectLegacy(rect, cornerRadius, color);
		return;
	}

	// ③ 取掩码（键 = effective 整数半径——掩码只依赖半径，与尺寸/位置/颜色正交）
	const CornerCoverageMask& mask = m_cornerMaskCache.Get(R);

	if (color.a >= 1.0f)
	{
		DrawRoundedRectOpaqueAA(rect, R, color, mask);      // §5.3：3 带 + 4 角补丁
	}
	else
	{
		DrawRoundedRectBlendedAA(rect, R, color, mask);     // §5.4：全矩形 DIB + 覆盖度
	}
}

void GDIBackend::DrawRoundedRectLegacy(const Rect& rect, float cornerRadius,
                                       const Color& color)
{
	// ⚠️ 本函数为 Phase 8.6 之前两条分支的**原样搬移（零改写）**——
	//    R == 0 或 AA 关闭时走此路，以保证「AA 关闭 / R == 0」与改造前逐位一致。

	// 半径钳制到 [0, min(w,h)/2]：GDI RoundRect 对过大半径行为未定义（§8.2 契约）
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);
	const LONG clampedRadius = std::clamp(std::lround(cornerRadius),
	                                      0L, (std::min)(w, h) / 2);

	// 9.5 Alpha 补强：a < 1 → 半透明合成（圆角同款实心语义）；
	// a == 1 → 原 GDI 快速路径。mask 传 nullptr（legacy 不做几何抗锯齿）
	if (color.a < 1.0f)
	{
		BlendAlphaSolid(m_memoryDC, rect, color, static_cast<float>(clampedRadius), nullptr);
		return;
	}

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

	// GDI RoundRect 参数语义 = 圆角椭圆直径（非半径）——cornerRadius 契约是"半径"，
	// 必须转直径传入（×2），否则实际圆角只有设定值一半（历史 bug，2026-08-26 修复：
	// 与 BlendAlphaSolid 数学裁剪路径 radius 语义对齐）
	RoundRect(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom,
	          clampedRadius * 2, clampedRadius * 2);

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

void GDIBackend::DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color)
{
	// Phase 8 §8.4：框架级"指定颜色点线框"。2026-08-27 重写：**手动画段状（统一周界段状）**——
	// PS_DOT 线型在带 clip 的 DC（Widget::Paint PushClip 的 SaveDC/IntersectClipRect 环境）
	// 下退化变实线（实测复现），改 MoveToEx/LineTo 手动画段，任何 DC 状态稳定段状。
	// 周界 = 4 直线（直角）或 4 直线 + 4 圆弧（圆角），沿周界连续 3px 实 + 3px 空交替——
	// 避免"采样段内再段状"导致的实心连串 + 断口（2026-08-27 修复）。
	// 命令层（RenderCommand/RecordingBackend）语义不变。
	HPEN pen = CreatePen(PS_SOLID, 1, ToColorRef(color));
	if (!pen)
	{
		return;
	}

	HPEN oldPen = static_cast<HPEN>(SelectObject(m_memoryDC, pen));

	// GDI 开区间约定：可见像素 = [left, right-1] × [top, bottom-1]（right/bottom 列被
	// PushClip(控件边界 [x,x+w)) 裁掉——手动画段需显式内缩，2026-08-27 修复）
	const float fLeft = static_cast<float>(std::lround(rect.x));
	const float fTop = static_cast<float>(std::lround(rect.y));
	const float fRight = static_cast<float>(std::lround(rect.x + rect.width) - 1);
	const float fBottom = static_cast<float>(std::lround(rect.y + rect.height) - 1);

	// ⚠️ 2026-09-11 修复：圆角弧心与半径必须基于「几何边界」[x, x+w) × [y, y+h)
	//    （与 DrawRoundedRect / DrawRect 的形状语义一致），而非上面内缩后的可见像素边界。
	//    原实现四角一律用 fRight/fBottom 定位弧心 ⇒ 右上 / 左下 / 右下三角的弧心各内移 1px
	//    （左上角因 fLeft/fTop 本就不内缩而恰好对齐）→ 与背景圆角的弧错位，
	//    在圆角与直线交接处表现为焦点框「内收」。
	const float gRight = static_cast<float>(std::lround(rect.x + rect.width));
	const float gBottom = static_cast<float>(std::lround(rect.y + rect.height));
	const float r = (std::min)(cornerRadius, (std::min)((gRight - fLeft), (gBottom - fTop)) * 0.5f);

	// ── 周界子段（按顺序：上 → 右上 → 右 → 右下 → 下 → 左下 → 左 → 左上）──
	// 每个子段 = 直线（起终点）或圆弧（圆心 + 起止角）。s 为沿周界的弧长参数。
	struct Seg { int type; float x1, y1, x2, y2; float cx, cy, a1, a2; float len; };
	std::vector<Seg> segs;
	const float kPi = 3.14159265f;
	const auto AddLine = [&](float x1, float y1, float x2, float y2){
		const float dx = x2 - x1, dy = y2 - y1;
		segs.push_back(Seg{ 0, x1, y1, x2, y2, 0, 0, 0, 0, std::sqrt(dx * dx + dy * dy) });
	};
	const auto AddArc = [&](float cx, float cy, float a1, float a2){
		segs.push_back(Seg{ 1, 0, 0, 0, 0, cx, cy, a1, a2, r * (a2 - a1) });
	};
	if (r <= 0.0f)
	{
		AddLine(fLeft, fTop, fRight, fTop);       // 上
		AddLine(fRight, fTop, fRight, fBottom);   // 右
		AddLine(fRight, fBottom, fLeft, fBottom); // 下
		AddLine(fLeft, fBottom, fLeft, fTop);     // 左
	}
	else
	{
		AddLine(fLeft + r, fTop, gRight - r, fTop);          // 上
		AddArc(gRight - r, fTop + r, -kPi * 0.5f, 0.0f);     // 右上 -90°→0°
		AddLine(fRight, fTop + r, fRight, fBottom - r);      // 右
		AddArc(gRight - r, gBottom - r, 0.0f, kPi * 0.5f);   // 右下 0°→90°
		AddLine(gRight - r, fBottom, fLeft + r, fBottom);    // 下
		AddArc(fLeft + r, gBottom - r, kPi * 0.5f, kPi);     // 左下 90°→180°
		AddLine(fLeft, fBottom - r, fLeft, fTop + r);        // 左
		AddArc(fLeft + r, fTop + r, kPi, kPi * 1.5f);        // 左上 180°→270°
	}

	// 周界采样：s（沿周界弧长）→ 坐标
	float total = 0.0f;
	for (const Seg& s : segs)
		total += s.len;
	// ⚠️ 弧端点在「几何边界」上（x/y 可达 gRight / gBottom），而可见像素范围是
	//    [fLeft, fRight] × [fTop, fBottom]——统一 clamp，避免无 PushClip 时越界 1px 绘制。
	const auto PointAt = [&](float s){
		float px = fLeft;
		float py = fTop;
		float acc = 0.0f;
		for (const Seg& seg : segs)
		{
			if (s < acc + seg.len + 1e-4f)
			{
				const float u = s - acc;
				if (seg.type == 0)
				{
					// 直线线性插值
					const float tx = (seg.len > 0.0f) ? u / seg.len : 0.0f;
					px = seg.x1 + (seg.x2 - seg.x1) * tx;
					py = seg.y1 + (seg.y2 - seg.y1) * tx;
				}
				else
				{
					// 圆弧：弧长 → 角度
					const float a = seg.a1 + u / r;
					px = seg.cx + r * std::cos(a);
					py = seg.cy + r * std::sin(a);
				}
				break;   // 命中即止（保留原「提前 return」的短路语义）
			}
			acc += seg.len;
		}
		// clamp 到可见像素范围（`(std::min)`/`(std::max)` 加括号——抑制 Windows 的 min/max 宏）
		return std::pair<float, float>{ (std::min)((std::max)(px, fLeft), fRight),
		                                (std::min)((std::max)(py, fTop), fBottom) };
	};

	// ── 沿周界连续段状：3px 实 + 3px 空交替，实心段细分画线 ──
	constexpr float kDash = 3.0f;
	constexpr float kGap = 3.0f;
	constexpr int kSub = 8;   // 实心段细分（3px / 8 段 ≈ 0.4px 步长——弧线平滑）
	float t = 0.0f;
	bool draw = true;
	while (t < total)
	{
		const float step = draw ? kDash : kGap;
		const float t2 = (std::min)(t + step, total);
		if (draw)
		{
			auto [px, py] = PointAt(t);
			for (int i = 1; i <= kSub; ++i)
			{
				const auto [x, y] = PointAt(t + (t2 - t) * static_cast<float>(i) / static_cast<float>(kSub));
				MoveToEx(m_memoryDC, std::lround(px), std::lround(py), nullptr);
				LineTo(m_memoryDC, std::lround(x), std::lround(y));
				px = x;
				py = y;
			}
		}
		t = t2;
		draw = !draw;
	}

	SelectObject(m_memoryDC, oldPen);
	DeleteObject(pen);
}

HFONT GDIBackend::GetOrCreateFont(const Font& font)
{
	// ★ Phase 20（△20）：`Font::size` 是**公共 API 的 DIP**，而 `lfHeight` 要的是**物理像素**
	//   ⇒ 按**窗口 DPI** 换算，并把 DPI 纳入缓存键（跨屏后旧 DPI 的 HFONT 自然不命中）。
	// ⚠️ 此处**不调平台层的 `DipToPixels`**——契约 C2（换算函数只应出现在 `src/Platform/Win32/`）
	//    与分层纪律（`src/Render/` 不得依赖 `src/Platform/Win32/`）。**后端自行做 DIP → 物理**
	//    正是「后端内部 = 物理」这一层的职责。因 `Font::size` 是 float，本链路沿用
	//    **float 口径**（与测量链路同族——详设 §3.5：文本链路口径 ≠ 几何链路口径）。
	const int dpi = GetDpiForWindow(m_hwnd);   // 0 / 失败 ⇒ 按 96（fail-safe，与 DpiConversion 一致）

	const int effectiveDpi = (dpi > 0) ? dpi : 96;

	// D1：缓存键 = size + family + **dpi**（键隔离——见头文件成员注释）
	const auto key = std::make_tuple(font.size, font.family, effectiveDpi);
	auto it = m_fontCache.find(key);
	if (it != m_fontCache.end())
	{
		return it->second;
	}

	// D3：CreateFontIndirectW + LOGFONTW（零初始化）
	LOGFONTW lf{};
	// ★ Phase 20：DIP → 物理像素（float 口径）+ lround 取整（lfHeight 是 LONG）
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

// ── Phase 8.6：圆角覆盖度抗锯齿（详细设计 §5.3–§5.6）────────────────

void GDIBackend::DrawRoundedRectOpaqueAA(const Rect& rect, int R, const Color& color,
                                         const CornerCoverageMask& mask)
{
	const LONG x = static_cast<LONG>(rect.x);
	const LONG y = static_cast<LONG>(rect.y);
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);

	// ── ① 三条实心带（覆盖度恒为 1 → 与覆盖度混合路径数学等价，初设 §2.2 已证）──
	// 因 clamp 保证 2R <= min(w,h)，三条带的宽高恒 >= 0；零尺寸者跳过
	// （真圆 w == h == 2R 时三条带全零尺寸 → 仅四角补丁拼成整圆；胶囊同理）
	const RECT bands[3] = {
		{ x,     y + R,     x + w,     y + h - R },   // 中带（全宽）
		{ x + R, y,         x + w - R, y + R     },   // 上带（去左右两角）
		{ x + R, y + h - R, x + w - R, y + h     }    // 下带（去左右两角）
	};

	HBRUSH brush = CreateSolidBrush(ToColorRef(color));
	if (!brush)
	{
		return;   // 决策 30：局部失败跳过
	}

	for (const RECT& rc : bands)
	{
		if (rc.right > rc.left && rc.bottom > rc.top)
		{
			FillRect(m_memoryDC, &rc, brush);
		}
	}

	DeleteObject(brush);   // 决策 24：1 次建销服务 3 条带

	// ── ② 四个角补丁（同一份 canonical 掩码 + 索引变换）──
	const struct { LONG x; LONG y; CornerId corner; } kCorners[4] = {
		{ x,         y,         CornerId::TopLeft     },
		{ x + w - R, y,         CornerId::TopRight    },
		{ x,         y + h - R, CornerId::BottomLeft  },
		{ x + w - R, y + h - R, CornerId::BottomRight }
	};

	for (const auto& c : kCorners)
	{
		FillPatchFromMask(mask, color, c.corner);         // 写入 PatchSurface（预乘 BGRA，R×R）
		BlendPatch(m_memoryDC, static_cast<int>(c.x), static_cast<int>(c.y), R);
	}
}

void GDIBackend::DrawRoundedRectBlendedAA(const Rect& rect, int R, const Color& color,
                                          const CornerCoverageMask& mask)
{
	// 不变量防御：掩码半径必须等于 R（同一缓存键必然成立）；不匹配则退化为硬边，不崩溃
	const bool maskUsable = (mask.radius == R);

	BlendAlphaSolid(m_memoryDC, rect, color, static_cast<float>(R),
	                maskUsable ? &mask : nullptr);
}

void GDIBackend::FillPatchFromMask(const CornerCoverageMask& mask, const Color& color,
                                   CornerId corner)
{
	// Phase 19.1：像素合成已抽到平台无关的 `Render/CoverageRaster`（算法逐位不变）——
	// 本函数只剩「提供目标缓冲」这一件 GDI 侧的事。这样一来，抗锯齿的**像素构造**不再
	// 属于 GDI 后端，换后端时可直接复用（本相位后端可替换性分析的落点之一）。
	//
	// ⚠️ 行宽必须用 **DIB 实际行宽** = PatchSurface.size * 4，**不是** mask.radius * 4：
	//    `PatchSurface` 只增不减（Ensure 仅在 requiredSize > size 时重建），故 size >= R 恒成立；
	//    若按 R 定位行，则「同一后端先画过大半径、再画小半径」时整体行错位，
	//    补丁内容被垂直压缩 → 圆角渲染错乱（2026-09-11 修复）。
	//    每行只写前 R 个像素（R*4 字节），正好落在 [0,R)×[0,R) 有效区内。
	RasterizeCornerPatch(static_cast<std::uint8_t*>(m_patchSurface.bits),
	                     m_patchSurface.size * 4, mask, color, corner);
}

void GDIBackend::BlendPatch(HDC target, int x, int y, int size)
{
	BLENDFUNCTION blend{};
	blend.BlendOp             = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat         = AC_SRC_ALPHA;   // 源为预乘 BGRA

	AlphaBlend(target, x, y, size, size,
	           m_patchSurface.dc, 0, 0, size, size, blend);
}

bool GDIBackend::PatchSurface::Ensure(HDC reference, int requiredSize)
{
	if (requiredSize <= 0 || reference == nullptr)
	{
		return false;
	}

	if (dc != nullptr && size >= requiredSize)
	{
		return true;   // 已够大（**只增不减**——初设 §6.2）
	}

	// 先建后替（与 EnsureBackBuffer 决策 38 同款：新资源就绪前不动旧资源）
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = requiredSize;
	bmi.bmiHeader.biHeight      = -requiredSize;   // 负 = 顶降（行序自上而下）
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC newDC = CreateCompatibleDC(reference);
	if (!newDC)
	{
		return false;
	}

	void* newBits = nullptr;
	HBITMAP newBitmap = CreateDIBSection(newDC, &bmi, DIB_RGB_COLORS, &newBits, nullptr, 0);
	if (!newBitmap || !newBits)
	{
		if (newBitmap) DeleteObject(newBitmap);
		DeleteDC(newDC);
		return false;
	}

	HBITMAP oldInNewDC = static_cast<HBITMAP>(SelectObject(newDC, newBitmap));

	Release();   // 释放旧资源（严格逆序）

	dc = newDC;
	bitmap = newBitmap;
	oldBitmap = oldInNewDC;
	bits = newBits;
	size = requiredSize;

	return true;
}

void GDIBackend::PatchSurface::Release()
{
	// 决策 20/31 严格逆序：不能删除仍被选中的对象
	if (dc)
	{
		SelectObject(dc, oldBitmap);
	}
	if (bitmap)
	{
		DeleteObject(bitmap);
	}
	if (dc)
	{
		DeleteDC(dc);
	}

	dc = nullptr;
	bitmap = nullptr;
	oldBitmap = nullptr;
	bits = nullptr;
	size = 0;
}

}
