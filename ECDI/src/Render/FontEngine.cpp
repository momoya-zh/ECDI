#include "Render/FontEngine.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"   // UTF8ToWide（告警消息用）
#include "Render/Utf8Decode.h"  // ★ Phase 26 批三：与 GLRenderer **共用同一解码**

// ★★ `FT_*` 出现在**本编译单元内**（pimpl —— 头文件与框架其余部分零 FreeType：详设盯防①）
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ECDI{

namespace{

// ★ 缓存上限（防无界增长——与 GDI 测量缓存同策略；★ LRU 不做，见详设 §9 O9）
constexpr std::size_t kMaxGlyphCache = 8192;
constexpr std::size_t kMaxMeasureCache = 4096;
constexpr std::size_t kMaxLineHeightCache = 256;

// ★ UTF-8 解码已上提为 `Render/Utf8Decode.h`（Phase 26 批三：测量链与渲染链**共用**——
//   两链若各自解码，非法/截断字节处的码点切分会不一致 ⇒ 测宽与渲宽失配）

}   // namespace

// ══════════════════════════════════════════════════════════════════════════
// Impl —— ★ `FT_*` 与所有平台无关资源都收敛在此（pimpl）
// ══════════════════════════════════════════════════════════════════════════
struct FontEngine::Impl{

	FT_Library library = nullptr;

	struct FaceEntry{
		FaceId id = 0;
		FT_Face face = nullptr;
	};

	/// ★ 缓存键用**解析后的路径**（不是 family 原串）：空 family 与显式默认文件名
	///   因此**复用同一 face**（比"按 family 字符串"更准——实现相对详设的细化）
	std::map<std::string, FaceEntry> faces;
	std::map<FaceId, FT_Face> facesById;   ///< id → face（O(log n) 反查）
	int nextFaceId = 1;                    ///< ★ 从 1 起（**0 恒为「无效」**）；单调递增、**永不复用**
	std::map<std::string, FaceId> familyCache;   ///< family 原串 → FaceId（★ v1.1：含**失败结果**——
	                                             ///< 未命中 family 每帧每标签重复「探测文件 + 告警」的刷屏与开销由此消除）

	/// ── CPU 两级缓存（Phase 26 详设 §3-③）──
	std::map<GlyphKey, GlyphBitmap> glyphCache;                                     ///< 渲染链：栅格化结果
	std::map<std::tuple<std::string, float, std::string, int>, Size> measureCache;  ///< 测量链：metrics
	std::map<std::tuple<float, std::string, int>, float> lineHeightCache;
	std::map<std::pair<FaceId, int>, float> ascentCache;                            ///< (faceId, px) → ascent(px)

	std::unique_ptr<FontSource> source;
	int dpi = 96;

	std::size_t rasterizeCount = 0;
	std::size_t measureMissCount = 0;

	FT_Face FaceOf(FaceId id) const{
		const auto it = facesById.find(id);
		return (it != facesById.end()) ? it->second : nullptr;
	}
};

FontEngine::FontEngine()
	: m_impl(std::make_unique<Impl>())
{
	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0)
	{
		Logger::Log(LogLevel::Error, L"FontEngine: FT_Init_FreeType failed");
		return;
	}
	m_impl->library = library;
}

FontEngine::~FontEngine()
{
	if (!m_impl)
	{
		return;
	}

	for (auto& entry : m_impl->faces)
	{
		if (entry.second.face != nullptr)
		{
			FT_Done_Face(entry.second.face);
		}
	}
	m_impl->faces.clear();
	m_impl->facesById.clear();

	if (m_impl->library != nullptr)
	{
		FT_Done_FreeType(m_impl->library);
		m_impl->library = nullptr;
	}
}

void FontEngine::SetFontSource(std::unique_ptr<FontSource> source)
{
	m_impl->source = std::move(source);
}

void FontEngine::SetDpi(int dpi)
{
	m_impl->dpi = (dpi > 0) ? dpi : 96;
}

int FontEngine::PixelSize(const Font& font) const
{
	// ★★ Phase 26 **唯一 DIP→px 路径**（详设 D26-2）——与既有 GDI 两条链**同一公式**
	//    （`lround(size·dpi/96)`，**float 口径**）。★ 不得改用 `DpiConversion::DipToPixels`
	//    （几何链路的 int 口径——`DpiConversion.h:21-22` 明写两条链路不得合并）。
	const int dpi = (m_impl->dpi > 0) ? m_impl->dpi : 96;
	const long long px = std::llround(static_cast<double>(font.size) * static_cast<double>(dpi) / 96.0);
	return static_cast<int>((std::max)(1LL, px));   // ★ 下限 1（字号 0/负会让 FreeType 拒绝）
}

