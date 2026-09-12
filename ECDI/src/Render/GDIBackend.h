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
#include"ECDI/Core/Image.h"
#include"ECDI/Render/RenderingBackend.h"
#include"Render/CornerCoverageMask.h"

#include<map>
#include<string>
#include<vector>

namespace ECDI {

/// @brief GDI 渲染后端（默认实现，双缓冲完全封装，决策 11/14；7.1.4 拆类——渲染专用）
/// @details
/// - 所有权：Window 经 RenderServices 注入（unique_ptr——7.1.4 决策 35 代价解决）
/// - 双缓冲：BeginFrame 懒创建 + 尺寸自检（决策 15/26），先建后替重建（决策 38）
/// - HDC/HBITMAP/BitBlt 全是实现细节，Window/Renderer 零感知（决策 14）
/// - 句柄注入：Initialize(PlatformRenderContext&) → static_cast<Win32RenderContext> 取 HWND
///   （7.1.4——取代 SetHwnd 过渡；体系内约定，非跨层 dynamic_cast）
/// - 文本绘制：DrawText 用 TextOutW + HFONT 缓存（D1/D3）；测量已拆出 GDITextMeasurer（7.1.4）
class GDIBackend : public RenderingBackend {
public:
	GDIBackend();                          ///< 默认构造（hwnd 空——Initialize 注入）
	~GDIBackend() override;                ///< 决策 18/31 + D1：缓冲与字体缓存统一释放

	void Initialize(const PlatformRenderContext& context) override;   ///< 7.1.4：平台句柄注入（取代 SetHwnd）

	void BeginFrame() override;            ///< 决策 16 清屏白 + 17 BeginPaint + 15/26 EnsureBackBuffer
	void DrawRect(const Rect& rect, const Color& color) override;   ///< 决策 21-25
	void DrawText(const Point& pos, const std::string& text,
	              const Color& color, const Font& font) override;   ///< D5/D6：TextOutW
	void DrawLine(const Point& start, const Point& end,
	              float width, const Color& color) override;        ///< Phase 8 §8.1：lround + 1px 下限
	void DrawRoundedRect(const Rect& rect, float cornerRadius,
	                     const Color& color) override;              ///< Phase 8 §8.2：NULL_PEN + RoundRect
	void DrawImage(const Rect& dest, const Image& image) override;  ///< Phase 8 §8.3：DIB + AlphaBlend
	void PushClip(const Rect& rect) override;                       ///< Phase 8 §8.5：SaveDC + IntersectClipRect
	void PopClip() override;                                        ///< Phase 8 §8.5：栈空防御 + RestoreDC
	void DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color) override;  ///< Phase 8 §8.4 + 9.5 R4 圆角：PS_DOT + NULL_BRUSH（RoundRect 圆角）
	void EndFrame() override;              ///< 决策 17 EndPaint + 27/29 GetClientRect + BitBlt

	/// @brief 设置抗锯齿开关（Phase 8.6；后端级，默认开启）
	/// @details Internal 头内的公有方法——**不属于公共 API**（`include/ECDI` 零改动）。
	///          用途：L2 测试需要「开/关对照」；生产代码无需调用。
	void SetAntiAliasing(bool enabled) noexcept { m_antiAliasing = enabled; }
	bool IsAntiAliasingEnabled() const noexcept { return m_antiAliasing; }

private:
	void EnsureBackBuffer();               ///< 决策 15/19/26/38：懒创建 + 尺寸自检 + 先建后替
	void ReleaseBackBuffer();              ///< 决策 20/31：SelectObject(old)→DeleteObject→DeleteDC
	HFONT GetOrCreateFont(const Font& font);   ///< D1+D3：缓存取/建 HFONT（键 = size+family；DrawText 渲染用）
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

	std::vector<int> m_clipStack;          ///< Phase 8 §8.5：SaveDC 返回值栈（Push 时入、Pop 时出）

	std::map<std::pair<float, std::string>, HFONT> m_fontCache;   ///< D1：Font→HFONT 缓存（渲染 DrawText 用；测量缓存归 GDITextMeasurer）

	// ── Phase 8.6：圆角覆盖度抗锯齿 ─────────────────────────────────

	/// @brief 角补丁绘制面（Phase 8.6）：32bpp 顶降**预乘** DIB，单个复用、**只增不减**
	/// @details ⚠️ 仅用于同步、顺序的 GDIBackend 绘制流程；**不保证并发/重入安全**。
	/// 有效区域仅 `[0, R) × [0, R)`（R = 本次调用半径）——`size - R` 范围内的历史像素
	/// **无语义要求**，调用方不得读取或混合该区域。
	struct PatchSurface {

		HDC dc = nullptr;
		HBITMAP bitmap = nullptr;
		HBITMAP oldBitmap = nullptr;   ///< SelectObject 返回值（释放时先恢复再删——GDI 铁律）
		void* bits = nullptr;
		int size = 0;                  ///< 当前边长（正方形；只增不减）

		/// @brief 确保边长 >= requiredSize（不足则重建；成功返回 true）
		bool Ensure(HDC reference, int requiredSize);

		/// @brief 释放（严格逆序：恢复 oldBitmap → DeleteObject → DeleteDC）
		void Release();

	};

	/// @brief 既有两条分支的搬移（行为逐位不变）——R == 0 或 AA 关闭时走此路
	void DrawRoundedRectLegacy(const Rect& rect, float cornerRadius, const Color& color);

	/// @brief a == 1 的 AA 路径：3 条带 FillRect + 4 角补丁（详细设计 §5.3）
	void DrawRoundedRectOpaqueAA(const Rect& rect, int radius, const Color& color,
	                             const CornerCoverageMask& mask);

	/// @brief a < 1 的 AA 路径：全矩形 DIB + 覆盖度（详细设计 §5.4）
	void DrawRoundedRectBlendedAA(const Rect& rect, int radius, const Color& color,
	                              const CornerCoverageMask& mask);

	/// @brief 把掩码 + 颜色按角方向写入 PatchSurface（预乘 BGRA）
	void FillPatchFromMask(const CornerCoverageMask& mask, const Color& color, CornerId corner);

	/// @brief 把 PatchSurface 的 R×R 区域 AlphaBlend 到目标 (x, y)
	void BlendPatch(HDC target, int x, int y, int size);

	CornerMaskCache m_cornerMaskCache;   ///< Phase 8.6：覆盖度掩码缓存（键 = effective 整数半径）
	PatchSurface m_patchSurface;         ///< Phase 8.6：角补丁绘制面（复用，只增不减）
	bool m_antiAliasing = true;          ///< Phase 8.6：AA 开关（后端级；默认开）
};

}
