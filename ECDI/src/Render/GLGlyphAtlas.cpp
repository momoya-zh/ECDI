#include "Render/GLGlyphAtlas.h"

#include "ECDI/Core/Logger.h"

// ⚠️ Win32 宏防护（条 10）：`GL/gl.h` 需要 `Windows.h` 先展开；`DrawText` 宏若已生效会污染
// 本编译单元里对 `RenderingBackend::DrawText` 的引用
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif
#include <GL/gl.h>

#include <algorithm>
#include <limits>

namespace ECDI{

GlyphSlot GLGlyphAtlas::Empty()
{
	return GlyphSlot{};
}

std::size_t GLGlyphAtlas::ClampMaxPages(std::size_t maxPages, bool warnOnClamp)
{
	if (maxPages < kMinMaxPages)
	{
		if (warnOnClamp)
		{
			Logger::Log(LogLevel::Warning,
			            L"GLGlyphAtlas: maxPages < 2 clamped to 2 to preserve protected Page 0");
		}
		return kMinMaxPages;
	}
	return maxPages;
}

GLGlyphAtlas::GLGlyphAtlas(std::size_t maxPages)
	: m_maxPages(ClampMaxPages(maxPages, false))
{
	PageState page0;
	page0.allocated = true;
	page0.protectedPage = true;
	page0.epoch = 1;
	m_pages.push_back(std::move(page0));
}

GLGlyphAtlas::~GLGlyphAtlas()
{
	// ★ 释放必须在 **current context** 下（由 `GLRenderer` 析构在其 context 尚存时销毁本对象）
	for (PageState& page : m_pages)
	{
		if (page.texture != 0)
		{
			const GLuint tex = page.texture;
			glDeleteTextures(1, &tex);
			page.texture = 0;
		}
	}
}

bool GLGlyphAtlas::EnsurePageTexture(PageState& page)
{
	// ★★ 前置条件：WGL context 已 current（详设 §3-⑧ 创建顺序不变量）
	// ★ 不设 `GL_CLAMP_TO_EDGE`：MSVC SDK 的 `<GL/gl.h>` 只到 **GL 1.1**，槽位间 1px 间隙防串色
	while (glGetError() != GL_NO_ERROR) { }

	GLuint tex = 0;
	glGenTextures(1, &tex);
	if (tex != 0)
	{
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// LUMINANCE_ALPHA：L 恒 255（乘 1 ⇒ 颜色完全由 `glColor4f` 决定）、A = 覆盖度
		const std::vector<std::uint8_t> blank(
			static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize) * 2, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, kSize, kSize, 0,
		             GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, blank.data());
	}

	if (tex == 0 || glGetError() != GL_NO_ERROR)
	{
		if (tex != 0)
		{
			glDeleteTextures(1, &tex);
		}
		page.texture = 0;
		page.allocated = false;
		Logger::Log(LogLevel::Error, L"GLGlyphAtlas: texture creation failed");
		return false;
	}

	page.texture = tex;
	page.allocated = true;
	return true;
}

void GLGlyphAtlas::Initialize()
{
	if (m_pages.empty())
	{
		PageState page0;
		page0.protectedPage = true;
		page0.epoch = 1;
		m_pages.push_back(std::move(page0));
	}
	m_ready = EnsurePageTexture(m_pages[0]);
}

void GLGlyphAtlas::SetMaxPagesForTests(std::size_t maxPages)
{
	m_maxPages = ClampMaxPages(maxPages, true);
}

std::size_t GLGlyphAtlas::PageCount() const noexcept
{
	std::size_t count = 0;
	for (const PageState& page : m_pages)
	{
		if (page.allocated)
		{
			++count;
		}
	}
	return count;
}

void GLGlyphAtlas::UpdateTotalUsedPixels() noexcept
{
	std::size_t total = 0;
	for (const PageState& page : m_pages)
	{
		if (page.allocated)
		{
			total += page.usedPixels;
		}
	}
	m_usedPixels = total;
}

