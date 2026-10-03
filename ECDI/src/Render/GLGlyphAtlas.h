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
	bool  valid = false;                ///< false = 图集已满 / 分配失败（不崩，跳过绘制）
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
	GLGlyphAtlas() = default;
	~GLGlyphAtlas();

	GLGlyphAtlas(const GLGlyphAtlas&) = delete;
	GLGlyphAtlas& operator=(const GLGlyphAtlas&) = delete;

	/// @brief 图集边长（正方形 · 1024²——沿 spike 实测值）
	static constexpr int kSize = 1024;

	/// @brief 创建 GPU 纹理（★★ **必须在 WGL context current 之后调用**——详设 §3-⑧ 创建顺序不变量）
	void Initialize();

	/// @brief 是否已就绪（`Initialize` 成功）
	bool IsReady() const noexcept { return m_ready; }

	/// @brief 纹理对象（`GLuint`——以 `unsigned int` 表达使本头零 GL 依赖）
	unsigned int Texture() const noexcept { return m_texture; }

	/// @brief **shelf 分配**（★ **纯逻辑、不碰 GL** —— 可无头测试）
	/// @details 单调 bump：行内右推，行溢出 ⇒ 回卷到下一行；**不淘汰**。
	/// @return 槽位（`valid = false` 表示：尺寸非法（<=0 或 > kSize）或**图集已满**）
	GlyphSlot Allocate(int w, int h);

	/// @brief 把 A8 覆盖度上传到已分配槽位（★ 需要 current context）
	/// @param slot 由 `Allocate` 得到（`valid == false` ⇒ 直接返回——**防堆越界**）
	/// @param a8   覆盖度（长度须 == `slot.w * slot.h`，否则拒绝——防越界写）
	void Upload(const GlyphSlot& slot, const std::vector<std::uint8_t>& a8);

	/// @brief `key` → 槽位（★ 详设 §1.4 数据流）
	/// @details **命中** ⇒ 直接返回；**miss** ⇒ `FontEngine::GlyphByKey` 栅格化 → `Allocate`
	///          → `Upload` → 缓存（★ 含失败结果——避免每帧重试，且让 `MissCount` 语义 = 不同键数）。
	///          ★ 需要 current context（miss 路径会 `Upload`）。
	GlyphSlot GetOrCreate(const GlyphKey& key, FontEngine& engine);

	/// @brief 累计 miss 次数（★ **观测缝**——命中与未命中的返回值相同，无法凭返回值区分）
	/// @details 同款计数即详设 §1.5 的基准指标（`atlas miss`）来源——**一处实现、两处消费**。
	std::size_t MissCount() const noexcept { return m_missCount; }

	/// @brief 已用像素上界（诊断——shelf 单调推进的高水位）
	std::size_t UsedPixels() const noexcept { return m_usedPixels; }

private:
	/// @brief 空槽（`valid = false`）
	static GlyphSlot Empty();

	unsigned int m_texture = 0;          ///< GL 纹理（0 = 未创建）
	bool m_ready = false;
	bool m_fullWarned = false;           ///< 图集满只告警一次（防日志刷屏）

	int m_shelfX = 0;                    ///< 当前行已用宽度（含 1px 间隙）
	int m_shelfY = 0;                    ///< 当前行起始 y
	int m_shelfH = 0;                    ///< 当前行高（行内最大字形高）

	std::map<GlyphKey, GlyphSlot> m_slots;   ///< ★ key → 槽位（`std::map` 够用——详设 §3-④）
	std::size_t m_missCount = 0;
	std::size_t m_usedPixels = 0;
};

}
