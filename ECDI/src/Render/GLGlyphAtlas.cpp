#include "Render/GLGlyphAtlas.h"

#include "ECDI/Core/Logger.h"

// ⚠️ Win32 宏防护（条 10）：`GL/gl.h` 需要 `Windows.h` 先展开；`DrawText` 宏若已生效会污染
// 本编译单元里对 `RenderingBackend::DrawText` 的引用
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif
#include <GL/gl.h>

namespace ECDI{

GlyphSlot GLGlyphAtlas::Empty()
{
	return GlyphSlot{};
}

GLGlyphAtlas::~GLGlyphAtlas()
{
	// ★ 释放必须在 **current context** 下（由 `GLRenderer` 析构在其 context 尚存时销毁本对象）
	if (m_texture != 0)
	{
		const GLuint tex = m_texture;
		glDeleteTextures(1, &tex);
		m_texture = 0;
	}
}

void GLGlyphAtlas::Initialize()
{
	// ★★ 前置条件：WGL context 已 current（详设 §3-⑧ 创建顺序不变量——`GLRenderer::Initialize`
	//    先建 context，再调本函数，最后才置 `ready`）
	// ★ 不设 `GL_CLAMP_TO_EDGE`：MSVC SDK 的 `<GL/gl.h>` 只到 **GL 1.1**，该枚举（GL 1.2）不在其中；
	//   而图集槽位之间留了 1px 间隙（见 `Allocate`）⇒ 用默认 `GL_REPEAT` 不会串色（沿 spike 实测结论）
	while (glGetError() != GL_NO_ERROR) { }   // 清历史错误，使下面的一次判据可信

	glGenTextures(1, &m_texture);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// LUMINANCE_ALPHA：L 恒 255（乘 1 ⇒ 颜色完全由 `glColor4f` 决定）、A = 覆盖度
	std::vector<std::uint8_t> blank(static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize) * 2, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, kSize, kSize, 0,
	             GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, blank.data());

	m_ready = (m_texture != 0) && (glGetError() == GL_NO_ERROR);
	if (!m_ready)
	{
		Logger::Log(LogLevel::Error, L"GLGlyphAtlas: texture creation failed");
	}
}

GlyphSlot GLGlyphAtlas::Allocate(int w, int h)
{
	// ★ 纯逻辑（不碰 GL）——T26-7 的观测面
	if (w <= 0 || h <= 0 || w > kSize || h > kSize)
	{
		return Empty();             // 尺寸非法（含越界）⇒ 分配失败
	}

	if (m_shelfX + w > kSize)       // 行溢出 ⇒ 回卷到下一行
	{
		m_shelfX = 0;
		m_shelfY += m_shelfH + 1;   // +1 = 行间 1px 间隙（防邻行采样串色）
		m_shelfH = 0;
	}

	if (m_shelfY + h > kSize)       // 图集满（★ 无淘汰——详设 §9 O3）
	{
		if (!m_fullWarned)
		{
			m_fullWarned = true;
			Logger::Log(LogLevel::Warning,
			            L"GLGlyphAtlas: atlas full; further glyphs are skipped (no eviction in this phase)");
		}
		return Empty();
	}

	GlyphSlot slot;
	slot.x = m_shelfX;
	slot.y = m_shelfY;
	slot.w = w;
	slot.h = h;
	slot.valid = true;

	m_shelfX += w + 1;              // +1 = 列间 1px 间隙
	if (h > m_shelfH)
	{
		m_shelfH = h;
	}

	const std::size_t used = static_cast<std::size_t>(m_shelfY + m_shelfH) * static_cast<std::size_t>(kSize)
	                       + static_cast<std::size_t>(m_shelfX);
	if (used > m_usedPixels)
	{
		m_usedPixels = used;
	}
	return slot;
}

void GLGlyphAtlas::Upload(const GlyphSlot& slot, const std::vector<std::uint8_t>& a8)
{
	// ★★ 防御（沿 spike v3.1 实测教训）：**槽位无效** 或 **尺寸失配** 时必须直接返回——
	//    否则下面的填充循环会写出 `w * h * 2` 之外 ⇒ **堆越界写**
	if (!slot.valid)
	{
		return;
	}
	if (a8.size() != static_cast<std::size_t>(slot.w) * static_cast<std::size_t>(slot.h))
	{
		Logger::Log(LogLevel::Warning, L"GLGlyphAtlas: upload size mismatch; skipped (heap-overrun guard)");
		return;
	}

	std::vector<std::uint8_t> la(a8.size() * 2);
	for (std::size_t i = 0; i < a8.size(); ++i)
	{
		la[i * 2 + 0] = 255;        // L = 255（颜色交给 glColor4f）
		la[i * 2 + 1] = a8[i];      // A = 覆盖度
	}

	glBindTexture(GL_TEXTURE_2D, m_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, slot.x, slot.y, slot.w, slot.h,
	                GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la.data());
}

GlyphSlot GLGlyphAtlas::GetOrCreate(const GlyphKey& key, FontEngine& engine)
{
	const auto cached = m_slots.find(key);
	if (cached != m_slots.end())
	{
		return cached->second;      // ★ 命中 ⇒ 不重复栅格化、不重复分配、不重复上传
	}

	++m_missCount;

	GlyphSlot slot;
	GlyphBitmap bitmap;
	if (engine.GlyphByKey(key, bitmap))
	{
		slot.bearingX  = bitmap.bearingX;
		slot.bearingY  = bitmap.bearingY;
		slot.advance   = bitmap.advance;
		slot.pixelSize = key.pixelSize;

		if (bitmap.width > 0 && bitmap.height > 0)
		{
			const GlyphSlot region = Allocate(bitmap.width, bitmap.height);
			if (region.valid)
			{
				Upload(region, bitmap.a8);
				slot.x = region.x;
				slot.y = region.y;
				slot.w = region.w;
				slot.h = region.h;
			}
			// ★ region 无效（图集满）⇒ slot.valid 保持 false，但 bearing/advance 仍有效
			//   ⇒ 调用方照常推进笔位（详设 §1.4「不得 if (!valid) continue;」）
			slot.valid = region.valid;
		}
		else
		{
			// ★ 空白字形（如空格）：**无需分配**（w = h = 0）——advance 有效即合法结果
			slot.valid = true;
		}
	}

	// ★ 失败结果**也缓存**：① 避免每帧重试；② 让 `MissCount` 语义 = 「图集里没有过的键数」
	m_slots.emplace(key, slot);
	return slot;
}

}
