#include "Render/GLRenderer.h"

#include "ECDI/Core/Logger.h"
#include "Platform/Win32/Win32RenderContext.h"   // ★ 取 HWND（同 GDIBackend / GDITextMeasurer 先例）
#include "Render/FontEngine.h"                   // 诊断用（RasterizeCount）
#include "Render/Utf8Decode.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace ECDI{

// ★★ GDI 决策 25 的 GL 对应实现：Rect→像素域**逐边截断**（floor）。GDI 的 `DrawRect` 等把
//    `RECT` 四边各自 `static_cast<LONG>`（截断）后填充 `[left, right)` 的整数像素列；
//    GL 的 quad 用原始浮点边渲染时，分数边的边界像素会被**相邻后画的控件**部分覆盖
//    （实测：贴边控件的焦点框被侧边控件盖住、按钮边框残留深色分数细线）。
//    顶点坐标一律 floor 后，quad 恰好覆盖 `[floor(x), floor(x+w))` 的整数像素列——
//    与 GDI 逐位一致 ⇒ 共享边的像素归属唯一确定。
static float Snap(float v)
{
	return std::floor(v);
}

GLRenderer::GLRenderer(std::shared_ptr<FontEngine> fontEngine)
	: m_fontEngine(std::move(fontEngine))
{
}

GLRenderer::~GLRenderer()
{
	// ★ 诊断（Phase 26 D26-5 基准的「计数打印」）：退出时一行汇总——供**人工性能对照**读数。
	//   验收口径是 warm 阶段「glyph 不再栅格化」⇒ 关键读数是 `glyphRasterizations` 与 `atlasMiss`
	//   是否**收敛**（不随滚动帧数增长）。★ `Info` 级：无调试器接收时几乎零成本。
	Logger::Log(LogLevel::Info,
	            L"GLRenderer: frames=" + std::to_wstring(m_frames)
	            + L" atlasMiss=" + std::to_wstring(m_atlas ? m_atlas->MissCount() : 0)
	            + L" glyphRasterizations=" + std::to_wstring(m_fontEngine ? m_fontEngine->RasterizeCount() : 0));

	// ★ 释放顺序（详设 §3-⑧ 不变量的**镜像**）：图集（texture 必须在 current context 下删）
	//   → context → DC。`m_atlas.reset()` 必须在 `wglDeleteContext` 之前。
	if (m_atlas)
	{
		m_atlas.reset();                 // ⇒ ~GLGlyphAtlas ⇒ glDeleteTextures
	}
	if (m_gl != nullptr)
	{
		wglMakeCurrent(m_dc, nullptr);
		wglDeleteContext(m_gl);
		m_gl = nullptr;
	}
	if (m_dc != nullptr)
	{
		ReleaseDC(m_hwnd, m_dc);
		m_dc = nullptr;
	}
}

