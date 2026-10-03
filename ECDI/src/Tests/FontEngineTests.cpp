#include "RunAllTests.h"
#include "TestFramework.h"

#include "Platform/Win32/Win32FontSource.h"   // 字体源（平台实现——测试可直接 include 内部件）
#include "Render/FontEngine.h"                // 内部件（Phase 26 批二）

#include <cstdint>
#include <memory>
#include <string>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// Phase 26 批二：FontEngine（FreeType 共享底层）—— T26-1..T26-6
// ══════════════════════════════════════════════════════════════════════
// ★ **纯 CPU、无窗口、无 GL**（DPI 经 SetDpi 直接注入）⇒ 无头可跑。
// ★ 判据说明：`MeasureText` / `LineHeight` 返回值恒为 **DIP** ⇒ ★ **对基准 DPI 近似不敏感**
//   （`width_px ∝ size·dpi/96`，折回 DIP 后与 dpi 无关）——这是 `TextMeasurer` 的既有契约，
//   T26-2 据此断言「近似不变」而非「按比例变」。

namespace{

/// @brief 建一个已注入 Win32 字体源、已设 DPI 的 `FontEngine`
std::unique_ptr<FontEngine> MakeEngine(int dpi)
{
	auto engine = std::make_unique<FontEngine>();
	engine->SetFontSource(std::make_unique<Win32FontSource>());
	engine->SetDpi(dpi);
	return engine;
}

/// @brief 计数替身：空 family ⇒ 预置的**真实**字体文件；其余一律解析失败（空串）
/// @details 供 T26-13 观测 `ResolveFile` 的调用次数——**缓存是否真命中**无法凭返回值判断，
///          只能数「底层探测被调了几次」。
class CountingFontSource final : public FontSource{
public:
	mutable int resolveCalls = 0;
	std::string goodPath;

	std::string ResolveFile(const std::string& family) const override{
		++resolveCalls;
		return family.empty() ? goodPath : std::string();
	}
};

}   // namespace

// ── T26-1：DIP → 物理像素的**唯一转换路径**（D26-2）────────────────────────────
void Test26PixelSize()
{
	auto engine = MakeEngine(96);
	const Font font{ 14.0f, "" };

	EXPECT_EQ(engine->PixelSize(font), 14);           // 96 ⇒ 恒等

	engine->SetDpi(120);
	EXPECT_EQ(engine->PixelSize(font), 18);           // lround(14·120/96) = lround(17.5) = 18
	EXPECT_EQ(engine->PixelSize(Font{ 16.0f, "" }), 20);

	engine->SetDpi(144);
	EXPECT_EQ(engine->PixelSize(font), 21);           // lround(14·144/96) = 21

	engine->SetDpi(0);                                // ★ fail-safe ⇒ 按 96
	EXPECT_EQ(engine->PixelSize(font), 14);

	engine->SetDpi(-7);                               // ★ 负数同样 fail-safe
	EXPECT_EQ(engine->PixelSize(font), 14);

	engine->SetDpi(96);                               // ★ 下限 1（字号 0 / 负不得产出 0）
	EXPECT_EQ(engine->PixelSize(Font{ 0.0f, "" }), 1);
	EXPECT_EQ(engine->PixelSize(Font{ -3.0f, "" }), 1);
}

// ── T26-2：测量稳定性 + DIP 返回值的 DPI 不敏感性 ──────────────────────────────
void Test26MeasureStableAndDpiInsensitive()
{
	auto engine = MakeEngine(96);
	const Font font{ 14.0f, "" };

	const Size a = engine->MeasureText(font, "Hello");
	EXPECT_TRUE(a.width > 0.0f);
	EXPECT_TRUE(a.height > 0.0f);

	const Size b = engine->MeasureText(font, "Hello");
	EXPECT_NEAR(a.width, b.width, 0.001f);            // ★ 同参 ⇒ 逐位一致
	EXPECT_NEAR(a.height, b.height, 0.001f);

	// ★ 换 DPI ⇒ 物理像素变，但**折回 DIP 后近似不变**（契约——见文件头说明）
	engine->SetDpi(144);
	const Size c = engine->MeasureText(font, "Hello");
	const float tolerance = a.width * 0.05f + 1.0f;
	EXPECT_NEAR(c.width, a.width, tolerance);
}