FaceId FontEngine::FaceIdFor(const std::string& family)
{
	Impl& impl = *m_impl;
	if (impl.library == nullptr || !impl.source)
	{
		return 0;
	}

	// ★ v1.1：family 原串 → FaceId 缓存（**含失败结果**）——否则解析失败的 family
	//   （如 "Consolas" 被当作文件名找不到）每帧每标签重复「探测文件 + 告警」刷屏
	const auto famCached = impl.familyCache.find(family);
	if (famCached != impl.familyCache.end())
	{
		return famCached->second;
	}

	// ① 解析文件路径（空 family ⇒ 默认字体）
	std::string path = impl.source->ResolveFile(family);
	if (path.empty() && !family.empty())
	{
		// ★ **不静默降级**：明确告警，再回退默认 face（详设 △2 / 外部评审 §11）
		Logger::Log(LogLevel::Warning,
		            L"FontEngine: font family not found; falling back to the default face: "
		            + UTF8ToWide(family));
		path = impl.source->ResolveFile(std::string());
	}
	if (path.empty())
	{
		impl.familyCache.emplace(family, 0);   // 失败结果也缓存（防每帧重试）
		return 0;
	}

	// ② 路径命中 ⇒ 复用（★ 同一文件只加载一次）
	const auto cached = impl.faces.find(path);
	if (cached != impl.faces.end())
	{
		return cached->second.id;
	}

	// ③ 加载 face
	FT_Face face = nullptr;
	if (FT_New_Face(impl.library, path.c_str(), 0, &face) != 0 || face == nullptr)
	{
		Logger::Log(LogLevel::Warning, L"FontEngine: FT_New_Face failed for resolved font file");
		return 0;
	}

	// ④ 分配**新** FaceId（单调递增、永不复用）+ family 原串入缓存
	const FaceId id = impl.nextFaceId++;
	impl.faces.emplace(path, Impl::FaceEntry{ id, face });
	impl.facesById.emplace(id, face);
	impl.familyCache.emplace(family, id);
	return id;
}

std::uint32_t FontEngine::GlyphIndex(const Font& font, char32_t cp)
{
	FT_Face face = m_impl->FaceOf(FaceIdFor(font.family));
	if (face == nullptr)
	{
		return 0;
	}

	// ★ 内部即 `FT_Get_Char_Index` ⇒ `FT_*` 仍只出现在本编译单元（契约 C-4）
	return static_cast<std::uint32_t>(FT_Get_Char_Index(face, static_cast<FT_ULong>(cp)));
}

std::size_t FontEngine::MeasureTextCacheMissCount() const noexcept
{
	return m_impl->measureMissCount;
}

std::size_t FontEngine::RasterizeCount() const noexcept
{
	return m_impl->rasterizeCount;
}

Size FontEngine::MeasureText(const Font& font, const std::string& text)
{
	Impl& impl = *m_impl;
	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);

	// ★ metrics cache（键含 **dpi**）——消掉「每帧 × 每行」的重复度量（性能方向②）
	const auto key = std::make_tuple(text, font.size, font.family, dpi);
	const auto cached = impl.measureCache.find(key);
	if (cached != impl.measureCache.end())
	{
		return cached->second;
	}
	++impl.measureMissCount;

	Size result{};
	FT_Face face = impl.FaceOf(FaceIdFor(font.family));
	if (face != nullptr)
	{
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

		const std::vector<char32_t> codepoints = DecodeUtf8(text);
		long advancePx = 0;
		for (const char32_t cp : codepoints)
		{
			// ★ 与渲染链**同一 load flags**（含 FT_LOAD_NO_BITMAP，v1.1）——内嵌点阵的
			//   advance 与矢量 advance 不同 ⇒ 测量/绘制异源会错位（选择/光标对不齐笔画）
			if (FT_Load_Char(face, static_cast<FT_ULong>(cp),
			                 FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP) != 0)
			{
				continue;   // 缺字形 ⇒ 跳过（advance 不计）
			}
			advancePx += static_cast<long>(face->glyph->advance.x >> 6);
		}

		// ★ 物理像素 → **DIP**（`TextMeasurer` 契约；float 口径——与 GDI 测量链同族）
		const float toDip = 96.0f / static_cast<float>(dpi);
		result.width  = static_cast<float>(advancePx) * toDip;
		result.height = static_cast<float>(face->size->metrics.height >> 6) * toDip;
	}

	if (impl.measureCache.size() >= kMaxMeasureCache)
	{
		impl.measureCache.clear();   // ★ 上限 ⇒ 清空（O(1)，防无界增长）
	}
	impl.measureCache.emplace(key, result);
	return result;
}

float FontEngine::LineHeight(const Font& font)
{
	Impl& impl = *m_impl;
	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);

	const auto key = std::make_tuple(font.size, font.family, dpi);
	const auto cached = impl.lineHeightCache.find(key);
	if (cached != impl.lineHeightCache.end())
	{
		return cached->second;
	}

	float height = font.size;   // 兜底：字号（**已是 DIP**——与返回值单位一致）
	FT_Face face = impl.FaceOf(FaceIdFor(font.family));
	if (face != nullptr)
	{
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
		height = static_cast<float>(face->size->metrics.height >> 6)
		       * (96.0f / static_cast<float>(dpi));
	}

	if (impl.lineHeightCache.size() >= kMaxLineHeightCache)
	{
		impl.lineHeightCache.clear();
	}
	impl.lineHeightCache.emplace(key, height);
	return height;
}