void GLRenderer::Initialize(const PlatformRenderContext& context)
{
	// ★ 与 `GDIBackend` 同一接缝：从类型安全上下文取 HWND（体系内约定，非跨层 dynamic_cast）
	const HWND hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
	if (hwnd == nullptr)
	{
		Logger::Log(LogLevel::Error, L"GLRenderer: null window handle; GL backend disabled");
		return;                          // m_ready 保持 false ⇒ 全部 DrawXxx 静默 no-op（不崩）
	}
	m_hwnd = hwnd;

	m_dc = GetDC(m_hwnd);
	if (m_dc == nullptr)
	{
		Logger::Log(LogLevel::Error, L"GLRenderer: GetDC failed; GL backend disabled");
		return;
	}

	PIXELFORMATDESCRIPTOR want{};
	want.nSize      = sizeof(want);
	want.nVersion   = 1;
	want.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	want.iPixelType = PFD_TYPE_RGBA;
	want.cColorBits = 32;
	want.cAlphaBits = 8;
	want.iLayerType = PFD_MAIN_PLANE;

	const int pixelFormat = ChoosePixelFormat(m_dc, &want);
	if (pixelFormat == 0 || !SetPixelFormat(m_dc, pixelFormat, &want))
	{
		Logger::Log(LogLevel::Error, L"GLRenderer: pixel format selection failed; GL backend disabled");
		return;
	}

	// 回读真实能力（是否真双缓冲——`EndFrame` 据此选择 SwapBuffers / glFlush）
	PIXELFORMATDESCRIPTOR real{};
	real.nSize = sizeof(real);
	if (DescribePixelFormat(m_dc, pixelFormat, sizeof(real), &real) != 0)
	{
		m_doubleBuffered = (real.dwFlags & PFD_DOUBLEBUFFER) != 0;
	}

	m_gl = wglCreateContext(m_dc);
	if (m_gl == nullptr || !wglMakeCurrent(m_dc, m_gl))
	{
		Logger::Log(LogLevel::Error, L"GLRenderer: WGL context creation failed; GL backend disabled");
		return;
	}

	// 显式状态初始化（不靠默认）：y 翻转投影下四边形环绕反序 ⇒ 关剔除；2D ⇒ 关深度
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	// ★★ 创建顺序不变量（详设 §3-⑧）：① context current → ② 图集初始化 → ③ ready
	m_atlas = std::make_unique<GLGlyphAtlas>();
	m_atlas->Initialize();
	if (!m_atlas->IsReady())
	{
		Logger::Log(LogLevel::Error, L"GLRenderer: glyph atlas init failed; GL backend disabled");
		return;
	}

	// 1×1 全亮单元：所有实心 quad 都走图集 ⇒ **整帧只绑一张纹理**（攒批友好，M-5）
	m_solid = m_atlas->Allocate(1, 1);
	if (m_solid.valid)
	{
		const std::vector<std::uint8_t> white(1, 255);
		m_atlas->Upload(m_solid, white);
	}

	m_ready = true;
}

void GLRenderer::BeginFrame(const Color& background)
{
	if (!m_ready)
	{
		return;
	}

	// ★ resize = **per-frame 自省**（详设 §3-⑧ / N7：不实现 OnTargetResized 接缝）
	++m_frames;
	RECT rc{};
	GetClientRect(m_hwnd, &rc);
	m_w = rc.right - rc.left;
	m_h = rc.bottom - rc.top;

	glViewport(0, 0, m_w, m_h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, static_cast<double>(m_w), static_cast<double>(m_h), 0.0, -1.0, 1.0);   // y 向下（与框架同向）
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glClearColor(background.r, background.g, background.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, m_atlas->Texture());

	m_clipStack.clear();
	glDisable(GL_SCISSOR_TEST);
}

void GLRenderer::DrawRect(const Rect& rect, const Color& color)
{
	if (!m_ready)
	{
		return;
	}
	Shade(color);
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, rect.x, rect.y, rect.width, rect.height);
}

