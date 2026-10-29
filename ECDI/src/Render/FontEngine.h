#pragma once

#include "ECDI/Core/Font.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Render/TextMeasurer.h"   // ★ Phase 29 △3：TextFit（契约类型同源于公共头）
#include "Render/FontSource.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ECDI{

/// @brief face 稳定身份（★ 内部件类型——单调递增、**永不复用**）
/// @details ★ **不得用 `FT_Face*` 当稳定 key**——face 释放 / 重载后地址会变
///          （Phase 26 详设 §1.1 · D26-1）。`FT_Face` 由本类持有，**从不外泄**。
using FaceId = int;

/// @brief rasterization policy 取值（本 Phase **单值**——详设 §1.3 ③）
enum class HintingMode : std::uint8_t{
	Normal = 0,   ///< `FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL` + `FT_RENDER_MODE_NORMAL`
};

/// @brief 字形缓存键（Phase 26 详设 §1.3 **定稿**——每一维都落到具体值）
/// @details 三处精确化：① 用 **`pixelSize`** 代 `size + dpi`（★ **DPI 经此入 key**：
///          同 size 不同 dpi ⇒ 不同 px ⇒ 不同 key）；② 用 **`glyphIndex`** 代 codepoint
///          （多码点可映射同一 glyph）；③ `hinting` 是**具体 policy**，不是概念词。
struct GlyphKey{
	FaceId faceId = 0;              ///< 稳定 face 身份（**不是 `FT_Face*`**）
	std::uint32_t glyphIndex = 0;   ///< `FT_Get_Char_Index` 的真实栅格化单位（≠ codepoint）
	int pixelSize = 0;              ///< `FontEngine::PixelSize` —— ★ DPI 入 key 的落点
	std::uint8_t hinting = 0;       ///< `HintingMode`（本 Phase 恒 `Normal`）

	bool operator<(const GlyphKey& other) const noexcept{
		if (faceId != other.faceId)         return faceId < other.faceId;
		if (glyphIndex != other.glyphIndex) return glyphIndex < other.glyphIndex;
		if (pixelSize != other.pixelSize)   return pixelSize < other.pixelSize;
		return hinting < other.hinting;
	}
};

/// @brief 字形位图（8-bit A8 覆盖度）——★ 内部件值类型（**不碰 GL**）
struct GlyphBitmap{
	int width = 0;
	int height = 0;
	float bearingX = 0.0f;   ///< FreeType `bitmap_left`（相对笔位）
	float bearingY = 0.0f;   ///< FreeType `bitmap_top`（相对基线，y **向上**）
	float advance = 0.0f;    ///< 笔位推进（**物理像素**——已由 `pixelSize` 决定）
	std::vector<std::uint8_t> a8;   ///< 覆盖度（`FT_RENDER_MODE_NORMAL` 的灰度）
};

/// @brief FreeType 共享底层（Phase 26 详设 §3-③ 三层拆分的最底层）
/// @details
/// ★ **边界（冻结）**——
///   - **属本类**：FT 库生命周期 · face 加载/缓存 · glyph metrics · 栅格化（A8）
///     · **CPU 两级缓存**（metrics cache + glyph bitmap cache）。
///   - **不属本类**：GPU 资源 · **shaping / 排版语义** · 窗口与 DPI 的持有
///     （DPI 经 `SetDpi` 注入）。
/// ★ **`FT_*` 只出现在本类的 `.cpp`**——公共头与框架其余部分零 FreeType
///   （详设盯防①）；本类**从不返回 `FT_Face`**（缓存键只用 `FaceId`）。
/// ★ 非拷贝、非移动（资源类——与 `Window` / `Widget` 同族）。
class FontEngine{
public:
	FontEngine();
	~FontEngine();

	FontEngine(const FontEngine&) = delete;
	FontEngine& operator=(const FontEngine&) = delete;

	/// @brief 字体源注入（平台层提供——`FontEngine` 因而不知道系统字体目录在哪）
	void SetFontSource(std::unique_ptr<FontSource> source);

	/// @brief 测量基准 DPI（★ D26-2：唯一 DIP→px 基准；`<= 0` ⇒ 按 96）
	void SetDpi(int dpi);

	/// @brief DIP 字号 → 物理像素（★★ Phase 26 **唯一转换路径**——测量与渲染共用）
	/// @details 与既有 GDI 两条链**同一公式**（`lround(size·dpi/96)`，**float 口径**）。
	///          ★ **不得**改用 `DpiConversion::DipToPixels`——那是几何链路的 **int** 口径，
	///          `DpiConversion.h:21-22` 明写「两条链路**不得合并**」（`Font::size` 是 float）。
	/// @return 像素高度（**>= 1**）
	int PixelSize(const Font& font) const;