// ── T26-3：glyph bitmap cache 命中（**栅格化链**）────────────────────────────
void Test26GlyphCacheHit()
{
	auto engine = MakeEngine(96);
	const Font font{ 14.0f, "" };

	GlyphBitmap first;
	EXPECT_TRUE(engine->Glyph(font, U'A', first));
	const std::size_t afterFirst = engine->RasterizeCount();

	GlyphBitmap second;
	EXPECT_TRUE(engine->Glyph(font, U'A', second));
	EXPECT_EQ(engine->RasterizeCount(), afterFirst);   // ★ 命中 ⇒ **不重复栅格化**
	EXPECT_EQ(second.width, first.width);
	EXPECT_EQ(second.height, first.height);

	// ★ 换 pixelSize（经 DPI）⇒ 新 key ⇒ 重新栅格化
	engine->SetDpi(144);
	GlyphBitmap third;
	EXPECT_TRUE(engine->Glyph(font, U'A', third));
	EXPECT_EQ(engine->RasterizeCount(), afterFirst + 1);

	// ★ 无此字形 ⇒ false（不增计数）
	GlyphBitmap missing;
	EXPECT_FALSE(engine->Glyph(font, static_cast<char32_t>(0x10FFFF), missing));
	EXPECT_EQ(engine->RasterizeCount(), afterFirst + 1);
}

// ── T26-4：GlyphKey 严格弱序自洽（详设 §1.3 定稿 key）───────────────────────
void Test26GlyphKeyOrdering()
{
	const GlyphKey a{ 1, 36, 14, 0 };
	const GlyphKey b{ 1, 36, 14, 0 };

	EXPECT_FALSE(a < b);                              // ★ 全同 ⇒ 互不小于（等价）
	EXPECT_FALSE(b < a);

	const GlyphKey bySize{ 1, 36, 21, 0 };            // pixelSize 不同（★ DPI 经此入 key）
	EXPECT_TRUE(a < bySize);
	EXPECT_FALSE(bySize < a);

	const GlyphKey byFace{ 2, 36, 14, 0 };            // faceId 不同
	EXPECT_TRUE(a < byFace);

	const GlyphKey byGlyph{ 1, 37, 14, 0 };           // glyphIndex 不同
	EXPECT_TRUE(a < byGlyph);
}

// ── T26-5：metrics cache 命中 + **DPI 变化 ⇒ 缓存失效**（契约 C-7）────────────
void Test26MeasureCacheAndDpiInvalidation()
{
	auto engine = MakeEngine(120);
	const Font font{ 14.0f, "" };

	const std::size_t base = engine->MeasureTextCacheMissCount();
	EXPECT_EQ(base, static_cast<std::size_t>(0));     // 新建 ⇒ 零未命中

	const Size first = engine->MeasureText(font, "Cache-Probe");
	EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 1);

	const Size second = engine->MeasureText(font, "Cache-Probe");
	EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 1);   // ★ 命中（键含 text+size+family+dpi）
	EXPECT_NEAR(second.width, first.width, 0.001f);

	engine->MeasureText(font, "Other");                          // 换 text ⇒ miss
	EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 2);

	// ★ 换 DPI ⇒ **新 key ⇒ 未命中**（跨屏自动失效——契约 C-7）
	const Size beforeDpi = engine->MeasureText(font, "Cache-Probe");
	engine->SetDpi(144);
	const Size afterDpi = engine->MeasureText(font, "Cache-Probe");
	EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 3);
	EXPECT_NEAR(afterDpi.width, beforeDpi.width, beforeDpi.width * 0.05f + 1.0f);   // DIP ⇒ 近似不变
}