void GLRenderer::DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font)
{
	if (!m_ready || !m_fontEngine)
	{
		return;
	}

	// ★ 契约：pos = 物理像素（Renderer 已折）；font.size = DIP ⇒ 经 FontEngine 换算（D26-2 唯一路径）
	const int pixelSize = m_fontEngine->PixelSize(font);
	const FaceId faceId = m_fontEngine->FaceIdFor(font.family);
	if (faceId == 0)
	{
		return;                          // 无可用 face（FontEngine 已告警）
	}

	// ★★ `pos.y` = **字符单元顶边**（不是基线！）——`GDIBackend::DrawText` 用 `TextOutW`，
	//    其默认对齐 `TA_LEFT | TA_TOP` 把 y 解释为单元顶边。⇒ 基线 = 行顶 + ascent。
	//    ★ 缺这一项会让**整行文字上移一个 ascent**（20px 字号约 21px）——实测与 GDI 差 21px。
	// ★★ 文字**整数吸附**（v1.1 修「文字污染」）：GDI 的 `TextOutW` 只收**整数**坐标
	//    （`static_cast<LONG>` 截断），字形永远落在整数像素网格上；GL 若用分数 pen/baseline，
	//    **hinted 位图会在分数相位下采样**——笔画错相、粗细不一（实测「些许污染」）。
	//    ⇒ pen / baseline 一律 floor 吸附；此后 advance / bearingX / bearingY 全为整数
	//      （FreeType `advance.x >> 6` / `bitmap_left` / `bitmap_top`）⇒ 整行逐字对齐像素网格。
	const float baseline = std::floor(pos.y) + std::floor(m_fontEngine->Ascent(font));

	float pen = std::floor(pos.x);
	Shade(color);

	for (const char32_t cp : DecodeUtf8(text))
	{
		const std::uint32_t glyphIndex = m_fontEngine->GlyphIndex(font, cp);
		if (glyphIndex == 0)
		{
			continue;                    // 该 face 无此字形
		}

		const GlyphKey key{ faceId, glyphIndex, pixelSize,
		                    static_cast<std::uint8_t>(HintingMode::Normal) };
		const GlyphSlot slot = m_atlas->GetOrCreate(key, *m_fontEngine);

		// ★ FreeType 位图行序 = **top-down** ⇒ 常规 UV（低 v → quad 顶），**不翻转**
		//   （spike 的 `TexturedQuadYUp` 是给其自带 bottom-up 光栅器用的——此处不适用）
		if (slot.valid && slot.w > 0 && slot.h > 0)
		{
			TexturedQuad(slot.x, slot.y, slot.w, slot.h,
			             pen + slot.bearingX,                 // 左端 = 笔位 + bitmap_left
			             baseline - slot.bearingY,            // 顶端 = 基线 − bitmap_top（bearingY 向上量）
			             static_cast<float>(slot.w), static_cast<float>(slot.h));
		}

		// ★★ 无论 valid 与否都推进笔位（详设 §1.4：图集失败只影响可见性、不影响排版位置）
		pen += slot.advance;
	}
}

void GLRenderer::DrawLine(const Point& start, const Point& end, float width, const Color& color)
{
	if (!m_ready)
	{
		return;
	}

	// ★ 覆盖度复用（S2）：与 GDI 侧**同一个平台无关生成器**（`Render/LineCoverage.h`）
	const LineCoverageGrid grid = GenerateLineCoverage(start.x, start.y, end.x, end.y, width);
	if (grid.Empty())
	{
		return;
	}

	const GlyphSlot slot = LineSlot(grid, start.x, start.y, end.x, end.y, width);
	if (!slot.valid)
	{
		return;                          // 图集满 ⇒ 跳过（不崩）
	}

	Shade(color);
	TexturedQuad(slot.x, slot.y, grid.width, grid.height,
	             static_cast<float>(grid.originX), static_cast<float>(grid.originY),
	             static_cast<float>(grid.width), static_cast<float>(grid.height));
}

void GLRenderer::DrawRoundedRect(const Rect& rect, float cornerRadius, const Color& color)
{
	if (!m_ready)
	{
		return;
	}

	const float maxRadius = (std::min)(rect.width, rect.height) * 0.5f;
	const int radius = static_cast<int>(std::lround((std::min)(cornerRadius, maxRadius)));
	if (radius <= 0)
	{
		DrawRect(rect, color);
		return;
	}

	const float x = rect.x, y = rect.y, w = rect.width, h = rect.height;
	const float fr = static_cast<float>(radius);
	Shade(color);

	// ★ 三条**互不重叠**的实心带（★ 与 GDI 的 `DrawRoundedRectOpaqueAA` 同款几何）——
	//   原实现的「竖带（全高）+ 横带（全宽）」在**中心区域重叠**，半透明色被二次混合
	//   ⇒ 实测中心明显变深、外圈偏浅（可见的「回」字形）。★ 因 clamp 保证 `2R <= min(w,h)`，
	//   三条带的宽高恒 >= 0；零尺寸者自然退化（真圆时三条带全零 ⇒ 仅四角拼成整圆）。
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x, y + fr, w, h - 2.0f * fr);            // 中带（全宽）
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + fr, y, w - 2.0f * fr, fr);           // 上带
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + fr, y + h - fr, w - 2.0f * fr, fr);  // 下带

	const GlyphSlot corner = CornerSlot(radius);
	if (!corner.valid)
	{
		return;                          // 图集满：中心/条带已画，四角留白（不崩）
	}

	// 四角复用**同一份 canonical 掩码**，靠 UV 翻转取向（分 4 次调用 —— 与 GDI 的角索引变换等价）
	TexturedQuadUV(corner.x, corner.y, radius, radius, x, y, fr, fr, false, false);                       // TL
	TexturedQuadUV(corner.x, corner.y, radius, radius, x + w - fr, y, fr, fr, true, false);               // TR
	TexturedQuadUV(corner.x, corner.y, radius, radius, x, y + h - fr, fr, fr, false, true);               // BL
	TexturedQuadUV(corner.x, corner.y, radius, radius, x + w - fr, y + h - fr, fr, fr, true, true);       // BR
}