	/// @brief family（+ 字重）→ 稳定 `FaceId`（★ D26-1；**不返回 `FT_Face`**）
	/// @details ★ Phase 36 批二（三层规则，详设 §2.3）：候选（`ResolveFileCandidates` =
	///          文件直查 + 注册表逐项；**直查候选豁免族名过滤**——`directFile`）→
	///          **nameID-1 直读过滤 ∧ 非斜体**（`family_name` 偏好 nameID-16——探针 3 证伪）
	///          → **OS/2.usWeightClass 档位**最近平手取轻（不跨族）。
	///          命中 family 缓存即返回；未命中 ⇒ 候选扫描 → 加载 → 分配**新** id。
	///          ★ 解析不到 ⇒ **回退默认 face + 写告警日志**（**不静默降级**）。
	///          ★ ZH 族名不解析（需求非目标 3——注册表键名无中文项）⇒ 回退默认 face + 告警。
	/// @param weight 0–1000（C36-1）；**默认 400 = 既有行为**（单参调用点零改动编译；
	///        ★ 有 `Font` 在手的内部调用点必须显式传 `font.weight`）
	/// @return 有效 `FaceId`；**无可用 face ⇒ 0**（0 恒为无效——id 从 1 起分配）
	FaceId FaceIdFor(const std::string& family, std::uint16_t weight = 400);

	/// @brief codepoint → glyph index（★ 修正：调用方经此取，**不直调 `FT_Get_Char_Index`**）
	/// @details 内部 = `FT_Get_Char_Index` ⇒ ★ **`FT_*` 仍只出现在本类 `.cpp`**（契约 C-4）。
	/// @return glyph index；`0` = 该 face 无此字形
	std::uint32_t GlyphIndex(const Font& font, char32_t cp);

	/// @brief 测量文本尺寸——★ **单位恒为 DIP**（`TextMeasurer` 契约）
	/// @details ★ **带 metrics 缓存**（键含 text + size + family + **dpi**）——
	///          消掉「每帧 × 每行」的重复度量（性能方向②）。
	std::size_t MeasureTextCacheMissCount() const noexcept;

	/// @brief 单码点 advance（★ 审计 #62 / H2 的修复接缝）——**物理像素**，与测量链同源
	/// @param font 字体描述符
	/// @param cp   码点
	/// @param out  该码点 advance（**物理像素**，`advance.x >> 6`）
	/// @return false = 该 face 无此字形 / 加载失败（`out` 不变）
	/// @details ★★ **为什么需要它**：H2 的根因是「**测量链推进、绘制链不推进**」——
	///          `GLRenderer::DrawText` 见 `glyphIndex == 0` 就 `continue`（笔位不动），
	///          而 `Impl::AdvanceOf` 对 gid==0 **仍真加载 `.notdef`** 并返回其**非零** advance
	///          ⇒ 绘制比测量少推一个 `.notdef` ⇒ 其后字形整体左移、互相重叠。
	///          修复要点 = 让绘制链**走同一条 advance 求值路径**（而不是自己再算一套）。
	///          ★ 本方法是 `Impl::AdvanceOf` 的**唯一对外出口**——它**不改变** memo 策略
	///          （gid==0 仍永不入 memo），只把结果暴露给渲染侧（审计 M15 的硬要求）。
	/// @note **非公共 API**（本头在 `src/Render/`，不进 `include/ECDI`）⇒ 最小 API 面不受影响。
	bool AdvancePx(const Font& font, char32_t cp, long& out);

	/// @brief advance memo miss 计数（Phase 28 批一观测缝——T28-1「真命中」断言）
	/// @details 仅统计「gid ≠ 0 且 advanceCache miss ⇒ 实际 `FT_Load_Char`」的次数（成败均计）；
	///          gid = 0（未映射）恒走现行 `FT_Load_Char` 原路径、不计入（C28-1：未映射行为
	///          与 memo 化前逐位一致）。
	std::size_t AdvanceMemoMissCount() const noexcept;

	Size MeasureText(const Font& font, const std::string& text);

