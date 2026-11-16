#pragma once

#include "Render/FontEngine.h"   // GlyphKey / GlyphBitmap / FaceId

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace ECDI{

/// @brief 图集槽位 + 绘制所需全部度量（Phase 26 详设 §1.4 **D26-4 定稿**）
/// @details ★ 这是 `DrawText` 的核心数据：槽位几何给出 UV，`bearing/advance` 给出笔位推进。
/// @note `valid == false` **只表示「图集已满、本次分配失败」**（详设 §1.4 语义边界）——
///       **不影响** `advance` 的排版位置（后续字形不会错位）。
struct GlyphSlot{
	int   x = 0, y = 0, w = 0, h = 0;   ///< 图集内的像素矩形（UV 由此推导）
	float bearingX = 0.0f;              ///< FreeType `bitmap_left`（相对笔位）
	float bearingY = 0.0f;              ///< FreeType `bitmap_top`（相对基线，y **向上**）
	float advance  = 0.0f;              ///< 笔位推进（**物理像素**——已由 `pixelSize` 决定）
	int   pixelSize = 0;                ///< 该槽位对应的像素字号（`GlyphKey` 的一部分）
	bool  valid = false;                ///< false = 分配失败（不崩，跳过绘制）
	unsigned int  texture = 0;          ///< Phase 47：所属页纹理（0 = 无效或无头逻辑页）
	std::uint32_t pageIndex = 0;        ///< Phase 47：固定页槽索引 [0, maxPages)
	std::uint32_t epoch = 0;            ///< Phase 47：页代际号（活跃页从 1 起，0 恒无效）
};

/// @brief GPU 字形图集（Phase 26 详设 §1.4 **D26-4** · §3-④ D6）
/// @details
/// ★ **职责边界**：本类只做「**字形位图 → GPU 纹理**」——
///   - **属本类**：shelf 分配 · 覆盖度上传 · `GlyphKey → GlyphSlot` 缓存。
///   - **不属本类**：字形栅格化（归 `FontEngine`，CPU 侧）· 排版语义 · quad 合成（归 `GLRenderer`）。
///   ★ **CPU 缓存与 GPU texture 分离**（详设 §3-⑤）：`FontEngine` **不持** 任何 GL 对象。
///
/// ★ **本头零 `Windows.h` / 零 GL 头**（详设盯防②⑥）——纹理对象用 `unsigned int` 表示，
///   GL 调用**只出现在 `.cpp`**。⇒ `Allocate` 是**纯逻辑**，可在无 GL 环境下直接测试（T26-7）。
///
/// ★ **无淘汰**（详设 §9 O3 保持不做）：图集满 ⇒ 返回 `valid = false` + 告警，**不崩**。
///   重启条件 = 大字体 / 多字号场景图集常满。
/// ★ 非拷贝、非移动（资源类——与 `Window` / `Widget` / `FontEngine` 同族）。
class GLGlyphAtlas{
public:
	static constexpr std::size_t kDefaultMaxPages = 8;
	static constexpr std::size_t kMinMaxPages = 2;

	explicit GLGlyphAtlas(std::size_t maxPages = kDefaultMaxPages);
	~GLGlyphAtlas();

	GLGlyphAtlas(const GLGlyphAtlas&) = delete;
	GLGlyphAtlas& operator=(const GLGlyphAtlas&) = delete;

	/// @brief 图集边长（正方形 · 1024²——沿 spike 实测值）
	static constexpr int kSize = 1024;

	/// @brief 创建 GPU 纹理（★★ **必须在 WGL context current 之后调用**——详设 §3-⑧ 创建顺序不变量）
	void Initialize();

	/// @brief 是否已就绪（`Initialize` 成功）
	bool IsReady() const noexcept { return m_ready; }

	/// @brief 受保护 `Page 0` 的纹理对象（`GLuint`——以 `unsigned int` 表达使本头零 GL 依赖）
	unsigned int Texture() const noexcept { return m_pages.empty() ? 0 : m_pages[0].texture; }

	/// @brief **单页（Page 0）shelf 分配**（★ 保留原有纯逻辑单页满容语义，兼容 T26-7 / T46-8）
	GlyphSlot Allocate(int w, int h);

	/// @brief **多页分页分配**（先试既有页 → 按需开新页至 maxPages → 跨帧 Page-LRU 回收 / 极端帧内就地重置）
	GlyphSlot AllocatePaged(int w, int h);

	/// @brief 帧边界推进（递进帧号并在必要时执行帧边界 Page-LRU 整页释放）
	void BeginFrame() noexcept;

	/// @brief 判断长寿缓存槽位是否仍然有效（校验页号、活跃状态、代际 epoch 与带 GL 环境下的纹理句柄一致）
	bool IsSlotAlive(const GlyphSlot& slot) const noexcept;

	/// @brief 命中既有缓存槽时刷新所属页 LRU 帧号与序
	void TouchSlot(const GlyphSlot& slot) noexcept;

	/// @brief 把 A8 覆盖度上传到已分配槽位所在页（★ 需要 current context）
	void Upload(const GlyphSlot& slot, const std::vector<std::uint8_t>& a8);

	/// @brief `key` → 槽位（命中刷新 LRU 并直接返回；miss 经 `AllocatePaged` 分配上传）
	GlyphSlot GetOrCreate(const GlyphKey& key, FontEngine& engine);

	/// @brief 设置分页池上限（`< 2` 静默钳位至 `2` 并写 Warning；供确定性淘汰/重置测试）
	void SetMaxPagesForTests(std::size_t maxPages);

	/// @brief 累计 miss 次数（★ **观测缝**——命中与未命中的返回值相同，无法凭返回值区分）
	std::size_t MissCount() const noexcept { return m_missCount; }

	/// @brief 所有当前活跃页（`allocated == true`）已用像素高水位总和
	std::size_t UsedPixels() const noexcept { return m_usedPixels; }

	/// @brief 当前活跃（`allocated == true`）页数
	std::size_t PageCount() const noexcept;

	/// @brief 累计跨帧 / 帧边界 Page-LRU 淘汰次数（`glDeleteTextures` 释页）
	std::size_t EvictionCount() const noexcept { return m_evictionCount; }

	/// @brief 累计极端单帧全池溢出就地重置次数（`glFlush` + 就地覆写，不删句柄）
	std::size_t InFrameResetCount() const noexcept { return m_inFrameResetCount; }

	/// @brief 自构建起累计因图集空间不足导致的 `Allocate` / 建页失败次数（不含非法尺寸拒绝；Phase 46 D46-0）
	std::size_t AllocationFailureCount() const noexcept { return m_allocationFailures; }

	/// @brief `Page 0` 单页 `Allocate` 是否曾触达饱和阈值（`m_fullWarned` 语义，兼容 T46-8）
	bool IsFull() const noexcept { return m_fullWarned; }

private:
	struct PageState{
		unsigned int texture = 0;
		int shelfX = 0;
		int shelfY = 0;
		int shelfH = 0;
		std::size_t usedPixels = 0;
		std::uint32_t epoch = 1;
		std::uint64_t lastUsedFrame = 0;
		std::uint64_t lastUsedSeq = 0;
		std::uint64_t lastResetSeq = 0;
		bool allocated = false;
		bool protectedPage = false;
		std::vector<GlyphKey> residentKeys;
	};

	/// @brief 空槽（`valid = false`）
	static GlyphSlot Empty();
	static std::size_t ClampMaxPages(std::size_t maxPages, bool warnOnClamp);

	bool EnsurePageTexture(PageState& page);
	GlyphSlot AllocateOnPage(std::size_t pageIdx, int w, int h, bool recordPage0Full);
	void PurgePageSlots(std::uint32_t pageIdx);
	void EvictPage(std::size_t victimIdx);
	void ResetPageInPlace(std::size_t victimIdx);
	void UpdateTotalUsedPixels() noexcept;

	std::vector<PageState> m_pages;
	std::size_t m_maxPages = kDefaultMaxPages;
	bool m_ready = false;
	bool m_fullWarned = false;           ///< 单页 `Allocate` 满只告警一次（防日志刷屏）
	bool m_pendingBoundaryEvict = false; ///< 极端帧内重置后标记下帧首清收

	std::uint64_t m_currentFrame = 1;
	std::uint64_t m_touchSeq = 0;
	std::uint64_t m_resetSeq = 0;

	std::map<GlyphKey, GlyphSlot> m_slots;   ///< ★ key → 槽位（`std::map` 够用——详设 §3-④）
	std::size_t m_missCount = 0;
	std::size_t m_usedPixels = 0;
	std::size_t m_evictionCount = 0;
	std::size_t m_inFrameResetCount = 0;
	std::size_t m_allocationFailures = 0;    ///< 单页 Allocate 满或页纹理分配失败累计次数
};

}