void GLRenderer::DrawImage(const Rect& dest, const Image& image)
{
	if (!m_ready || image.width <= 0 || image.height <= 0)
	{
		return;
	}

	// premultiplied BGRA → RGBA（legacy GL 无 `GL_BGRA` 依赖）
	// ★ dest 四边同样 Snap（GDI AlphaBlend 的 RECT 截断语义——决策 25 对齐）
	std::vector<std::uint8_t> rgba;
	rgba.reserve(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
	for (int j = 0; j < image.height; ++j)
	{
		const std::uint8_t* const row = image.pixels.data()
		                              + static_cast<std::ptrdiff_t>(j) * image.stride;
		for (int i = 0; i < image.width; ++i)
		{
			rgba.push_back(row[static_cast<std::size_t>(i) * 4 + 2]);   // R ← B
			rgba.push_back(row[static_cast<std::size_t>(i) * 4 + 1]);   // G
			rgba.push_back(row[static_cast<std::size_t>(i) * 4 + 0]);   // B ← R
			rgba.push_back(row[static_cast<std::size_t>(i) * 4 + 3]);   // A
		}
	}

	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

	// ★ `Image` 契约 = **premultiplied**（`Image.h`）⇒ 用 premult 混合；数据 top-down ⇒ 顶边取 v = 0
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(Snap(dest.x), Snap(dest.y));
	glTexCoord2f(1.0f, 0.0f); glVertex2f(Snap(dest.x + dest.width), Snap(dest.y));
	glTexCoord2f(1.0f, 1.0f); glVertex2f(Snap(dest.x + dest.width), Snap(dest.y + dest.height));
	glTexCoord2f(0.0f, 1.0f); glVertex2f(Snap(dest.x), Snap(dest.y + dest.height));
	glEnd();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDeleteTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, m_atlas->Texture());   // 恢复图集绑定（后续绘制仍走它）
}

void GLRenderer::PushClip(const Rect& rect)
{
	if (!m_ready)
	{
		return;
	}

	RECT rc{ static_cast<LONG>(rect.x), static_cast<LONG>(rect.y),
	         static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height) };
	if (!m_clipStack.empty())
	{
		const RECT& top = m_clipStack.back();
		rc.left   = (std::max)(rc.left, top.left);
		rc.top    = (std::max)(rc.top, top.top);
		rc.right  = (std::min)(rc.right, top.right);
		rc.bottom = (std::min)(rc.bottom, top.bottom);
	}
	m_clipStack.push_back(rc);
	ApplyScissor();
}

void GLRenderer::PopClip()
{
	if (!m_ready)
	{
		return;
	}
	if (!m_clipStack.empty())
	{
		m_clipStack.pop_back();
	}
	ApplyScissor();
}

