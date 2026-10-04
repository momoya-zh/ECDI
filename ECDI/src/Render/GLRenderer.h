#pragma once

// ⚠️ Win32 宏防护必须先于任何 ECDI 头（条 10）：`GL/gl.h` 需要 `Windows.h`；`DrawText` 宏若
// 已生效会污染基类声明 → override 不匹配（"only virtual member functions can be marked 'override'"）
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif
#include <GL/gl.h>

#include "ECDI/Core/Point.h"
#include "ECDI/Core/Image.h"
#include "ECDI/Render/RenderingBackend.h"
#include "Render/CornerCoverageMask.h"
#include "Render/GLGlyphAtlas.h"
#include "Render/LineCoverage.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace ECDI{

class FontEngine;

/// @brief OpenGL（WGL）渲染后端（Phase 26 · `RenderingBackend` 的**第二个实现**）
/// @details
/// ★ **不改变默认后端**（详设 D1）：GDI 仍是默认；本类经 `CreateGLRenderServices()` 注入
///   （见 `examples/ModelProbe --gl`）。默认路径**逐位零回归**。
///
/// ★ **与 GDI 后端的对称性**：只实现 `RenderingBackend` 的**操作级**接口——**不接触**
///   `RenderCommand` / `Renderer` / `PaintContext` / `CommandBuffer`（详设 P5 定案）⇒ 那四层**零改动**。
///
/// ★ **WGL 生命周期（详设 §3-⑧ 定案）**：
///   - ownership：本类持 `HDC`（`GetDC`）+ `HGLRC`（`wglCreateContext`）；
///   - 创建：`Initialize(context)`（与 `GDIBackend` 同一接缝）；
///   - 线程：全部在 **UI 线程**（Window 构造 → Initialize；PaintFrame → 绘制）；
///   - resize：**per-frame 自省**（`BeginFrame` 内 `GetClientRect` → `glViewport`）；
///   - destroy：析构内 `wglMakeCurrent(nullptr)` → `wglDeleteContext` → `ReleaseDC`；
///   - 失败：任一 WGL 调用失败 ⇒ `m_ready = false` + 日志，**全部 `DrawXxx` 静默 no-op**（不崩）。
///     ★ **不静默回退 GDI**（详设 D7）——否则性能对照可能实际跑的是 GDI。
///
/// ★ **文本栈**：与 `FreeTypeTextMeasurer` **共享同一个 `FontEngine`**（**同源同基准**——S6）。
class GLRenderer : public RenderingBackend{
public:
	/// @param fontEngine 与测量器共享的字体引擎（`nullptr` 合法——文本命令将被跳过）
	explicit GLRenderer(std::shared_ptr<FontEngine> fontEngine);
	~GLRenderer() override;

	GLRenderer(const GLRenderer&) = delete;
	GLRenderer& operator=(const GLRenderer&) = delete;

	void Initialize(const PlatformRenderContext& context) override;

	void BeginFrame(const Color& background) override;
	void DrawRect(const Rect& rect, const Color& color) override;
	void DrawText(const Point& pos, const std::string& text,
	              const Color& color, const Font& font) override;
	void DrawLine(const Point& start, const Point& end,
	              float width, const Color& color) override;
	void DrawRoundedRect(const Rect& rect, float cornerRadius,
	                     const Color& color) override;
	void DrawImage(const Rect& dest, const Image& image) override;
	void PushClip(const Rect& rect) override;
	void PopClip() override;
	void DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color) override;
	void EndFrame() override;

	/// @brief 是否已就绪（WGL 初始化成功）
	/// @details ★ **覆盖基类的默认 `true`**（`RenderingBackend::IsReady`）——真实消费者：
	///          `ModelProbe --gl` 据此**明确报错退出**（**不静默回退 GDI**，详设 D7）。
	bool IsReady() const noexcept override { return m_ready; }

	/// @brief 图集 miss 次数（★ 观测缝 —— 详设 §1.5 基准指标之一）
	std::size_t AtlasMissCount() const noexcept { return m_atlas ? m_atlas->MissCount() : 0; }

private:
	// ── 纹理 quad 合成（图集 UV）──
	void Shade(const Color& color) const;
	/// @brief 常规 quad：低 v → quad 顶（行序为**顶行在前**的位图用——含字形：FreeType 位图 top-down）
	void TexturedQuad(int ax, int ay, int aw, int ah, float x, float y, float w, float h) const;
	/// @brief 带 UV 翻转的 quad（圆角掩码四角复用同一份 canonical 掩码）
	void TexturedQuadUV(int ax, int ay, int aw, int ah, float x, float y, float w, float h,
	                    bool flipU, bool flipV) const;
	/// @brief 任意朝向的**实心**线段四边形（焦点框沿圆角周界的划段用——走 1×1 全亮单元）
	/// @param halfWidth 垂直半宽（0.5 ⇒ 1px 宽的划）
	void SolidSegment(float x0, float y0, float x1, float y1, float halfWidth) const;

	/// @brief 圆角：取（或生成并上传）半径 R 的角覆盖度槽位
	GlyphSlot CornerSlot(int radius);
	/// @brief 线段：取（或生成并上传）覆盖度网格槽位（★ 几何量化到 1/8 px 作键）
	GlyphSlot LineSlot(const LineCoverageGrid& grid,
	                   float x0, float y0, float x1, float y1, float width);

	void ApplyScissor();

	std::shared_ptr<FontEngine> m_fontEngine;   ///< ★ 与 `FreeTypeTextMeasurer` 共享
	/// ★ `unique_ptr`：图集**在 context 就绪后创建、在 context 拆除前销毁**——
	///   把「GPU texture 与 GL context 同生命周期」编码进成员顺序（详设 §3-⑧ 不变量的镜像）
	std::unique_ptr<GLGlyphAtlas> m_atlas;

	HWND  m_hwnd = nullptr;
	HDC   m_dc = nullptr;
	HGLRC m_gl = nullptr;
	/// ★★ 更新区域验证的配对态（`BeginPaint` 返回的 DC 弃用——渲染走 Initialize 缓存的 `m_dc`）
	PAINTSTRUCT m_ps{};                        ///< `BeginPaint`/`EndPaint` 严格配对的结构体
	bool  m_inFrame = false;                   ///< 决策 32 同构：Begin/End 严格配对
	bool  m_paintBegun = false;                ///< 本帧是否已 BeginPaint（⇒ 必须 EndPaint）
	bool  m_ready = false;
	bool  m_doubleBuffered = false;
	int   m_w = 0, m_h = 0;
	std::size_t m_frames = 0;                   ///< 帧计数（诊断——供 D26-5 基准的「计数打印」）

	GlyphSlot m_solid;                          ///< 1×1 全亮单元（实心 quad 走图集 ⇒ 整帧单纹理绑定）
	std::vector<RECT> m_clipStack;
	CornerMaskCache m_cornerMasks;              ///< 覆盖度掩码缓存（键 = effective 整数半径）
	std::map<int, GlyphSlot> m_cornerSlots;     ///< 半径 → 图集槽位
	std::map<std::tuple<int, int, int, int, int, int, int>, GlyphSlot> m_lineSlots;  ///< 线段几何 → 槽位
};

}
