#include "Render/GLRenderer.h"

#include "ECDI/Core/ECDIAssert.h"             // FRAMEWORK_ASSERT（决策 32 同构：Begin/End 严格配对）
#include "ECDI/Core/Logger.h"
#include "Platform/Win32/Win32RenderContext.h"   // ★ 取 HWND（同 GDIBackend / GDITextMeasurer 先例）
#include "Render/ArcCoverage.h"                   // ★ Phase 33：弧覆盖度生成器（与 LineCoverage 同族）
#include "Render/BackendContract.h"               // ★ 审计 #62：共享契约件（H3 守卫 / M17 吸附）
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
// ★ 审计 #62 / M17：改走**共享契约件**——`BackendContract::SnapVertex`（单一真相源）。
//   修复前本处自写 `std::floor`，与 GDI 的四套取整口径各自独立（审计 M17：「同一矩形四种口径」）。
//   两端从此共用 `QuantizeEdge` 的边规则（详见 `BackendContract.h` 的口径段与已知偏差记账）。
static float Snap(float v)
{
	return BackendContract::SnapVertex(v);
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
		// ★ #72-①：失败态仍须**验证更新区域**——空白帧的 BeginPaint/EndPaint 对。
		//   跳过验证 ⇒ WM_PAINT 队列一空即重生成（正是下方注释预言的风暴）⇒ WM_TIMER
		//   永不投递 ⇒ 一切定时器饿死（探针实测 4s 内 595571 帧重绘、0 tick——
		//   `.workbuddy/spike/p72/readout-A-defect.txt`）。绘制弃用——此对仅承担
		//   「验证」职责（与下方成功路径的 BeginPaint 同一理由）。
		if (m_hwnd != nullptr)
		{
			PAINTSTRUCT ps;
			BeginPaint(m_hwnd, &ps);
			EndPaint(m_hwnd, &ps);
		}
		return;
	}

	// 决策 32 同构：Begin/End 严格配对
	FRAMEWORK_ASSERT(!m_inFrame);
	m_inFrame = true;

	// ★★★ **更新区域验证**（与 `GDIBackend` 决策 17 同构）——**这不是可选的礼节，是正确性前提**：
	//   不验证更新区域 ⇒ `WM_PAINT` 在消息队列一空就被**重新生成** ⇒ 队列**永不空闲**
	//   ⇒ 而 `WM_TIMER` 只在队列空闲时才被投递 ⇒ **一切定时器饿死**
	//   （回放 tick / auto-close / 光标闪烁 / 动画 tick / ModelProbe 轮询全失效）。
	//   诊断：同一二进制下 GDI 后端定时器正常，GL 后端 7 秒内 `OnTimer` 一次未进。
	//   ★ 渲染仍走 `Initialize` 缓存的 `m_dc`（WGL context 绑定其上，改 DC 会使 context 失效）；
	//     `BeginPaint` 的返回值**弃用**——它在此**只承担「验证更新区域」这一个职责**。
	BeginPaint(m_hwnd, &m_ps);
	m_paintBegun = true;

	// ★ resize = **per-frame 自省**（详设 §3-⑧ / N7：不实现 OnTargetResized 接缝）
	++m_frames;
	RECT rc{};
	GetClientRect(m_hwnd, &rc);
	m_w = rc.right - rc.left;
	m_h = rc.bottom - rc.top;

	// ★★ 审计 #62 / H4：**每帧刷新共享 `FontEngine` 的 DPI**（修复前永不刷新）。
	//   缺陷：本链的 DPI 只在 `FreeTypeTextMeasurer::Initialize` 注入**一次**
	//     （`FreeTypeTextMeasurer.cpp:30` 是全库唯一的生产 `SetDpi` 调用点）⇒ 把窗口拖到
	//     不同 DPI 的显示器后（`WM_DPICHANGED` 只更新了翻译器/几何），**几何按新 DPI 缩放、
	//     字号与 advance 停在旧 DPI** ⇒ 每行 advance 与缩放后布局不匹配
	//     （裁剪 / 光标漂移 / 字形大小错），**直到重启**。
	//   ★ **对照 GDI**：`GDIBackend.cpp:875` 与 `GDITextMeasurer.cpp:89` **每次调用**都读
	//     `GetDpiForWindow` ⇒ GDI 无此缺陷。本处是 GL 侧的对等实现。
	//   ★ **落点选择**：`BeginFrame` 是每帧唯一必经点（与上面 `GetClientRect` 的 per-frame
	//     自省同款）——不新增接缝、不改 `WM_DPICHANGED` 链（那里只更新翻译器，是既有契约）。
	//   ★ **`FontEngine` 是共享对象**（`CreateGLRenderServices` 里 renderer 与 measurer 共用
	//     同一个）⇒ 在此刷新**同时**修正了渲染链与测量链，正是 Phase 26「同源」目标
	//     （S6）的落地；且 `FontEngine` 的各级缓存**键含 dpi**（`GlyphKey.pixelSize` /
	//     `measureCache` 的 dpi 维）⇒ 换 DPI 自然不命中旧缓存，**无需显式清理**。
	if (m_fontEngine)
	{
		const int dpi = GetDpiForWindow(m_hwnd);
		if (dpi > 0)
		{
			m_fontEngine->SetDpi(dpi);   // 幂等赋值；同值调用零代价（缓存键不变 ⇒ 全命中）
		}
	}

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
	const FaceId faceId = m_fontEngine->FaceIdFor(font.family, font.weight);   // Phase 36 批二：显式传参（详设 PD36-8）
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
			// ★★ 审计 #62 / H2：**缺字形也必须推进笔位**（与测量链同源）。
			//   修复前本处直接 `continue` ⇒ **不推进**；而测量链（`FontEngine::AdvanceOf`）
			//   对 gid==0 **仍真加载** `.notdef` 并返回其**非零** advance（见 `AdvanceOf`
			//   的 `glyphIndex != 0` 只门控 **memo**、不门控 `FT_Load_Char`）⇒
			//   「测出来的宽」比「画出来的宽」多一个 `.notdef` advance ⇒
			//   **该字符之后的所有字形整体左移、互相重叠；TextBox 光标与已绘制文字错位**。
			//   ★ GDI **无此问题**：`TextOutW` 会真画 `.notdef` 并推进，与
			//     `GetTextExtentPoint32W` 同源。
			//   ★ 触发人群：任何 face 不覆盖的码点——emoji、生僻 CJK、SimSun 里的 `✔`/方框字符。
			//   ★★ **必须只改绘制侧**：`FontEngine::AdvanceOf` 的 memo 策略
			//      （「gid = 0 永不入 memo」，`FontEngine.cpp:93-94` 有意为之）**一字不动**——
			//      动它会改掉测量基准（审计 M15 明确要求「修 H2 时保持本处不动」）。
			//   ★ 取法 = `AdvancePx`（走**同一个** `Impl::AdvanceOf`）⇒ 「测」与「画」同源。
			long advancePx = 0;
			if (m_fontEngine->AdvancePx(font, cp, advancePx))
			{
				pen += static_cast<float>(advancePx);   // ★ 位置推进（不绘制——face 确实无此字形）
			}
			continue;
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
		// ★★ 审计 #62 / L10：图集满 ⇒ 走 **legacy 实心降级**（不整线消失）。
		//   缺陷（修复前）：本处直接 `return` ⇒ 图集满时**整条线不画**——
		//   可达消费者：勾选标记 `✔`（`DrawLine` 画）、进度条刻度、分隔线。表现为
		//   「勾选标记凭空消失」。⚠️ 这是**可用性缺陷**（不是崩溃）——图集满本身就罕见，
		//   但「稀有资源耗尽 ⇒ UI 部件消失」是**静默失效**，无日志、无告警、无从诊断。
		//   ★ **GDI 同款哲学**（`GDIBackend.cpp:876` 原文）：「**宁可无 AA，不可缺绘制**」——
		//     GDI 在 `DrawLineAA` 的补丁面分配失败时**降级**到 `DrawLineLegacy`（无 AA 实心线），
		//     而不是跳过。本处是 GL 侧的对等实现。
		//   ★ 实现 = 复用 `SolidSegment`（**既有**件——`DrawFocusRect` 已在用，零新增 API）：
		//     以起点-终点为轴、半宽 `max(0.5, lround(width)/2)` 的实心线段四边形。
		//   ⚠️ 与 AA 路径的**非等价性如实记账**：AA 路径走图集覆盖度（亚像素边界平滑），
		//     降级路径是硬边 ⇒ **slope/粗细与正常路径略有差异**。这是有意的取舍：
		//     「画出一条粗一点的实线」优于「什么都不画」。
		const float halfWidth = (std::max)(0.5f, static_cast<float>(std::lround(width)) * 0.5f);
		Shade(color);
		SolidSegment(start.x, start.y, end.x, end.y, halfWidth);
		return;
	}

	Shade(color);
	TexturedQuad(slot.x, slot.y, grid.width, grid.height,
	             static_cast<float>(grid.originX), static_cast<float>(grid.originY),
	             static_cast<float>(grid.width), static_cast<float>(grid.height));
}