void GLRenderer::DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color)
{
	if (!m_ready)
	{
		return;
	}

	// ★★ 与 GDI **同几何**（`GDIBackend::DrawFocusRect` §8.4）：沿「4 直线 + 4 圆弧」周界
	//    连续 **3px 实 + 3px 空**，圆角跟随 `cornerRadius`。
	//    ★ 修正（原实现）：忽略 `cornerRadius` 画方框 ⇒ 圆角控件的焦点框在**四角外凸**，
	//      与 GDI 的圆角焦点框语义不符（契约 C-5：同 `RenderCommand` ⇒ 同语义操作）。
	const float fLeft   = static_cast<float>(std::lround(rect.x));
	const float fTop    = static_cast<float>(std::lround(rect.y));
	const float fRight  = static_cast<float>(std::lround(rect.x + rect.width) - 1);
	const float fBottom = static_cast<float>(std::lround(rect.y + rect.height) - 1);

	// 弧心基于**几何边界** [x, x+w) × [y, y+h)——与 `DrawRoundedRect` 的形状语义一致（GDI 同款修正）
	const float gRight  = static_cast<float>(std::lround(rect.x + rect.width));
	const float gBottom = static_cast<float>(std::lround(rect.y + rect.height));
	const float r = (std::min)(cornerRadius, (std::min)(gRight - fLeft, gBottom - fTop) * 0.5f);

	struct Seg
	{
		bool  arc = false;
		float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;   // 直线端点
		float cx = 0.0f, cy = 0.0f, a1 = 0.0f, a2 = 0.0f;   // 圆弧：圆心 + 起止角
		float len = 0.0f;                                   // 周界弧长
	};
	std::vector<Seg> segs;
	constexpr float kPi = 3.14159265f;
	const auto AddLine = [&segs](float x1, float y1, float x2, float y2)
	{
		const float dx = x2 - x1, dy = y2 - y1;
		segs.push_back(Seg{ false, x1, y1, x2, y2, 0.0f, 0.0f, 0.0f, 0.0f, std::sqrt(dx * dx + dy * dy) });
	};
	const auto AddArc = [&segs, r](float cx, float cy, float a1, float a2)
	{
		segs.push_back(Seg{ true, 0.0f, 0.0f, 0.0f, 0.0f, cx, cy, a1, a2, r * (a2 - a1) });
	};

	// 上 → 右上 → 右 → 右下 → 下 → 左下 → 左 → 左上（沿周界连续顺序）
	if (r <= 0.0f)
	{
		AddLine(fLeft, fTop, fRight, fTop);
		AddLine(fRight, fTop, fRight, fBottom);
		AddLine(fRight, fBottom, fLeft, fBottom);
		AddLine(fLeft, fBottom, fLeft, fTop);
	}
	else
	{
		AddLine(fLeft + r, fTop, gRight - r, fTop);
		AddArc(gRight - r, fTop + r, -kPi * 0.5f, 0.0f);
		AddLine(fRight, fTop + r, fRight, fBottom - r);
		AddArc(gRight - r, gBottom - r, 0.0f, kPi * 0.5f);
		AddLine(gRight - r, fBottom, fLeft + r, fBottom);
		AddArc(fLeft + r, gBottom - r, kPi * 0.5f, kPi);
		AddLine(fLeft, fBottom - r, fLeft, fTop + r);
		AddArc(fLeft + r, fTop + r, kPi, kPi * 1.5f);
	}

	float total = 0.0f;
	for (const Seg& s : segs)
	{
		total += s.len;
	}

	// 周界弧长 → 坐标（★ 同 GDI：clamp 到可见像素范围，避免无 clip 时越界 1px 绘制）
	const auto PointAt = [&](float s)
	{
		float px = fLeft, py = fTop, acc = 0.0f;
		for (const Seg& seg : segs)
		{
			if (s < acc + seg.len + 1e-4f)
			{
				const float u = s - acc;
				if (!seg.arc)
				{
					const float t = (seg.len > 0.0f) ? u / seg.len : 0.0f;
					px = seg.x1 + (seg.x2 - seg.x1) * t;
					py = seg.y1 + (seg.y2 - seg.y1) * t;
				}
				else
				{
					const float a = seg.a1 + u / r;
					px = seg.cx + r * std::cos(a);
					py = seg.cy + r * std::sin(a);
				}
				break;                   // 命中即止
			}
			acc += seg.len;
		}
		return std::pair<float, float>{ (std::min)((std::max)(px, fLeft), fRight),
		                                (std::min)((std::max)(py, fTop), fBottom) };
	};

	Shade(color);
	constexpr float kDash = 3.0f;
	constexpr float kGap  = 3.0f;
	constexpr int   kSub  = 8;           // 实心段细分（弧线平滑——同 GDI）
	float t = 0.0f;
	bool draw = true;
	while (t < total)
	{
		const float t2 = (std::min)(t + (draw ? kDash : kGap), total);
		if (draw)
		{
			auto [px, py] = PointAt(t);
			for (int i = 1; i <= kSub; ++i)
			{
				const auto [qx, qy] = PointAt(t + (t2 - t) * static_cast<float>(i)
				                                / static_cast<float>(kSub));
				// ★ +0.5：把「像素索引」转成**像素中心** ⇒ 1px 宽的划恰好吃满一格（不糊成两格）
				SolidSegment(px + 0.5f, py + 0.5f, qx + 0.5f, qy + 0.5f, 0.5f);
				px = qx;
				py = qy;
			}
		}
		t = t2;
		draw = !draw;
	}
}