	/// @brief 断行适配（★ Phase 29 △3/D29-Ⅵ：**advance memo 累积**——生产路径）
	/// @details 与 `MeasureText` **共用同一 per-glyph advance 求值路径**（同 GlyphKey / 同 load
	///          flags / 同 memo 哨兵语义）⇒ ★ 同一 `[startCp, startCp+fitCp)` 区间的宽度
	///          **与 `MeasureText` 逐位一致**（C29-1 的宽度口径前提；T29-FIT-3 锚点）。
	///          本方法**不理解**空格 / CJK / 禁则（「断在哪」属布局引擎——盯防①）。
	/// @param startCp 起始**码点**索引（绝对；`>= 码点数` ⇒ 返回 `{0, 0}`）
	/// @param maxWidth 可用宽（DIP，< 0 视作 0；**连一个码点也放不下 ⇒ `{0, 0}`**）
	/// @return `TextFit`：`fitCp` = **相对**可消费码点数；`width` = 消费段宽（DIP）
	/// @note ★ **O(n) 契约仅限本 FT/memo 路径**（C29-9）——每码点至多一次 memo 查询，
	///       **不逐行重扫**（盯防⑧：构建布局时不得对同一区间二次调用本方法）。
	TextFit FitText(const Font& font, const std::string& text,
	                std::size_t startCp, float maxWidth);

	/// @brief 断行适配（★ Phase 40：**表感知重载**——复用入参 `cps`，不再整串 `DecodeUtf8`）
	/// @details 语义与 4 参**完全相同**（同 `AdvanceOf` 路径 / 同缺字形消费语义 / 同求和顺序），
	///          只是把「整串解码」换成「直接用调用方已解好的 `cps`」⇒ **零 `vector` 分配**。
	///          ★★ `totalCp` 取 **`byteOffsets.size() - 1`**（`SequenceLength` 空间），
	///          **不是** `cps.size()`——后者在「前导合法 + 续字节非法」的串上会**大于** `count`
	///          （`"\xE0\x41\x41"` ⇒ cps=2 / count=1），而 `totalCp` 同时喂 `startCp >= totalCp`
	///          守卫与无 face 兜底 ⇒ 取错就改脏字节行为（违反红线②）。
	/// @param cps `DecodeUtf8` 空间的码点（与调用方行切分同源；从这里按 `startCp` 起扫 advance）
	/// @param text ★ **不读**（只为与 4 参 / 其他实现签名统一）
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth,
	                const std::vector<char32_t>& cps,
	                const std::vector<std::uint32_t>& byteOffsets);

	/// @brief 字体行高——★ **单位恒为 DIP**
	float LineHeight(const Font& font);

	/// @brief **行顶 → 基线**的距离 = ascent（★ **物理像素**）
	/// @details ★★ 为什么需要它：框架的 `DrawText` 契约里 **`pos.y` = 字符单元顶边**
	///          （GDI `TextOutW` 默认 `TA_LEFT | TA_TOP`——`GDIBackend::DrawText` 未改对齐），
	///          **不是基线**。任何非 GDI 后端都必须自己下移一个 ascent 才能复现同一语义；
	///          缺了它就是**整行文字上移一个字高**（观感 = 被裁掉上 3/4）。
	/// @note 与 `GlyphBitmap::bearingY` 同族单位（物理像素）；`pixelSize` 由 `PixelSize` 决定。
	/// @return ascent（px，**>= 0**）；无 face ⇒ 0
	float Ascent(const Font& font);

	/// @brief 取字形位图（供 GL 侧栅格化）——★ 返回 **CPU** 位图，不碰 GL
	/// @details miss ⇒ `FT_Set_Pixel_Sizes` + `FT_Load_Glyph` + `FT_Render_Glyph(NORMAL)`。
	/// @return false = 无此字形 / 加载或栅格化失败（调用方仍应推进笔位——详设 §1.4）
	bool Glyph(const Font& font, char32_t cp, GlyphBitmap& out);

	/// @brief 按**缓存键**取字形位图（★ Phase 26 批三：`GLGlyphAtlas` 的 miss 路径入口）
	/// @details 键**完全决定了**要栅格化什么（face + glyphIndex + pixelSize + hinting）
	///          ⇒ 不需要 `Font`。★ `Glyph(font, cp, …)` 即「构建键后委派给本方法」。
	///          ★ 渲染链因此**不必知道 codepoint**（图集只认键——详设 §1.3 ② 的直接兑现）。
	bool GlyphByKey(const GlyphKey& key, GlyphBitmap& out);

	/// @brief 累计栅格化次数（★ **观测缝**——命中与否的返回值相同，无法凭返回值区分）
	std::size_t RasterizeCount() const noexcept;

private:
	struct Impl;                                  // pimpl：把 `FT_*` 收敛在 .cpp（不污染 include 者）
	std::unique_ptr<Impl> m_impl;
};

}