GlyphSlot GLGlyphAtlas::AllocateOnPage(std::size_t pageIdx, int w, int h, bool recordPage0Full)
{
	if (w <= 0 || h <= 0 || w > kSize || h > kSize || pageIdx >= m_pages.size())
	{
		return Empty();
	}

	PageState& page = m_pages[pageIdx];
	if (!page.allocated)
	{
		return Empty();
	}

	if (page.shelfX + w > kSize)   // 行溢出 ⇒ 回卷到下一行
	{
		page.shelfX = 0;
		page.shelfY += page.shelfH + 1;   // +1 = 行间 1px 间隙
		page.shelfH = 0;
	}

	if (page.shelfY + h > kSize)
	{
		if (recordPage0Full)
		{
			++m_allocationFailures;
			if (!m_fullWarned)
			{
				m_fullWarned = true;
				Logger::Log(LogLevel::Warning,
				            L"GLGlyphAtlas: Page 0 full in single-page Allocate");
			}
		}
		return Empty();
	}

	GlyphSlot slot;
	slot.x = page.shelfX;
	slot.y = page.shelfY;
	slot.w = w;
	slot.h = h;
	slot.valid = true;
	slot.texture = page.texture;
	slot.pageIndex = static_cast<std::uint32_t>(pageIdx);
	slot.epoch = page.epoch;

	page.shelfX += w + 1;   // +1 = 列间 1px 间隙
	if (h > page.shelfH)
	{
		page.shelfH = h;
	}

	const std::size_t used = static_cast<std::size_t>(page.shelfY + page.shelfH) * static_cast<std::size_t>(kSize)
	                       + static_cast<std::size_t>(page.shelfX);
	if (used > page.usedPixels)
	{
		page.usedPixels = used;
	}
	page.lastUsedFrame = m_currentFrame;
	page.lastUsedSeq = ++m_touchSeq;
	UpdateTotalUsedPixels();
	return slot;
}

GlyphSlot GLGlyphAtlas::Allocate(int w, int h)
{
	// ★ 纯单页（Page 0）逻辑不变——保留 T26-7 / T46-8 兼容契约
	return AllocateOnPage(0, w, h, /*recordPage0Full=*/true);
}

void GLGlyphAtlas::PurgePageSlots(std::uint32_t pageIdx)
{
	if (pageIdx >= m_pages.size())
	{
		return;
	}
	PageState& page = m_pages[pageIdx];
	for (const GlyphKey& key : page.residentKeys)
	{
		const auto it = m_slots.find(key);
		if (it != m_slots.end() && it->second.pageIndex == pageIdx)
		{
			m_slots.erase(it);
		}
	}
	page.residentKeys.clear();
}

void GLGlyphAtlas::EvictPage(std::size_t victimIdx)
{
	if (victimIdx >= m_pages.size())
	{
		return;
	}
	PageState& page = m_pages[victimIdx];
	if (page.protectedPage || !page.allocated)
	{
		return;
	}

	if (m_ready && page.texture != 0)
	{
		const GLuint tex = page.texture;
		glDeleteTextures(1, &tex);
	}
	page.texture = 0;
	page.allocated = false;
	page.shelfX = 0;
	page.shelfY = 0;
	page.shelfH = 0;
	page.usedPixels = 0;
	++page.epoch;
	PurgePageSlots(static_cast<std::uint32_t>(victimIdx));
	UpdateTotalUsedPixels();
	++m_evictionCount;
	m_pendingBoundaryEvict = false;
}

void GLGlyphAtlas::ResetPageInPlace(std::size_t victimIdx)
{
	if (victimIdx >= m_pages.size())
	{
		return;
	}
	PageState& page = m_pages[victimIdx];
	if (page.protectedPage || !page.allocated)
	{
		return;
	}

	if (m_ready && page.texture != 0)
	{
		// ★ 极端帧内全池溢出：先 glFlush 确保此前已发射图元完成采样，严禁 glDeleteTextures 制造悬空句柄
		glFlush();
		glBindTexture(GL_TEXTURE_2D, page.texture);
		const std::vector<std::uint8_t> blank(
			static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize) * 2, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize,
		                GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, blank.data());
	}

	page.shelfX = 0;
	page.shelfY = 0;
	page.shelfH = 0;
	page.usedPixels = 0;
	page.lastResetSeq = ++m_resetSeq;
	page.lastUsedFrame = m_currentFrame;
	page.lastUsedSeq = ++m_touchSeq;
	++page.epoch;
	PurgePageSlots(static_cast<std::uint32_t>(victimIdx));
	UpdateTotalUsedPixels();
	++m_inFrameResetCount;
	m_pendingBoundaryEvict = true;
}