void GLRenderer::SolidSegment(float x0, float y0, float x1, float y1, float halfWidth) const
{
	// 以 (x0,y0)-(x1,y1) 为轴、垂直半宽 halfWidth 的**实心**四边形（走 1×1 全亮单元）
	// ★ 顶点同样过 Snap（floor）：分数坐标的弧线段量化到像素网格——与 GDI 的整数像素划一致
	float dx = x1 - x0, dy = y1 - y0;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0f)
	{
		return;
	}
	dx /= len;
	dy /= len;
	const float nx = -dy * halfWidth;
	const float ny =  dx * halfWidth;

	// ★ 四个顶点取**同一个**实心 texel 的 UV ⇒ 恒定颜色（NEAREST 无插值——几何才是形状）
	const float size = static_cast<float>(GLGlyphAtlas::kSize);
	const float u = static_cast<float>(m_solid.x) / size;
	const float v = static_cast<float>(m_solid.y) / size;

	glBegin(GL_QUADS);
	glTexCoord2f(u, v); glVertex2f(Snap(x0 + nx), Snap(y0 + ny));
	glTexCoord2f(u, v); glVertex2f(Snap(x1 + nx), Snap(y1 + ny));
	glTexCoord2f(u, v); glVertex2f(Snap(x1 - nx), Snap(y1 - ny));
	glTexCoord2f(u, v); glVertex2f(Snap(x0 - nx), Snap(y0 - ny));
	glEnd();
}

void GLRenderer::EndFrame()
{
	if (!m_ready)
	{
		return;
	}
	if (m_doubleBuffered)
	{
		SwapBuffers(m_dc);
	}
	else
	{
		glFlush();
	}
}

// ══════════════════════════════════════════════════════════════════════════
// 内部：quad 合成 / 覆盖度槽位 / 裁剪
// ══════════════════════════════════════════════════════════════════════════

void GLRenderer::Shade(const Color& color) const
{
	glColor4f(color.r, color.g, color.b, color.a);
}

// ★★ GDI 决策 25 的 GL 对应实现：Rect→像素域**逐边截断**（floor）。GDI 的 `DrawRect` 等把
//    `RECT` 四边各自 `static_cast<LONG>`（截断）后填充 `[left, right)` 的整数像素列；
//    GL 的 quad 用原始浮点边渲染时，分数边的边界像素会被**相邻后画的控件**部分覆盖
//    （实测：贴边控件的焦点框被侧边控件盖住、按钮边框残留深色分数细线）。
//    顶点坐标一律 floor 后，quad 恰好覆盖 `[floor(x), floor(x+w))` 的整数像素列——
//    与 GDI 逐位一致 ⇒ 共享边的像素归属唯一确定。

