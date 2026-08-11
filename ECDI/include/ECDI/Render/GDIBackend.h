#pragma once

#include"ECDI/Render/RenderingBackend.h"

#include<Windows.h>

namespace ECDI {

/// @brief GDI 渲染后端（默认实现，双缓冲完全封装，决策 11/14）
/// @details
/// - 所有权：Window 值成员，构造体内 SetHwnd（决策 35）
/// - 双缓冲：BeginFrame 懒创建 + 尺寸自检（决策 15/26），先建后替重建（决策 38）
/// - HDC/HBITMAP/BitBlt 全是实现细节，Window/Renderer 零感知（决策 14）
class GDIBackend : public RenderingBackend {
public:
	GDIBackend();                          ///< 决策 35：默认构造（hwnd 暂空）
	~GDIBackend() override;                ///< 决策 18/31：ReleaseBackBuffer 统一释放

	void SetHwnd(HWND hwnd);               ///< 决策 35：Window 构造体内、CreateWindowExW 成功后调用

	void BeginFrame() override;            ///< 决策 16 清屏白 + 17 BeginPaint + 15/26 EnsureBackBuffer
	void DrawRect(const Rect& rect, const Color& color) override;   ///< 决策 21-25
	void EndFrame() override;              ///< 决策 17 EndPaint + 27/29 GetClientRect + BitBlt

private:
	void EnsureBackBuffer();               ///< 决策 15/19/26/38：懒创建 + 尺寸自检 + 先建后替
	void ReleaseBackBuffer();              ///< 决策 20/31：SelectObject(old)→DeleteObject→DeleteDC

	HWND m_hwnd = nullptr;
	PAINTSTRUCT m_ps{};                    ///< 决策 17：帧状态（BeginPaint/EndPaint 配对）
	HDC m_windowDC = nullptr;              ///< BeginPaint 返回值，EndFrame BitBlt 目标
	HDC m_memoryDC = nullptr;
	HBITMAP m_bitmap = nullptr;
	HBITMAP m_oldBitmap = nullptr;         ///< 决策 20：SelectObject 返回值
	int m_bitmapWidth = 0;                 ///< 尺寸自检（决策 26）
	int m_bitmapHeight = 0;
	bool m_inFrame = false;                ///< 决策 32：Begin/End 配对断言
};

}