void GLGlyphAtlas::BeginFrame() noexcept
{
	++m_currentFrame;
	if (m_pendingBoundaryEvict && PageCount() >= m_maxPages)
	{
		std::size_t victim = m_pages.size();
		std::uint64_t bestFrame = (std::numeric_limits<std::uint64_t>::max)();
		std::uint64_t bestSeq = (std::numeric_limits<std::uint64_t>::max)();
		for (std::size_t i = 0; i < m_pages.size(); ++i)
		{
			const PageState& p = m_pages[i];
			if (!p.protectedPage && p.allocated && p.lastUsedFrame < m_currentFrame)
			{
				if (p.lastUsedFrame < bestFrame
				    || (p.lastUsedFrame == bestFrame && p.lastUsedSeq < bestSeq))
				{
					bestFrame = p.lastUsedFrame;
					bestSeq = p.lastUsedSeq;
					victim = i;
				}
			}
		}
		if (victim < m_pages.size())
		{
			EvictPage(victim);
		}
	}
	m_pendingBoundaryEvict = false;
}

GlyphSlot GLGlyphAtlas::AllocatePaged(int w, int h)
{
	if (w <= 0 || h <= 0 || w > kSize || h > kSize)
	{
		return Empty();
	}

	// ① 先依次试现有活跃页
	for (std::size_t i = 0; i < m_pages.size(); ++i)
	{
		if (m_pages[i].allocated)
		{
			const GlyphSlot s = AllocateOnPage(i, w, h, /*recordPage0Full=*/false);
			if (s.valid)
			{
				return s;
			}
		}
	}

	// ② 复用已被 EvictPage 释放的 !allocated 槽或扩容新页（至多 m_maxPages 个活跃页）
	std::size_t freeIdx = m_pages.size();
	for (std::size_t i = 0; i < m_pages.size(); ++i)
	{
		if (!m_pages[i].allocated)
		{
			freeIdx = i;
			break;
		}
	}
	if (freeIdx < m_pages.size() || PageCount() < m_maxPages)
	{
		if (freeIdx == m_pages.size())
		{
			PageState fresh;
			fresh.protectedPage = false;
			fresh.epoch = 1;   // 新建页 epoch 初始为 1；复用 !allocated 页保留 EvictPage 递增后代际
			m_pages.push_back(std::move(fresh));
		}
		PageState& page = m_pages[freeIdx];
		page.shelfX = 0;
		page.shelfY = 0;
		page.shelfH = 0;
		page.usedPixels = 0;
		page.lastUsedFrame = m_currentFrame;
		page.lastUsedSeq = ++m_touchSeq;
		page.lastResetSeq = ++m_resetSeq;

		if (m_ready)
		{
			if (!EnsurePageTexture(page))
			{
				++m_allocationFailures;
				return Empty();
			}
		}
		else
		{
			page.texture = 0;
			page.allocated = true;
		}
		return AllocateOnPage(freeIdx, w, h, /*recordPage0Full=*/false);
	}

	// ③ 全池满容：优先跨帧过期 Page-LRU（lastUsedFrame < m_currentFrame）
	std::size_t lruVictim = m_pages.size();
	std::uint64_t bestFrame = (std::numeric_limits<std::uint64_t>::max)();
	std::uint64_t bestSeq = (std::numeric_limits<std::uint64_t>::max)();
	for (std::size_t i = 0; i < m_pages.size(); ++i)
	{
		const PageState& p = m_pages[i];
		if (!p.protectedPage && p.allocated && p.lastUsedFrame < m_currentFrame)
		{
			if (p.lastUsedFrame < bestFrame
			    || (p.lastUsedFrame == bestFrame && p.lastUsedSeq < bestSeq))
			{
				bestFrame = p.lastUsedFrame;
				bestSeq = p.lastUsedSeq;
				lruVictim = i;
			}
		}
	}
	if (lruVictim < m_pages.size())
	{
		EvictPage(lruVictim);
		PageState& page = m_pages[lruVictim];
		page.lastUsedFrame = m_currentFrame;
		page.lastUsedSeq = ++m_touchSeq;
		page.lastResetSeq = ++m_resetSeq;
		if (m_ready)
		{
			if (!EnsurePageTexture(page))
			{
				++m_allocationFailures;
				return Empty();
			}
		}
		else
		{
			page.texture = 0;
			page.allocated = true;
		}
		return AllocateOnPage(lruVictim, w, h, /*recordPage0Full=*/false);
	}

	// ④ 极端单帧全池溢出（所有非保护页都在当前帧使用过）：就地重置（不删句柄）
	std::size_t inFrameVictim = m_pages.size();
	std::uint64_t bestResetSeq = (std::numeric_limits<std::uint64_t>::max)();
	for (std::size_t i = 0; i < m_pages.size(); ++i)
	{
		const PageState& p = m_pages[i];
		if (!p.protectedPage && p.allocated && p.lastResetSeq < bestResetSeq)
		{
			bestResetSeq = p.lastResetSeq;
			inFrameVictim = i;
		}
	}
	if (inFrameVictim < m_pages.size())
	{
		ResetPageInPlace(inFrameVictim);
		return AllocateOnPage(inFrameVictim, w, h, /*recordPage0Full=*/false);
	}

	return Empty();
}