void GLRenderer::TexturedQuad(int ax, int ay, int aw, int ah, float x, float y, float w, float h) const
{
	const float size = static_cast<float>(GLGlyphAtlas::kSize);
	const float u0 = static_cast<float>(ax) / size;
	const float v0 = static_cast<float>(ay) / size;
	const float u1 = static_cast<float>(ax + aw) / size;
	const float v1 = static_cast<float>(ay + ah) / size;

	const float x0 = Snap(x), y0 = Snap(y);
	const float x1 = Snap(x + w), y1 = Snap(y + h);
	glBegin(GL_QUADS);
	glTexCoord2f(u0, v0); glVertex2f(x0, y0);
	glTexCoord2f(u1, v0); glVertex2f(x1, y0);
	glTexCoord2f(u1, v1); glVertex2f(x1, y1);
	glTexCoord2f(u0, v1); glVertex2f(x0, y1);
	glEnd();
}

void GLRenderer::TexturedQuadUV(int ax, int ay, int aw, int ah, float x, float y, float w, float h,
                                bool flipU, bool flipV) const
{
	const float size = static_cast<float>(GLGlyphAtlas::kSize);
	float u0 = static_cast<float>(ax) / size;
	float v0 = static_cast<float>(ay) / size;
	float u1 = static_cast<float>(ax + aw) / size;
	float v1 = static_cast<float>(ay + ah) / size;
	if (flipU) { std::swap(u0, u1); }
	if (flipV) { std::swap(v0, v1); }

	const float x0 = Snap(x), y0 = Snap(y);
	const float x1 = Snap(x + w), y1 = Snap(y + h);
	glBegin(GL_QUADS);
	glTexCoord2f(u0, v0); glVertex2f(x0, y0);
	glTexCoord2f(u1, v0); glVertex2f(x1, y0);
	glTexCoord2f(u1, v1); glVertex2f(x1, y1);
	glTexCoord2f(u0, v1); glVertex2f(x0, y1);
	glEnd();
}

GlyphSlot GLRenderer::CornerSlot(int radius)
{
	const auto cached = m_cornerSlots.find(radius);
	if (cached != m_cornerSlots.end())
	{
		return cached->second;
	}

	// ★ 复用 GDI 侧同一份平台无关生成器（`Render/CornerCoverageMask.h`）——覆盖度复用（S2）
	const CornerCoverageMask& mask = m_cornerMasks.Get(radius);
	GlyphSlot slot;
	if (mask.Empty())
	{
		return slot;
	}

	slot = m_atlas->Allocate(mask.radius, mask.radius);
	if (slot.valid)
	{
		m_atlas->Upload(slot, mask.coverage);
		m_cornerSlots.emplace(radius, slot);
	}
	return slot;
}

GlyphSlot GLRenderer::LineSlot(const LineCoverageGrid& grid,
                               float x0, float y0, float x1, float y1, float width)
{
	// ★ 几何量化到 1/8 px 作键：**几何相同 ⇒ 覆盖度逐位相同**，必须复用槽位——否则每帧新分配
	//   会迅速填满图集（spike v3.0 的实测教训：~89 帧即满 ⇒ 堆越界崩）
	const auto key = std::make_tuple(
		grid.width, grid.height,
		static_cast<int>(std::lround(x0 * 8.0f)), static_cast<int>(std::lround(y0 * 8.0f)),
		static_cast<int>(std::lround(x1 * 8.0f)), static_cast<int>(std::lround(y1 * 8.0f)),
		static_cast<int>(std::lround(width * 8.0f)));

	const auto cached = m_lineSlots.find(key);
	if (cached != m_lineSlots.end())
	{
		return cached->second;
	}

	const GlyphSlot slot = m_atlas->Allocate(grid.width, grid.height);
	if (slot.valid)
	{
		m_atlas->Upload(slot, grid.coverage);
		m_lineSlots.emplace(key, slot);
	}
	return slot;
}

void GLRenderer::ApplyScissor()
{
	// ★ `glScissor` 用 framebuffer 坐标（**y 向上**）—— 必须 y 翻转
	if (m_clipStack.empty())
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}

	const RECT& rc = m_clipStack.back();
	glEnable(GL_SCISSOR_TEST);
	glScissor(rc.left, m_h - rc.bottom, rc.right - rc.left, rc.bottom - rc.top);
}

}