void GLRenderer::DrawArc(const Point& center, float radius,
                         float startAngleDeg, float sweepAngleDeg,
                         float width, const Color& color)
{
	// ★ C33-4 全表在 `NormalizeArc` 内一次判完（**唯一实现**——两端同符号调用）
	const BackendContract::ArcParams p =
		BackendContract::NormalizeArc(center.x, center.y, radius,
		                              startAngleDeg, sweepAngleDeg, width);
	if (!p.drawable || !m_ready)
	{
		return;   // 非就绪 = 全部绘制静默 no-op（既有形态）
	}

	// ★ 覆盖度复用：与 GDI 侧**同一个平台无关生成器**（`Render/ArcCoverage.h`）。
	//   GL 侧采样密度固定 8 = `GenerateLineCoverage` 的既有默认（`GLRenderer` 没有
	//   角缓存旋钮）⇒ 与 `DrawLine` 的既有一致性状态**相同**，非本 Phase 新引入的分歧（O33-1）。
	const LineCoverageGrid grid = GenerateArcCoverage(center.x, center.y, radius,
	                                                  startAngleDeg, sweepAngleDeg,
	                                                  width);
	if (grid.Empty())
	{
		return;
	}

	// ★ **GL 侧没有 AA 开关**（实测：本类无 `m_antiAliasing` 之类的成员，也没有
	//   `SetAntiAliasing`——`GLRenderer` 全部绘制都走图集覆盖度，AA 是它的**唯一**形态）。
	//   ⇒ PD33-C 在 GL 侧**无对应物**，`binarize` 在此的**唯一真实用途 = 图集满重试**
	//   （详设 △7 的 E33-1 已如实登记这处「初设 PD33-C 对 GL 不适用」）。
	GlyphSlot slot = ArcSlot(grid, p, /*binarize=*/false);
	if (!slot.valid)
	{
		// ★★ 降级（PD33-D / C33-7）：图集满 ⇒ **先试二值化槽位**（硬边弧体积不变，
		//   但内容更简、且**可能命中既有的硬边缓存**），**绝不**退回 `SolidSegment`——
		//   那是**直线段四边形**，会把弧降级成直线（C33-7 明令禁止）。
		slot = ArcSlot(grid, p, true);
		if (!slot.valid)
		{
			// 图集真的没有空间：**不画 + 一次 Warning**（L33-6——如实登记为有意的能力边界）
			Logger::Log(LogLevel::Warning,
			            L"GLRenderer: glyph atlas full - arc skipped");
			return;
		}
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

	// ★ 审计 M16：半径钳制改**整数口径**（与 GDI `GDIBackend.cpp:861-865` 逐字同式）——
	//   ① 量化 `lround(cornerRadius)`；② 上界 `min(w,h) / 2`（**整数**除法）；
	//   ③ clamp；④ effective 整数半径即角掩码键。
	//   首版用 `min(w,h)*0.5f` + `lround` ⇒ **奇数边长下 2R > min**：如 5×5 得 R=3，
	//   2R=6 > 5 ⇒ 三条「互不重叠」带（下方 :296-298）实际重叠 ⇒ 半透明色二次混合，
	//   且 :294 自称的 `2R <= min(w,h)` 不变式为假。整数 `/2` 恒保证 2R <= min。
	//   ★ 可达消费者：`ProgressBar` 默认 `effectiveRadius = GetHeight()/2`（奇数高即触发）。
	const int w = static_cast<int>(rect.width);
	const int h = static_cast<int>(rect.height);
	const int roundedRadius   = static_cast<int>(std::lround(cornerRadius));
	const int maxRadius       = (std::min)(w, h) / 2;
	const int radius          = std::clamp(roundedRadius, 0, maxRadius);
	if (radius <= 0)
	{
		DrawRect(rect, color);
		return;
	}

	const float x = rect.x, y = rect.y;
	const float fr = static_cast<float>(radius);
	Shade(color);

	// ★ 三条**互不重叠**的实心带（★ 与 GDI 的 `DrawRoundedRectOpaqueAA` 同款几何）——
	//   原实现的「竖带（全高）+ 横带（全宽）」在**中心区域重叠**，半透明色被二次混合
	//   ⇒ 实测中心明显变深、外圈偏浅（可见的「回」字形）。★ 整数 clamp 保证
	//   `2R <= min(w,h)`（M16 修复前该不变式为假），三条带的宽高恒 >= 0；
	//   零尺寸者自然退化（真圆时三条带全零 ⇒ 仅四角拼成整圆）。
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x, y + fr, w, h - 2.0f * fr);            // 中带（全宽）
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + fr, y, w - 2.0f * fr, fr);           // 上带
	TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + fr, y + h - fr, w - 2.0f * fr, fr);  // 下带

	const GlyphSlot corner = CornerSlot(radius);
	if (!corner.valid)
	{
		// ★★ 审计 #62 / L10（第二个落点）：图集满 ⇒ 补上四角的**实心降级**，不留四个黑洞。
		//   缺陷（修复前）：本处 `return` ⇒ 三条带已画、**四角留白** ⇒ 视觉 =「**十字形**」
		//   （四角四个洞）。可达消费者：`ProgressBar` 半胶囊、圆角面板、圆角按钮。
		//   ★ 修法沿用 GDI 的同一哲学（`GDIBackend.cpp:936-940`「**宁可无 AA，不可缺绘制**」：
		//     `DrawRoundedRect` 的 a==1 分支在补丁面取不到时**降级**到 `RoundRect`）——
		//     四角各补一个 `R×R` 的实心方块（走 1×1 全亮单元，`DrawRect` 走的同一 texel）。
		//   ⚠️ **非等价性如实记账**：降级后四角是**方块**而非圆角 ⇒ 视觉从「胶囊」变
		//     「带四个方角的圆角矩形」。同 L10 线降级的取舍：有形状 > 无形状。
		TexturedQuad(m_solid.x, m_solid.y, 1, 1, x,         y,         fr, fr);   // TL
		TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + w - fr, y,         fr, fr);   // TR
		TexturedQuad(m_solid.x, m_solid.y, 1, 1, x,         y + h - fr, fr, fr);   // BL
		TexturedQuad(m_solid.x, m_solid.y, 1, 1, x + w - fr, y + h - fr, fr, fr);   // BR
		return;
	}

	// 四角复用**同一份 canonical 掩码**，靠 UV 翻转取向（分 4 次调用 —— 与 GDI 的角索引变换等价）
	TexturedQuadUV(corner.x, corner.y, radius, radius, x, y, fr, fr, false, false);                       // TL
	TexturedQuadUV(corner.x, corner.y, radius, radius, x + w - fr, y, fr, fr, true, false);               // TR
	TexturedQuadUV(corner.x, corner.y, radius, radius, x, y + h - fr, fr, fr, false, true);               // BL
	TexturedQuadUV(corner.x, corner.y, radius, radius, x + w - fr, y + h - fr, fr, fr, true, true);       // BR
}