bool GLGlyphAtlas::IsSlotAlive(const GlyphSlot& slot) const noexcept
{
	if (!slot.valid || slot.pageIndex >= m_pages.size())
	{
		return false;
	}
	const PageState& p = m_pages[slot.pageIndex];
	if (!p.allocated || p.epoch != slot.epoch)
	{
		return false;
	}
	return !m_ready || (slot.texture != 0 && slot.texture == p.texture);
}

void GLGlyphAtlas::TouchSlot(const GlyphSlot& slot) noexcept
{
	if (!IsSlotAlive(slot))
	{
		return;
	}
	PageState& p = m_pages[slot.pageIndex];
	p.lastUsedFrame = m_currentFrame;
	p.lastUsedSeq = ++m_touchSeq;
}

void GLGlyphAtlas::Upload(const GlyphSlot& slot, const std::vector<std::uint8_t>& a8)
{
	// ★★ 防御（沿 spike v3.1 实测教训）：**槽位无效** 或 **尺寸失配** 时必须直接返回
	if (!slot.valid)
	{
		return;
	}
	if (slot.texture == 0)
	{
		Logger::Log(LogLevel::Warning,
		            L"GLGlyphAtlas::Upload: invalid texture handle 0; skipped");
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

	glBindTexture(GL_TEXTURE_2D, slot.texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, slot.x, slot.y, slot.w, slot.h,
	                GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la.data());
}

GlyphSlot GLGlyphAtlas::GetOrCreate(const GlyphKey& key, FontEngine& engine)
{
	const auto cached = m_slots.find(key);
	if (cached != m_slots.end())
	{
		if (cached->second.valid && cached->second.w == 0 && cached->second.h == 0)
		{
			return cached->second;
		}
		if (!cached->second.valid || IsSlotAlive(cached->second))
		{
			if (cached->second.valid)
			{
				TouchSlot(cached->second);
			}
			return cached->second;
		}
		m_slots.erase(cached);
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
			const GlyphSlot region = AllocatePaged(bitmap.width, bitmap.height);
			if (region.valid)
			{
				if (region.texture != 0)
				{
					Upload(region, bitmap.a8);
				}
				slot.x = region.x;
				slot.y = region.y;
				slot.w = region.w;
				slot.h = region.h;
				slot.texture = region.texture;
				slot.pageIndex = region.pageIndex;
				slot.epoch = region.epoch;
				slot.valid = true;
				m_slots.emplace(key, slot);
				m_pages[slot.pageIndex].residentKeys.push_back(key);
			}
			// ★ 瞬态分配失败不写入永久负缓存，确保后续淘汰释页后可恢复绘制
			return slot;
		}
		// ★ 空白字形（如空格）：无需分配页槽，直接记为有效结果
		slot.valid = true;
		m_slots.emplace(key, slot);
	}

	return slot;
}

}
