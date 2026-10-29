#include "Render/FontEngine.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"   // UTF8ToWide（告警消息用）
#include "ECDI/Core/UTF8.h"     // ★ Phase 29 △3：CodepointIndexToByteOffset（码点数上界/切片换算）
#include "Render/Utf8Decode.h"  // ★ Phase 26 批三：与 GLRenderer **共用同一解码**
#include "Render/FontWeightRule.h"  // ★ Phase 36 批二：档位归并纯函数 + ClampWeight（C36-1/2）

// ★★ `FT_*` 出现在**本编译单元内**（pimpl —— 头文件与框架其余部分零 FreeType：详设盯防①）
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SFNT_NAMES_H      // ★ Phase 36 批二：nameID-1 直读（family_name 偏好 nameID-16——探针 3 证伪）
#include FT_TRUETYPE_TABLES_H // ★ Phase 36 批二：OS/2 usWeightClass / fsSelection（档位 + 斜体位）

#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr std::size_t kMaxAdvanceCache = 8192;   ///< Phase 28 批一：advance memo 上界（与 glyphCache 同策略）

// ★ Phase 36 批二：name 记录 UTF-16BE → UTF-8（name 表字符串字节序为大端——盯防②）
std::string Utf16BEToUtf8(const unsigned char* data, FT_UShort len)
{
	std::string out;
	for (FT_UShort i = 0; i + 1 < len; i += 2)
	{
		const unsigned cp = (static_cast<unsigned>(data[i]) << 8) | data[i + 1];
		if (cp < 0x80)
		{
			out += static_cast<char>(cp);
		}
		else if (cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

/// ★ Phase 36 批二：nameID-1 EN 直读（platform 3 · language 0x409）——
///    **不得用 `family_name`**（探针 3：FT 偏好 nameID-16，msyhl f0 会读成 'Microsoft YaHei'）。
///    无记录 ⇒ 空串 ⇒ 过滤判据恒 false（该 face 出局——盯防③）。
std::string ReadNameId1En(FT_Face face)
{
	FT_UInt count = FT_Get_Sfnt_Name_Count(face);
	if (count > 256)
	{
		count = 256;
	}
	for (FT_UInt i = 0; i < count; ++i)
	{
		FT_SfntName n{};
		if (FT_Get_Sfnt_Name(face, i, &n) != 0)
		{
			continue;
		}
		if (n.name_id == 1 && n.platform_id == 3 && n.language_id == 0x409)
		{
			return Utf16BEToUtf8(n.string, n.string_len);
		}
	}
	return {};
}
/// advance memo 缺字形哨兵（成功 advance = `advance.x >> 6`，量级远小于此 ⇒ 不会混淆）
constexpr long kMissingAdvance = (std::numeric_limits<long>::min)();

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
		long numFaces = 1;               ///< Phase 36 批二：该文件 face 总数（face0 打开时读得——先开 0 再扫）
		std::string nameId1En;           ///< Phase 36 批二：nameID-1 EN 直读（过滤判据——**非** family_name）
		std::uint16_t os2Weight = 400;   ///< Phase 36 批二：档位 = `OS/2.usWeightClass`（缺表记 400 + 告警）
		bool italic = false;             ///< Phase 36 批二：`OS/2.fsSelection` bit0（斜体面穿族名过滤——consolaz 实测——显式排除）
	};

	/// ★ 缓存键用**解析后的路径**（不是 family 原串）：空 family 与显式默认文件名
	///   因此**复用同一 face**（比"按 family 字符串"更准——实现相对详设的细化）
	std::map<std::pair<std::string, int>, FaceEntry> faces;   ///< Phase 36 批二：键 = (解析后路径, faceIndex)
	std::map<FaceId, FT_Face> facesById;   ///< id → face（O(log n) 反查）
	int nextFaceId = 1;                    ///< ★ 从 1 起（**0 恒为「无效」**）；单调递增、**永不复用**
	std::map<std::pair<std::string, std::uint16_t>, FaceId> familyCache;   ///< Phase 36 批二：(family, weight) → FaceId   ///< family 原串 → FaceId（★ v1.1：含**失败结果**——
	                                             ///< 未命中 family 每帧每标签重复「探测文件 + 告警」的刷屏与开销由此消除）

	/// ── CPU 两级缓存（Phase 26 详设 §3-③）──
	std::map<GlyphKey, GlyphBitmap> glyphCache;                                     ///< 渲染链：栅格化结果
	std::map<std::tuple<std::uint16_t, std::string, float, std::string, int>, Size> measureCache;   ///< Phase 36 批二 K8-⑤：键含 weight  ///< 测量链：metrics
	std::map<std::tuple<std::uint16_t, float, std::string, int>, float> lineHeightCache;   ///< Phase 36 批二 K8-⑥：键含 weight
	std::map<std::pair<FaceId, int>, float> ascentCache;                            ///< (faceId, px) → ascent(px)
	std::map<GlyphKey, long> advanceCache;                                          ///< Phase 28 批一：per-glyph advance memo（复用 GlyphKey——C28-2）
	std::size_t advanceMemoMissCount = 0;                                           ///< 观测缝（T28-1 真命中断言）

	std::unique_ptr<FontSource> source;
	int dpi = 96;

	std::size_t rasterizeCount = 0;
	std::size_t measureMissCount = 0;

	FT_Face FaceOf(FaceId id) const{
		const auto it = facesById.find(id);
		return (it != facesById.end()) ? it->second : nullptr;
	}

		/// @brief 按 (路径, faceIndex) 取/载 face（Phase 36 批二；详设 §2.3-④）
		/// @details 打开成功 ⇒ 读 nameID-1 EN / OS/2 档位 / 斜体位入 entry；
		///          打开失败 ⇒ 告警 + nullptr（family 级缓存兜底防重试——T26-13 锚）。
		FaceEntry* EnsureFace(const std::string& path, long index);

	/// @brief 单码点 advance 求值（★ Phase 29 △3 提取：`MeasureText` 与 `FitText` **共用同一路径**）
	/// @details ★ 共享是**逐位一致**的前提：两方法若各自实现 memo/哨兵逻辑，一旦某侧漂移
	///          （load flags、skip 语义、求和顺序），「测出来的宽」与「断行算出的宽」会失配
	///          ——表现 = 断行位置与绘制错位。批次一的 memo 语义（C28-1/2/4）**原样保留**。
	/// @param face 已 `FT_Set_Pixel_Sizes(px)` 的 face（★ 依赖调用方设置——advance 随 pixelSize 变）
	/// @param faceId memo 键的 face 维（★ FaceId 永不复用＝既有不变量升格契约 C28-2）
	/// @param px     memo 键的 pixelSize 维（★ DPI 经此入 key）
	/// @param cp   码点
	/// @param out  该码点 advance（**物理像素**，`advance.x >> 6`）
	/// @return false = 缺字形 / 加载失败 ⇒ `out` 不变（调用方跳过该码点——同 `continue` 语义）
	bool AdvanceOf(FT_Face face, FaceId faceId, int px, char32_t cp, long& out)
	{
		// ★ 与渲染链**同一 load flags**（含 FT_LOAD_NO_BITMAP）——内嵌点阵的 advance 与
		//   矢量 advance 不同 ⇒ 测量/绘制异源会错位（选择/光标对不齐笔画）
		const FT_UInt glyphIndex = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
		// ★ gid = 0（未映射）**永不入 memo**：未映射字符保持 `FT_Load_Char` 原路径
		//   （防把「缺字形」与「charmap 命中 .notdef」的病态 face 混淆——C28-1）
		const GlyphKey key{ faceId, static_cast<std::uint32_t>(glyphIndex), px,
		                    static_cast<std::uint8_t>(HintingMode::Normal) };

		if (glyphIndex != 0)
		{
			const auto cached = advanceCache.find(key);
			if (cached != advanceCache.end())
			{
				if (cached->second == kMissingAdvance)
				{
					return false;   // 已知缺字形 ⇒ 跳过（与原 `continue` 行为一致）
				}
				out = cached->second;
				return true;
			}
		}

		const bool loaded =
			FT_Load_Char(face, static_cast<FT_ULong>(cp),
			             FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP) == 0;
		if (glyphIndex != 0)
		{
			++advanceMemoMissCount;   // 观测缝（T28-1 真命中断言——成败均计）
			if (advanceCache.size() >= kMaxAdvanceCache)
			{
				advanceCache.clear();   // ★ 上限 ⇒ 清空（O(1)，防无界增长——O9 口径）
			}
			advanceCache.emplace(key, loaded ? static_cast<long>(face->glyph->advance.x >> 6)
			                                 : kMissingAdvance);
		}
		if (!loaded)
		{
			return false;   // 缺字形 ⇒ 跳过（advance 不计）——与既有行为逐位一致
		}
		out = static_cast<long>(face->glyph->advance.x >> 6);
		return true;
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

FontEngine::Impl::FaceEntry* FontEngine::Impl::EnsureFace(const std::string& path, long index)
{
	const auto key = std::make_pair(path, static_cast<int>(index));
	const auto it = faces.find(key);
	if (it != faces.end())
	{
		return &it->second;   // 命中 ⇒ 复用（legacy「同一文件只加载一次」语义保持）
	}
	FT_Face face = nullptr;
	if (FT_New_Face(library, path.c_str(), static_cast<FT_ULong>(index), &face) != 0 || face == nullptr)
	{
		Logger::Log(LogLevel::Warning, L"FontEngine: FT_New_Face failed for resolved font file");
		return nullptr;
	}
	FaceEntry e;
	e.id = nextFaceId++;   // ★ 单调递增、永不复用（既有不变量）
	e.face = face;
	e.numFaces = face->num_faces > 0 ? face->num_faces : 1;
	e.nameId1En = ReadNameId1En(face);
	TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
	if (os2 != nullptr)
	{
		e.os2Weight = static_cast<std::uint16_t>(os2->usWeightClass);
		e.italic = (os2->fsSelection & 0x0001) != 0;   // bit0 = ITALIC（consolai=1 / consolaz=33 实测）
	}
	else
	{
		Logger::Log(LogLevel::Warning, L"FontEngine: OS/2 table missing; assuming weight 400");   // 盯防⑨
	}
	facesById.emplace(e.id, face);
	return &faces.emplace(key, std::move(e)).first->second;
}

FaceId FontEngine::FaceIdFor(const std::string& family, std::uint16_t weight)
{
	Impl& impl = *m_impl;
	if (impl.library == nullptr || !impl.source)
	{
		return 0;
	}

	// ★ Phase 36 批二：familyCache 键 = (family, weight)——**weight 原值入键**（C36-1：
	//   clamp 仅作用于归并 / lfWeight 消费；键存原值 ⇒ 用户改回合法值不残留污染）
	const auto famKey = std::make_pair(family, weight);
	const auto famCached = impl.familyCache.find(famKey);
	if (famCached != impl.familyCache.end())
	{
		return famCached->second;   // ★ v1.1 语义保持：**含失败/回退结果**——T26-13 防刷屏锚
	}

	// 回退：默认 face（legacy 语义——告警由调用分支负责，此处不重复打）
	const auto fallbackToDefault = [&]() -> FaceId {
		const std::string path = impl.source->ResolveFile(std::string());
		if (path.empty())
		{
			impl.familyCache.emplace(famKey, 0);   // 失败结果也缓存（防每帧重试）
			return 0;
		}
		// ★ v1.5 语义保持：默认 face 通常**早已加载** ⇒ faces 命中即复用——否则每个新 key
		//   都会重新探测 + 重新告警（实机刷屏；T26-13 回归锚点）
		Impl::FaceEntry* entry = impl.EnsureFace(path, 0);
		const FaceId id = entry ? entry->id : 0;
		impl.familyCache.emplace(famKey, id);
		return id;
	};

	// ① 空 family ⇒ legacy 默认路径**原样**（不进候选机制——A1 红线）
	if (family.empty())
	{
		return fallbackToDefault();
	}

	// ② 候选（详设 §2.2：文件直查**首位** + 注册表逐项；档位由 face 自身 OS/2 提供）
	const std::vector<FontSourceCandidate> candidates = impl.source->ResolveFileCandidates(family);
	if (candidates.empty())
	{
		// ★ **不静默降级**：明确告警，再回退默认 face（详设 △2 / 外部评审 §11）
		Logger::Log(LogLevel::Warning,
		            L"FontEngine: font family not found; falling back to the default face: "
		            + UTF8ToWide(family));
		return fallbackToDefault();
	}

	// ③ 按路径分组（**保持候选序**——归并平手取下标小者 = 直查优先序，评审 P0-①）
	struct PathGroup
	{
		std::string path;
		bool direct = false;      // 任一直查候选命中该路径 ⇒ 整路径**豁免族名过滤**
		std::vector<long> hints;  // 扫描起点序（hint 优先、升序去重；其余 face 升序殿后）
	};
	std::vector<PathGroup> groups;
	std::map<std::string, std::size_t> groupOf;
	for (const auto& cand : candidates)
	{
		const auto it = groupOf.find(cand.filePath);
		if (it == groupOf.end())
		{
			groupOf.emplace(cand.filePath, groups.size());
			groups.push_back({cand.filePath, cand.directFile, {cand.faceIndexHint}});
		}
		else
		{
			PathGroup& g = groups[it->second];
			g.direct = g.direct || cand.directFile;
			g.hints.push_back(cand.faceIndexHint);
		}
	}

	// ④⑤ 扫描 + 过滤（nameID-1 直读 ∧ 非斜体；**直查路径豁免**——P0-①实施期勘误）
	std::vector<std::uint16_t> tiers;
	std::vector<std::pair<std::size_t, long>> tierLoc;
	for (std::size_t gi = 0; gi < groups.size(); ++gi)
	{
		PathGroup& g = groups[gi];
		Impl::FaceEntry* f0 = impl.EnsureFace(g.path, 0);
		if (f0 == nullptr)
		{
			continue;   // 打不开 ⇒ 整路径出局（告警在 EnsureFace）
		}
		const long numFaces = f0->numFaces;
		std::vector<long> order;   // hint 优先（升序去重）→ 其余 face 升序
		for (long h : g.hints)
		{
			if (h >= 0 && h < numFaces)
			{
				order.push_back(h);
			}
		}
		std::sort(order.begin(), order.end());
		order.erase(std::unique(order.begin(), order.end()), order.end());
		for (long i = 0; i < numFaces; ++i)
		{
			if (std::find(order.begin(), order.end(), i) == order.end())
			{
				order.push_back(i);
			}
		}
		for (long idx : order)
		{
			Impl::FaceEntry* e = impl.EnsureFace(g.path, idx);
			if (e == nullptr)
			{
				continue;
			}
			if (!g.direct && (e->nameId1En != family || e->italic))
			{
				continue;   // 族名不匹配 / 斜体面（consolaz 实测能穿族名过滤）⇒ 出局
			}
			tiers.push_back(e->os2Weight);
			tierLoc.emplace_back(gi, idx);
		}
	}

	// ⑥ 全灭 ⇒ 告警 + 回退（不静默——ZH 族名等非目标 3 边界在此落地）
	if (tiers.empty())
	{
		Logger::Log(LogLevel::Warning,
		            L"FontEngine: font family not found; falling back to the default face: "
		            + UTF8ToWide(family));
		return fallbackToDefault();
	}

	// ⑥ 归并：最近字面、平手取轻（C36-2；ClampWeight = 消费边界 C36-1）
	const std::size_t pick = PickNearestTier(tiers, ClampWeight(weight));
	const std::size_t gi = tierLoc[pick].first;
	const long idx = tierLoc[pick].second;
	Impl::FaceEntry* chosen = impl.EnsureFace(groups[gi].path, idx);
	const FaceId id = chosen ? chosen->id : 0;
	impl.familyCache.emplace(famKey, id);
	return id;
}

std::uint32_t FontEngine::GlyphIndex(const Font& font, char32_t cp)
{
	FT_Face face = m_impl->FaceOf(FaceIdFor(font.family, font.weight));
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

std::size_t FontEngine::AdvanceMemoMissCount() const noexcept
{
	return m_impl->advanceMemoMissCount;
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
	const auto key = std::make_tuple(font.weight, text, font.size, font.family, dpi);   // Phase 36 批二 K8-⑤：键含 weight
	const auto cached = impl.measureCache.find(key);
	if (cached != impl.measureCache.end())
	{
		return cached->second;
	}
	++impl.measureMissCount;

	Size result{};
	const FaceId faceId = FaceIdFor(font.family, font.weight);
	FT_Face face = impl.FaceOf(faceId);
	if (face != nullptr)
	{
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

		const std::vector<char32_t> codepoints = DecodeUtf8(text);
		long advancePx = 0;
		for (const char32_t cp : codepoints)
		{
			// ★ Phase 28 批一（C28-1/2/4）：per-glyph advance memo——★ v1.1 实施勘误：
			//   循环体已**提取为 `Impl::AdvanceOf`**（Phase 29 △3）——`FitText` 与本方法必须走
			//   **同一路径**（同 GlyphKey / 同 load flags / 同哨兵语义 / 同求和顺序），
			//   否则「测量宽」与「断行宽」失配。批一语义**逐条保留**（memo 命中零 FT load、
			//   gid=0 永不入 memo、上限清空、缺字形跳过）。
			long advance = 0;
			if (!impl.AdvanceOf(face, faceId, px, cp, advance))
			{
				continue;   // 缺字形 ⇒ 跳过（advance 不计）——与既有行为逐位一致
			}
			advancePx += advance;   // 求和顺序不变（C28-4）
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

TextFit FontEngine::FitText(const Font& font, const std::string& text,
                           std::size_t startCp, float maxWidth)
{
	Impl& impl = *m_impl;
	const std::size_t totalCp = ByteOffsetToCodepointIndex(text, text.size());
	if (startCp >= totalCp)
	{
		return TextFit{ 0, 0.0f };   // 窗口在串尾之后（含空串）
	}

	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);
	const FaceId faceId = FaceIdFor(font.family, font.weight);
	FT_Face face = impl.FaceOf(faceId);
	if (face == nullptr)
	{
		// ★ 无 face ⇒ 全部码点「装得下」（不排版 ⇒ 消费全串）——与 `MeasureText` 返
		//   `{0,0}` 后由布局引擎自行收敛同族；★ **不得**返回 0（否则无字体环境完全不能断行）。
		return TextFit{ totalCp - startCp, 0.0f };
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

	// ★ O(n) 单趟扫描（★ 非二分：memo 命中下每码点 O(log n)，二分反而多跑 log n 趟
	//   且**无法**保证「第一次超宽就停」——线性扫描天然「只扫到断点」）。
	const std::vector<char32_t> codepoints = DecodeUtf8(text);
	const float limit = (std::max)(0.0f, maxWidth);
	const float toDip = 96.0f / static_cast<float>(dpi);
	long advancePx = 0;
	std::size_t fitCp = 0;
	for (std::size_t i = startCp; i < codepoints.size(); ++i)
	{
		long advance = 0;
		if (!impl.AdvanceOf(face, faceId, px, codepoints[i], advance))
		{
			// ★ 缺字形 ⇒ advance 不计但**码点仍被消费**（与 `MeasureText` 求和逐位一致的必要条件：
			//   若此处 return，缺字形文本会在断行时「少消费一个字」⇒ 与测宽口径分裂）。
			++fitCp;
			continue;
		}
		const long nextPx = advancePx + advance;
		if (static_cast<float>(nextPx) * toDip > limit)
		{
			break;   // ★ 超宽 ⇒ 停在**本码点之前**（严格 >：恰好等于 maxWidth 仍算放得下）
		}
		advancePx = nextPx;
		++fitCp;
	}

	// ★ 恒至少消费 0 个；「首个码点就超宽」⇒ fitCp = 0（调用方据此按字硬断——D29-Ⅶ 退化语义）。
	//   width = **实际消费段**宽（缺字形码点贡献 0——与 `MeasureText` 同口径）。
	return TextFit{ fitCp, static_cast<float>(advancePx) * toDip };
}

TextFit FontEngine::FitText(const Font& font, const std::string& text,
                           std::size_t startCp, float maxWidth,
                           const std::vector<char32_t>& cps,
                           const std::vector<std::uint32_t>& byteOffsets)
{
	// ★ Phase 40：表感知重载。与 4 参的唯一差别 = **不再 `DecodeUtf8(text)` 整串解码**
	//   （4 参 `:529` 那次：O(N) + 一个 `reserve(3N)` 的 vector ⇒ L 行 = L 次 × 240 KB churn）。
	(void)text;   // 不读（码点由 cps 给出；保留形参只为与 4 参 / 其他实现签名统一）

	Impl& impl = *m_impl;
	if (byteOffsets.size() < 2)
	{
		return TextFit{ 0, 0.0f };   // 空 / 单元素表 ⇒ 码点数为 0（防 size()-1 下溢）
	}
	const std::size_t totalCp = byteOffsets.size() - 1;   // ★★ 不是 cps.size()（见头文件注释 / M3）
	if (startCp >= totalCp)
	{
		return TextFit{ 0, 0.0f };   // 窗口在串尾之后（含空串）
	}

	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);
	const FaceId faceId = FaceIdFor(font.family, font.weight);
	FT_Face face = impl.FaceOf(faceId);
	if (face == nullptr)
	{
		// ★ 无 face ⇒ 全部码点「装得下」（消费全串）——同 4 参 `:523`
		return TextFit{ totalCp - startCp, 0.0f };
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

	const float limit = (std::max)(0.0f, maxWidth);
	const float toDip = 96.0f / static_cast<float>(dpi);
	long advancePx = 0;
	std::size_t fitCp = 0;
	// ★★ 循环上界是 `cps.size()`、起点 `startCp` 是 `SequenceLength` 空间的索引 ——
	//    这是**既有分歧**（4 参同形），**原样保留、不得顺手修**（修了就改脏字节行为）
	for (std::size_t i = startCp; i < cps.size(); ++i)
	{
		long advance = 0;
		if (!impl.AdvanceOf(face, faceId, px, cps[i], advance))
		{
			// ★ 缺字形 ⇒ advance 不计但**码点仍被消费**（同 4 参 `:541`——与 MeasureText 求和一致的必要条件）
			++fitCp;
			continue;
		}
		const long nextPx = advancePx + advance;
		if (static_cast<float>(nextPx) * toDip > limit)
		{
			break;   // ★ 超宽 ⇒ 停在**本码点之前**（严格 >：恰好等于 maxWidth 仍算放得下）
		}
		advancePx = nextPx;
		++fitCp;
	}

	return TextFit{ fitCp, static_cast<float>(advancePx) * toDip };
}

float FontEngine::LineHeight(const Font& font){
	Impl& impl = *m_impl;
	const int dpi = (impl.dpi > 0) ? impl.dpi : 96;
	const int px = PixelSize(font);

	const auto key = std::make_tuple(font.weight, font.size, font.family, dpi);   // Phase 36 批二 K8-⑥：键含 weight
	const auto cached = impl.lineHeightCache.find(key);
	if (cached != impl.lineHeightCache.end())
	{
		return cached->second;
	}

	float height = font.size;   // 兜底：字号（**已是 DIP**——与返回值单位一致）
	FT_Face face = impl.FaceOf(FaceIdFor(font.family, font.weight));
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

	const auto cached = impl.ascentCache.find({ FaceIdFor(font.family, font.weight), px });
	if (cached != impl.ascentCache.end())
	{
		return cached->second;
	}

	float ascent = 0.0f;
	FT_Face face = impl.FaceOf(FaceIdFor(font.family, font.weight));
	if (face != nullptr)
	{
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));

		// ★ 与栅格化**同一 face / 同一 pixelSize / 同一转换**（D26-2 唯一路径）——
		//   26.6 定点 ⇒ /64 得物理像素（`metrics.ascender` 向上为正）
		ascent = static_cast<float>(face->size->metrics.ascender) / 64.0f;
	}

	impl.ascentCache.emplace(std::make_pair(FaceIdFor(font.family, font.weight), px), ascent);
	return ascent;
}

bool FontEngine::AdvancePx(const Font& font, char32_t cp, long& out)
{
	// ★ 审计 #62 / H2：`Impl::AdvanceOf` 的**唯一对外出口**——渲染侧（GL）与测量侧
	//   （`MeasureText` / `FitText`）因此走**完全同一条** advance 求值路径
	//   （同 GlyphKey / 同 load flags / 同哨兵语义）⇒「画出来的宽」与「测出来的宽」同源。
	//   ⚠️ **不触碰 memo 策略**：`AdvanceOf` 内 gid==0 仍永不入 memo（`FontEngine.cpp:93-94`
	//      有意为之，审计 M15 要求修 H2 时保持该处不动）——本方法只是把结果外传。
	Impl& impl = *m_impl;
	const int px = PixelSize(font);
	const FaceId faceId = FaceIdFor(font.family, font.weight);
	FT_Face face = impl.FaceOf(faceId);
	if (face == nullptr)
	{
		return false;   // 无可用 face（与 MeasureText 的空结果同族）
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
	return impl.AdvanceOf(face, faceId, px, cp, out);
}

bool FontEngine::Glyph(const Font& font, char32_t cp, GlyphBitmap& out)
{
	// ★ Phase 26 批三：本方法 = 「构建键 → 委派 `GlyphByKey`」（键型缓存单点，避免两处实现漂移）
	const std::uint32_t glyphIndex = GlyphIndex(font, cp);
	if (glyphIndex == 0)
	{
		return false;   // 该 face 无此字形（或 face 无效）
	}

	const GlyphKey key{ FaceIdFor(font.family, font.weight), glyphIndex, PixelSize(font),
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