float FontEngine::Ascent(const Font& font)
{
	Impl& impl = *m_impl;
	const int px = PixelSize(font);

	const auto cached = impl.ascentCache.find({ FaceIdFor(font.family), px });
	if (cached != impl.ascentCache.end())
	{
		return cached->second;
	}

	float ascent = 0.0f;
	FT_Face face = impl.FaceOf(FaceIdFor(font.family));
	if (face != nullptr)
	{
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

		// ★ 与栅格化**同一 face / 同一 pixelSize / 同一转换**（D26-2 唯一路径）——
		//   26.6 定点 ⇒ /64 得物理像素（`metrics.ascender` 向上为正）
		ascent = static_cast<float>(face->size->metrics.ascender) / 64.0f;
	}

	impl.ascentCache.emplace(std::make_pair(FaceIdFor(font.family), px), ascent);
	return ascent;
}

bool FontEngine::Glyph(const Font& font, char32_t cp, GlyphBitmap& out)
{
	// ★ Phase 26 批三：本方法 = 「构建键 → 委派 `GlyphByKey`」（键型缓存单点，避免两处实现漂移）
	const std::uint32_t glyphIndex = GlyphIndex(font, cp);
	if (glyphIndex == 0)
	{
		return false;   // 该 face 无此字形（或 face 无效）
	}

	const GlyphKey key{ FaceIdFor(font.family), glyphIndex, PixelSize(font),
	                    static_cast<std::uint8_t>(HintingMode::Normal) };
	return GlyphByKey(key, out);
}

bool FontEngine::GlyphByKey(const GlyphKey& key, GlyphBitmap& out)
{
	Impl& impl = *m_impl;
	FT_Face face = impl.FaceOf(key.faceId);
	if (face == nullptr)
	{
		return false;
	}

	const auto cached = impl.glyphCache.find(key);
	if (cached != impl.glyphCache.end())
	{
		out = cached->second;   // ★ 命中 ⇒ **不重复栅格化**（`RasterizeCount` 不增）
		return true;
	}

	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(key.pixelSize));

	// ★ rasterization policy 落到**具体调用**（详设 §1.3 ③）：
	//   `FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL` + `FT_RENDER_MODE_NORMAL`（8-bit 灰度 AA）
	//   ★ + `FT_LOAD_NO_BITMAP`（v1.1）：**强制矢量渲染**——① 本类的位图拷贝按 8bpp 灰度
	//   写死（FT_PIXEL_MODE_MONO 的 1bpp 内嵌点阵会被错读 = 字形污染）；② 内嵌点阵是
	//   GDI 观感的产物，GL 矢量渲染保持质量一致；③ SimSun 在 12~16px 有 MONO 点阵，
	//   默认字体改 SimSun 后此 flag 从「可选」变「必需」。
	if (FT_Load_Glyph(face, key.glyphIndex,
	                  FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP) != 0)
	{
		return false;
	}
	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
	{
		return false;
	}
	++impl.rasterizeCount;   // ★ 观测缝（供 T26-3 + 基准指标）

	GlyphBitmap bitmap;
	bitmap.width    = static_cast<int>(face->glyph->bitmap.width);
	bitmap.height   = static_cast<int>(face->glyph->bitmap.rows);
	bitmap.bearingX = static_cast<float>(face->glyph->bitmap_left);
	bitmap.bearingY = static_cast<float>(face->glyph->bitmap_top);
	bitmap.advance  = static_cast<float>(face->glyph->advance.x >> 6);

	if (bitmap.width > 0 && bitmap.height > 0)
	{
		bitmap.a8.resize(static_cast<std::size_t>(bitmap.width)
		                 * static_cast<std::size_t>(bitmap.height));

		// ★ `FT_RENDER_MODE_NORMAL` 的 `FT_Bitmap` 是 top-down、pitch > 0；逐行拷（不用 memcpy
		//   以免假设 pitch == width）
		const unsigned char* const src = face->glyph->bitmap.buffer;
		const int pitch = face->glyph->bitmap.pitch;
		for (int y = 0; y < bitmap.height; ++y)
		{
			const unsigned char* const row = src + static_cast<std::ptrdiff_t>(y) * pitch;
			for (int x = 0; x < bitmap.width; ++x)
			{
				bitmap.a8[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width)
				          + static_cast<std::size_t>(x)] = row[x];
			}
		}
	}
	// ★ 0×0 位图（如空格）是**合法结果**——`advance` 有效，返回 true（调用方仍须推进笔位）

	if (impl.glyphCache.size() >= kMaxGlyphCache)
	{
		impl.glyphCache.clear();
	}
	impl.glyphCache.emplace(key, bitmap);
	out = bitmap;
	return true;
}

}