// ── T26-6：FontSource 解析语义（含「找不到 ⇒ 空串」的**不静默降级**契约）──────
void Test26FontSourceResolve()
{
	const Win32FontSource source;

	// ① 空 family ⇒ 默认 face（系统必有可用字体）
	const std::string fallback = source.ResolveFile(std::string());
	EXPECT_TRUE(!fallback.empty());

	// ② 不存在的名字 ⇒ **空串**（★ 由 FontEngine 回退默认 + **告警日志**，不静默降级）
	EXPECT_TRUE(source.ResolveFile("__ecdi_no_such_font__.ttf").empty());

	// ③ 默认 face 的**文件名**应可直查（本 Phase 的解析 = family 视作文件名）
	const std::size_t slash = fallback.find_last_of("\\/");
	if (slash != std::string::npos)
	{
		const std::string baseName = fallback.substr(slash + 1);
		EXPECT_EQ(source.ResolveFile(baseName), fallback);
	}
}

// ── T26-13：解析不到的 family **只探测一次**（★ v1.5 缺陷的回归锚点）──────────
// 背景：`FaceIdFor` 的 family 缓存原先在「回退到**已加载**的默认 face」这条路径上
//       **提前 return 而漏写 `familyCache`** ⇒ 该 family 永不入缓存 ⇒ 每个**新**的
//       `(text, size, family, dpi)` 都重新 `ResolveFile` + 重新告警（实机刷屏）。
// 判据：用**计数替身**观测 `ResolveFile` 调用次数 ⇒ 第二次查询必须**零新增**。
void Test26UnresolvedFamilyProbedOnce()
{
	// 真字体文件路径取自平台实现（前提同 T26-1..T26-6：本机需有可用系统字体）
	const std::string good = Win32FontSource{}.ResolveFile(std::string());
	if (good.empty())
	{
		return;   // 无系统字体 ⇒ 跳过（不制造假绿）
	}

	auto engine = std::make_unique<FontEngine>();
	engine->SetDpi(96);

	CountingFontSource* source = nullptr;
	{
		auto owned = std::make_unique<CountingFontSource>();
		owned->goodPath = good;
		source = owned.get();
		engine->SetFontSource(std::move(owned));
	}

	// ① ★ 关键前置：**先**加载默认 face。否则下面的回退会走「新建 face」分支（③/④ 本来
	//    就会写 familyCache），**掩盖**本缺陷。
	const FaceId def = engine->FaceIdFor(std::string());
	EXPECT_TRUE(def != 0);
	const int afterDefault = source->resolveCalls;          // 空 family ⇒ 解析 1 次

	// ② 首次查一个解析不到的 family ⇒ 探测 2 次（失败 + 回退默认），并回退到**默认 face**
	const FaceId bad = engine->FaceIdFor("__ecdi_no_such_family__.ttf");
	EXPECT_EQ(bad, def);                                    // ★ 回退到默认 face（且已告警，不静默换字体）
	EXPECT_EQ(source->resolveCalls, afterDefault + 2);

	// ③ ★★ 再查同一 family ⇒ **命中 familyCache** ⇒ **零新增探测**（修复前此处为 +4）
	const FaceId again = engine->FaceIdFor("__ecdi_no_such_family__.ttf");
	EXPECT_EQ(source->resolveCalls, afterDefault + 2);
	EXPECT_EQ(again, def);
}

void ECDI::Test::RegisterFontEngineTests()
{
	GetTestRegistry().Add("FontEngine.PixelSize",          &Test26PixelSize);                      // T26-1
	GetTestRegistry().Add("FontEngine.MeasureStableAndDpi", &Test26MeasureStableAndDpiInsensitive); // T26-2
	GetTestRegistry().Add("FontEngine.GlyphCacheHit",      &Test26GlyphCacheHit);                  // T26-3
	GetTestRegistry().Add("FontEngine.GlyphKeyOrdering",   &Test26GlyphKeyOrdering);               // T26-4
	GetTestRegistry().Add("FontEngine.MeasureCacheAndDpi", &Test26MeasureCacheAndDpiInvalidation); // T26-5
	GetTestRegistry().Add("FontEngine.FontSourceResolve",  &Test26FontSourceResolve);              // T26-6
	GetTestRegistry().Add("FontEngine.UnresolvedFamilyProbedOnce", &Test26UnresolvedFamilyProbedOnce); // T26-13
}
