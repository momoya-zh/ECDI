#pragma once

// ⚠️ Win32 宏防护必须先于任何 ECDI 头：Windows.h 可能已通过其他路径（如 WindowMessageHandler.h）
// 先被展开，`#define DrawText DrawTextW` 已生效——若不在 RenderingBackend.h 之前 #undef，
// 基类声明会被污染成 DrawTextW，导致本类 override 不匹配
// （"only virtual member functions can be marked 'override'"）。
#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 本类用 TextOutW，不碰 GDI DrawTextW
#endif

#include"ECDI/Core/Point.h"
#include"ECDI/Render/RenderingBackend.h"
#include"ECDI/Render/TextMeasurer.h"

#include<map>
#include<string>

namespace ECDI {

/// @brief GDI 渲染后端（默认实现，双缓冲完全封装，决策 11/14；D1-D6 文本能力）
/// @details
/// - 所有权：Window 值成员，构造体内 SetHwnd（决策 35）
/// - 双缓冲：BeginFrame 懒创建 + 尺寸自检（决策 15/26），先建后替重建（决策 38）
/// - HDC/HBITMAP/BitBlt 全是实现细节，Window/Renderer 零感知（决策 14）
/// - 文本（路线 X）：实现 RenderingBackend::DrawText + TextMeasurer——
///   HFONT 缓存（D1）、临时屏幕 DC 测量（D2）、CreateFontIndirectW（D3）全封闭在本类
class GDIBackend : public RenderingBackend, public TextMeasurer {
public:
	GDIBackend();                          ///< 决策 35：默认构造（hwnd 暂空）
	~GDIBackend() override;                ///< 决策 18/31 + D1：缓冲与字体缓存统一释放

	void SetHwnd(HWND hwnd);               ///< 决策 35：Window 构造体内、CreateWindowExW 成功后调用

	void BeginFrame() override;            ///< 决策 16 清屏白 + 17 BeginPaint + 15/26 EnsureBackBuffer
	void DrawRect(const Rect& rect, const Color& color) override;   ///< 决策 21-25
	void DrawText(const Point& pos, const std::string& text,
	              const Color& color, const Font& font) override;   ///< D5/D6：TextOutW
	void EndFrame() override;              ///< 决策 17 EndPaint + 27/29 GetClientRect + BitBlt

	// TextMeasurer（D2：帧无关测量——GetDC(NULL) 临时屏幕 DC，仅测量不绘制）
	Size MeasureText(const Font& font, const std::string& text) override;
	float LineHeight(const Font& font) override;

private:
	void EnsureBackBuffer();               ///< 决策 15/19/26/38：懒创建 + 尺寸自检 + 先建后替
	void ReleaseBackBuffer();              ///< 决策 20/31：SelectObject(old)→DeleteObject→DeleteDC
	HFONT GetOrCreateFont(const Font& font);   ///< D1+D3：缓存取/建 HFONT（键 = size+family）
	static COLORREF ToColorRef(const Color& color);   ///< 决策 21/23：ToByte Clamp（DrawRect/DrawText 共用）

	HWND m_hwnd = nullptr;
	PAINTSTRUCT m_ps{};                    ///< 决策 17：帧状态（BeginPaint/EndPaint 配对）
	HDC m_windowDC = nullptr;              ///< BeginPaint 返回值，EndFrame BitBlt 目标
	HDC m_memoryDC = nullptr;
	HBITMAP m_bitmap = nullptr;
	HBITMAP m_oldBitmap = nullptr;         ///< 决策 20：SelectObject 返回值
	int m_bitmapWidth = 0;                 ///< 尺寸自检（决策 26）
	int m_bitmapHeight = 0;
	bool m_inFrame = false;                ///< 决策 32：Begin/End 配对断言

	std::map<std::pair<float, std::string>, HFONT> m_fontCache;   ///< D1：Font→HFONT 缓存（析构清理；键必须完整覆盖 Font 语义字段）
};

}