void GLRenderer::DrawImage(const Rect& dest, const Image& image)
{
	// ★ 审计 L14：dest 非正尺寸**不再绘制**。GL 侧无背面剔除 ⇒ 负尺寸的 quad 顶点顺序
	//   被翻转 ⇒ 画出**镜像四边形**（可见的鬼影）；GDI 侧 `FillRect`/`AlphaBlend` 对空矩形
	//   是 no-op。两端行为对齐：非正 dest ⇒ 两端都 no-op。
	if (dest.width <= 0.0f || dest.height <= 0.0f)
	{
		return;
	}

	// ★★ 审计 #62 / H3：**源数据完整性**守卫——与 `GDIBackend::DrawImage` **同一个符号**
	//   （`BackendContract::ImageIsDrawable`）。修复前 GL **完全无此校验**，而下面立即
	//   按 `pixels.data() + j * stride` 逐行读取 ⇒ 应用自造一个 `stride`/`pixels` 不自洽的
	//   `Image`（`PaintContext::DrawImage` 是**公开 API**）即**越界读**（UB）。
	//   审计原文定性：「同一命令：GDI 安全 no-op，GL 越界读 —— **后端决定内存安全**」。
	if (!m_ready || !BackendContract::ImageIsDrawable(image))
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

	// 焦点框以 lround 后的像素索引定位；裁切区必须使用相同的量化规则。
	// 否则缩放产生的 .5 坐标会使右/下边比 scissor 多出一个像素，进而被裁掉。
	// 注意：GDI PushClip 自 Phase 34 起走 floor（ToPixelRect = QuantizeEdge），本处 lround
	// 为跨后端已知 ≤1px 残差（deferred #75）——GL 内部与焦点框顶点的同口径不受影响。
	RECT rc{ static_cast<LONG>(std::lround(rect.x)), static_cast<LONG>(std::lround(rect.y)),
	         static_cast<LONG>(std::lround(rect.x + rect.width)),
	         static_cast<LONG>(std::lround(rect.y + rect.height)) };
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

	// 决策 32 同构：Begin/End 严格配对
	FRAMEWORK_ASSERT(m_inFrame);
	m_inFrame = false;

	if (m_doubleBuffered)
	{
		SwapBuffers(m_dc);
	}
	else
	{
		glFlush();
	}

	// ★★★ **更新区域验证的收尾**（见 `BeginFrame` 的说明）——必须**无条件**执行：
	//   漏掉 ⇒ 定时器饿死。★ 与 `m_ready` 无关：即便渲染中途失败，帧边界仍须闭合
	//   （故 `BeginFrame` 里 `!m_ready` 的早退不会与此处产生「Begin 了但没 End」的不配对）。
	if (m_paintBegun)
	{
		m_paintBegun = false;
		EndPaint(m_hwnd, &m_ps);
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

GlyphSlot GLRenderer::ArcSlot(const LineCoverageGrid& grid,
                              const BackendContract::ArcParams& p, bool binarize)
{
	// ★★ 键 = 11 元组（见头注释）。角度用 `QA` = 1/8°；`startDeg` 先 `Mod360`——
	//    既免疫超范围入参的 `lround`（§3.3 N4），又让 `10°` 与 `370°` 命中**同一槽位**
	//    （两者几何等价：`Mod360(a-10)` 与 `Mod360(a-370)` 恒等）。
	float startMod = std::fmod(p.startDeg, 360.0f);
	if (startMod < 0.0f)
	{
		startMod += 360.0f;
	}

	const auto key = std::make_tuple(
		grid.width, grid.height, grid.originX, grid.originY,
		static_cast<int>(std::lround(p.cx * 8.0f)),
		static_cast<int>(std::lround(p.cy * 8.0f)),
		static_cast<int>(std::lround(p.radius * 8.0f)),
		static_cast<int>(std::lround(startMod * 8.0f)),
		static_cast<int>(std::lround(p.sweepDeg * 8.0f)),
		static_cast<int>(std::lround(p.halfWidth * 2.0f * 8.0f)),
		binarize ? 1 : 0);

	const auto cached = m_arcSlots.find(key);
	if (cached != m_arcSlots.end())
	{
		return cached->second;
	}

	// ★ 二值化在 **Upload 之前**、作用于**局部拷贝**——`grid` 是值参但拷贝贵，
	//   且**不可**污染调用方语义（盯防⑨：拷贝必须被**消费**，否则等于没做）
	std::vector<std::uint8_t> pixels = grid.coverage;
	if (binarize)
	{
		for (std::uint8_t& c : pixels)
		{
			c = (c > 0) ? 255 : 0;
		}
	}

	const GlyphSlot slot = m_atlas->Allocate(grid.width, grid.height);
	if (slot.valid)
	{
		m_atlas->Upload(slot, pixels);
		m_arcSlots.emplace(key, slot);
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

	// ★ 审计 M13：退化/越界守卫。`glScissor` 对负宽高报 `GL_INVALID_VALUE` 且
	// **保留原 scissor 盒** ⇒ 上一级（父）的大 box 继续生效 ⇒ **欠裁剪**
	// （内容溢出到不该画的地方）+ 每帧一个 GL 错误。
	// 可达路径 = M11（TextBox 的 `PushClip` 收到负高文本区，`GetTextAreaHeight()`
	// = height - padding*2 在 `2*padding > height` 时为负）。
	// 修法：把 rc 钳进 framebuffer；钳后若为空（完全在外 / 退化）⇒ 宽高归 0
	// （GL 规定宽高 0 合法且不产错误）——与 GDI 的 `IntersectClipRect` 对空矩形容忍同族语义：
	// **空裁剪区 = 什么都不画**，而不是「残留父裁剪盒」。
	// ⚠️ 类型：rc 是 LONG、m_w/m_h 是 int ⇒ 统一升到 long 再比，避免模板推导冲突。
	const long left   = (std::max)(0L, (std::min)(static_cast<long>(rc.left),   static_cast<long>(m_w)));
	const long right  = (std::max)(0L, (std::min)(static_cast<long>(rc.right),  static_cast<long>(m_w)));
	const long bottom = (std::max)(0L, (std::min)(static_cast<long>(m_h) - static_cast<long>(rc.bottom), static_cast<long>(m_h)));
	const long top    = (std::max)(0L, (std::min)(static_cast<long>(m_h) - static_cast<long>(rc.top),    static_cast<long>(m_h)));
	const long width  = (std::max)(0L, right - left);
	const long height = (std::max)(0L, top - bottom);

	glEnable(GL_SCISSOR_TEST);
	glScissor(static_cast<GLint>(left), static_cast<GLint>(bottom),
	          static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

}
