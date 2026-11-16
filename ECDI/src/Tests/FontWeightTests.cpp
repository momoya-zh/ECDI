#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/Logger.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Platform/Win32/Win32FontSource.h"
#include "Render/FontEngine.h"
#include "Render/FontWeightRule.h"
#include "Render/FreeTypeTextMeasurer.h"
#include "Render/GDITextMeasurer.h"
#include "Render/GLGlyphAtlas.h"
#include "Render/Utf8Decode.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace ECDI;   // 装置与被测类型同域（TextMeasurerTests 同款）

// ════════════════════════════════════════════════════════════════════════
// Phase 36：Font::weight（批一 = 红线批 T36-2；批二 = 解析链 T36-3/4/5/6/8）
// ════════════════════════════════════════════════════════════════════════
// 装置 = TextMeasurerTests 同款 RAII 隐藏窗口（测量基准 DPI 来自窗口）。
// T36-2 判据 = 详设 §5：SimSun（**无 Bold 字面的族**）@700 vs @400 的 GDI **合成加粗**
// 串宽比 ∈ [1.05, 1.10]——探针读数 183/170 = 1.076 的回归带
// （`.workbuddy/spike/p36/p36a2_readout.txt` [1]）。

namespace {

/// @brief 测试窗口 RAII（隐藏窗口即足够——本组不读像素，无需 Show）
struct WeightWindow
{
    HWND hwnd = nullptr;
    ~WeightWindow() { if (hwnd) DestroyWindow(hwnd); }
};

bool CreateWeightWindow(const wchar_t* className, WeightWindow& out)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }
    out.hwnd = CreateWindowExW(0, className, className, WS_POPUP,
                               0, 0, 200, 200, nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(out.hwnd != nullptr);
    return out.hwnd != nullptr;
}

/// @brief 真实解析链装置（Win32FontSource + 系统字体；T36-4/5/6/8 共用）
std::unique_ptr<FontEngine> MakeRealEngine()
{
    auto engine = std::make_unique<FontEngine>();
    engine->SetDpi(96);
    engine->SetFontSource(std::make_unique<Win32FontSource>());
    return engine;
}

/// @brief 计算灰度位图上、下半部像素重心水平偏移（topCx - botCx；对齐 O46-2 探针）
double TopBottomCentroidDx(const GlyphBitmap& bmp)
{
    double twx = 0.0;
    double tw = 0.0;
    double bwx = 0.0;
    double bw = 0.0;
    const int mid = bmp.height / 2;
    for (int r = 0; r < bmp.height; ++r)
    {
        for (int c = 0; c < bmp.width; ++c)
        {
            const std::uint8_t a = bmp.a8[static_cast<std::size_t>(r) * static_cast<std::size_t>(bmp.width)
                                          + static_cast<std::size_t>(c)];
            const double x = static_cast<double>(bmp.bearingX) + static_cast<double>(c) + 0.5;
            if (r < mid)
            {
                twx += x * static_cast<double>(a);
                tw += static_cast<double>(a);
            }
            else
            {
                bwx += x * static_cast<double>(a);
                bw += static_cast<double>(a);
            }
        }
    }
    const double topCx = (tw > 0.0) ? (twx / tw) : 0.0;
    const double botCx = (bw > 0.0) ? (bwx / bw) : 0.0;
    return topCx - botCx;
}

std::uint64_t BitmapInkSum(const GlyphBitmap& bmp)
{
    return std::accumulate(bmp.a8.begin(), bmp.a8.end(), std::uint64_t{0});
}

void ExpectGlyphsBitIdentical(const GlyphBitmap& a, const GlyphBitmap& b)
{
    EXPECT_EQ(a.width, b.width);
    EXPECT_EQ(a.height, b.height);
    EXPECT_EQ(a.bearingX, b.bearingX);
    EXPECT_EQ(a.bearingY, b.bearingY);
    EXPECT_EQ(a.advance, b.advance);
    EXPECT_EQ(a.a8.size(), b.a8.size());
    EXPECT_TRUE(a.a8 == b.a8);
}

bool FontFileExists(const wchar_t* fileName)
{
    wchar_t winDir[MAX_PATH] = {};
    const UINT len = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return false;
    }
    const std::wstring full = std::wstring(winDir) + L"\\Fonts\\" + fileName;
    const DWORD attrs = GetFileAttributesW(full.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}   // namespace

/// T36-2：SimSun 合成加粗串宽回归带（A2 库内载体；P0-③ 后唯一宽度判据）。
/// 默认零回归红线（T36-1）由全量回归 + 既有测试文件零 diff 承担（详设 §3「测试文件 0 改动」）。
static void Test36SimSunSyntheticBoldWidthBand()
{
    WeightWindow w;
    if (!CreateWeightWindow(L"ECDI_T36_Weight", w))
    {
        return;
    }
    GDITextMeasurer measurer;
    measurer.Initialize(Win32RenderContext(w.hwnd));

    const Font regular{.size = 20.0f, .family = "SimSun"};              // 默认 weight = 400（C36-1）
    const Font bold{.size = 20.0f, .family = "SimSun", .weight = 700};  // C7560：weight 声明于 family 后
    EXPECT_TRUE(regular.weight == 400);   // 默认值 = 既有行为（红线）

    const std::string text = "测试文本 test 123";   // 与探针 p36a2 同串（读数 170 / 183）
    const Size w4 = measurer.MeasureText(regular, text);
    const Size w7 = measurer.MeasureText(bold, text);

    EXPECT_TRUE(w7.width > w4.width);   // 方向判据（A2：合成加粗 advance 增大）
    const double ratio = static_cast<double>(w7.width) / static_cast<double>(w4.width);
    EXPECT_TRUE(ratio >= 1.05 && ratio <= 1.10);   // 详设 §5 T36-2 回归带（探针 1.076）
}

/// T36-3（详设 §5）：归并纯函数真值表——零环境；与 GDI 探针 1 全格一致（**验证样本**语义）
static void Test36PickNearestTierTruthTable()
{
    const std::vector<std::uint16_t> two = {400, 700};
    EXPECT_EQ(PickNearestTier(two, 400), std::size_t{0});
    EXPECT_EQ(PickNearestTier(two, 500), std::size_t{0});
    EXPECT_EQ(PickNearestTier(two, 550), std::size_t{0});   // 平手取轻（GDI 550→400 实测同格）
    EXPECT_EQ(PickNearestTier(two, 560), std::size_t{1});
    EXPECT_EQ(PickNearestTier(two, 600), std::size_t{1});
    EXPECT_EQ(PickNearestTier(two, 650), std::size_t{1});
    EXPECT_EQ(PickNearestTier(two, 700), std::size_t{1});
    EXPECT_EQ(PickNearestTier(two, 900), std::size_t{1});
    const std::vector<std::uint16_t> one = {400};
    EXPECT_EQ(PickNearestTier(one, 700), std::size_t{0});   // 单档族恒该档
    const std::vector<std::uint16_t> tie = {300, 400};
    EXPECT_EQ(PickNearestTier(tie, 350), std::size_t{0});   // 平手取轻
    EXPECT_EQ(ClampWeight(65535u), std::uint16_t{1000});    // C36-1 消费边界
    EXPECT_EQ(ClampWeight(1001u), std::uint16_t{1000});
    EXPECT_EQ(ClampWeight(1000u), std::uint16_t{1000});
    EXPECT_EQ(ClampWeight(0u), std::uint16_t{0});
}

/// T36-4：family/weight → face 身份（真注册表；复合第二项 + 真 Bold 档 + NSimSun + ZH 边界钉）
static void Test36FaceIdPerFamilyAndWeight()
{
    auto engine = MakeRealEngine();
    const FaceId yahei = engine->FaceIdFor("Microsoft YaHei");
    const FaceId yaheiUi = engine->FaceIdFor("Microsoft YaHei UI");
    EXPECT_TRUE(yahei != 0);
    EXPECT_TRUE(yaheiUi != 0);
    EXPECT_TRUE(yaheiUi != yahei);   // msyh.ttc face1 ≠ face0（复合键第二项可达）
    const FaceId yaheiB = engine->FaceIdFor("Microsoft YaHei", 700);
    const FaceId yaheiUiB = engine->FaceIdFor("Microsoft YaHei UI", 700);
    EXPECT_TRUE(yaheiB != yahei);      // msyhbd.ttc face0（真 Bold 档，OS/2=700）
    EXPECT_TRUE(yaheiUiB != yaheiB);   // msyhbd.ttc face1
    EXPECT_TRUE(yaheiUiB != yaheiUi);
    const FaceId simsun = engine->FaceIdFor("SimSun");
    const FaceId nsimsun = engine->FaceIdFor("NSimSun");
    EXPECT_TRUE(nsimsun != simsun);    // simsun.ttc face1（NSimSun 可达）
    EXPECT_EQ(engine->FaceIdFor("Microsoft YaHei"), yahei);   // 复查 = familyCache 命中同 id
    // CB-2：Phase 46 批二接通中文族名惰性解析
    const FaceId zhYahei = engine->FaceIdFor("微软雅黑");
    EXPECT_EQ(zhYahei, yahei);
    EXPECT_NE(zhYahei, engine->FaceIdFor(std::string()));
}

/// T36-5：候选全灭 ⇒ 回退默认 face（不静默换字体）+ familyCache 复用（批二键 = (family, weight)）
static void Test36UnresolvedFallsBackAndCaches()
{
    auto engine = MakeRealEngine();
    const FaceId def = engine->FaceIdFor(std::string());
    EXPECT_TRUE(def != 0);
    EXPECT_EQ(engine->FaceIdFor("__ecdi_no_such_family__.ttf"), def);
    EXPECT_EQ(engine->FaceIdFor("__ecdi_no_such_family__.ttf"), def);   // 复用（T26-13 同族语义）
}

/// T36-6：测量缓存键含 weight（K8-⑤）——经既有观测缝 MeasureTextCacheMissCount()
static void Test36MeasureCacheKeyIncludesWeight()
{
    auto engine = MakeRealEngine();
    const Font regular{.size = 20.0f, .family = "Microsoft YaHei UI"};
    const Font bold{.size = 20.0f, .family = "Microsoft YaHei UI", .weight = 700};
    const std::string text = "测试文本 test 123";
    const std::size_t base = engine->MeasureTextCacheMissCount();
    const Size w4 = engine->MeasureText(regular, text);
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 1);        // 首测 miss
    EXPECT_EQ(engine->MeasureText(regular, text).width, w4.width);   // 复测命中（同键）
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 1);
    const Size w7 = engine->MeasureText(bold, text);
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), base + 2);        // ★ 变 weight ⇒ 新键 miss
    EXPECT_TRUE(w7.width > w4.width);   // 真字面（msyhbd face1，OS/2=700）更宽
}

/// T36-8：face 身份判别式——Semibold/Light/Black 独立族被 nameID-1 过滤（评审 P2-⑩：断身份不断宽度）
static void Test36FaceIdentityTierFiltering()
{
    auto engine = MakeRealEngine();
    EXPECT_EQ(engine->FaceIdFor("Segoe UI", 600), engine->FaceIdFor("Segoe UI", 700));
    EXPECT_TRUE(engine->FaceIdFor("Segoe UI", 600) != engine->FaceIdFor("Segoe UI", 400));
    EXPECT_EQ(engine->FaceIdFor("Segoe UI", 300), engine->FaceIdFor("Segoe UI", 400));
    EXPECT_EQ(engine->FaceIdFor("Segoe UI", 900), engine->FaceIdFor("Segoe UI", 700));   // Black 独立族被滤
    EXPECT_TRUE(engine->FaceIdFor("Microsoft YaHei Light") != engine->FaceIdFor("Microsoft YaHei"));   // 独立族直达
    // 直查豁免（P0-①实施期勘误）：显式文件名不被族名过滤误杀
    EXPECT_TRUE(engine->FaceIdFor("consola.ttf") != engine->FaceIdFor(std::string()));
    EXPECT_EQ(engine->FaceIdFor("consola.ttf"), engine->FaceIdFor("Consolas"));   // 同文件 face0 = 同 id
    // 斜体面排除（P1-⑤）：Consolas Bold Italic（nameID1='Consolas'）不得成为 700 档候选
    EXPECT_EQ(engine->FaceIdFor("Consolas", 650), engine->FaceIdFor("Consolas", 700));
}

// ── T45-2：斜体选面（有真斜体面族分叉 / 无真斜体面族回落常规池且不重复告警 / italic=false 逐位不变）──
static void Test45ItalicFaceSelection()
{
    auto engine = MakeRealEngine();

    // 1. 有真斜体面族（Consolas / Segoe UI）：FaceIdFor(f, w, true) != FaceIdFor(f, w, false) 且 != 0
    const FaceId consolasReg = engine->FaceIdFor("Consolas", 400, false);
    const FaceId consolasIta = engine->FaceIdFor("Consolas", 400, true);
    EXPECT_TRUE(consolasReg != 0);
    EXPECT_TRUE(consolasIta != 0);
    EXPECT_TRUE(consolasIta != consolasReg);

    const FaceId segoeReg = engine->FaceIdFor("Segoe UI", 400, false);
    const FaceId segoeIta = engine->FaceIdFor("Segoe UI", 400, true);
    EXPECT_TRUE(segoeReg != 0);
    EXPECT_TRUE(segoeIta != 0);
    EXPECT_TRUE(segoeIta != segoeReg);

    // 2. 无真斜体面族（SimSun）：合成斜切占独立 FaceId（Phase 46 CB-1a：作废 I45-italic-fallback）
    const FaceId simsunReg = engine->FaceIdFor("SimSun", 400, false);
    const FaceId simsunIta = engine->FaceIdFor("SimSun", 400, true);
    EXPECT_TRUE(simsunReg != 0);
    EXPECT_TRUE(simsunIta != 0);
    EXPECT_NE(simsunIta, simsunReg);

    // 3. italic=false 缺省实参与双参旧调用逐位保持一致
    EXPECT_EQ(engine->FaceIdFor("Consolas", 400), consolasReg);
    EXPECT_EQ(engine->FaceIdFor("Segoe UI", 400), segoeReg);

    // 4. 写缓存且不重复告警：再次请求命中同一合成斜体缓存项（Phase 46 CB-1b）
    EXPECT_EQ(engine->FaceIdFor("SimSun", 400, true), simsunIta);
}

// ── T45-7：负对照② 键完备投毒（familyCache / measureCache / lineHeightCache / ascentCache）──
static void Test45KeyCompletenessPoisoning()
{
    auto engine = MakeRealEngine();

    // ① familyCache 完备性：同一族、同字重下，改变 italic 位不会被旧缓存覆盖
    const FaceId fReg = engine->FaceIdFor("Segoe UI", 400, false);
    const FaceId fIta = engine->FaceIdFor("Segoe UI", 400, true);
    EXPECT_TRUE(fReg != fIta);

    // ② measureCache 完备性：Font.italic 改变产生新 key，未命中缓存
    const Font regular{ 20.0f, "Segoe UI", 400, false };
    const Font italic { 20.0f, "Segoe UI", 400, true };
    const std::string text = "ECDI Italic Poison Test";

    const std::size_t baseMiss = engine->MeasureTextCacheMissCount();
    engine->MeasureText(regular, text);
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), baseMiss + 1);

    engine->MeasureText(regular, text);
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), baseMiss + 1); // 命中缓存

    engine->MeasureText(italic, text);
    EXPECT_EQ(engine->MeasureTextCacheMissCount(), baseMiss + 2); // italic=true 必须 miss 并重算

    // ③ Ascent 与 LineHeight 分离投毒断言
    const float aReg = engine->Ascent(regular);
    const float aIta = engine->Ascent(italic);
    EXPECT_TRUE(aReg > 0.0f);
    EXPECT_TRUE(aIta > 0.0f);

    const float lhReg = engine->LineHeight(regular);
    const float lhIta = engine->LineHeight(italic);
    EXPECT_TRUE(lhReg > 0.0f);
    EXPECT_TRUE(lhIta > 0.0f);
}

// ════════════════════════════════════════════════════════════════════════
// Phase 46 Batch 0：GLGlyphAtlas 饱和可观测守卫（T46-8）
// ════════════════════════════════════════════════════════════════════════
static void Test46GlyphAtlasFullObservability()
{
    GLGlyphAtlas atlas;
    EXPECT_EQ(atlas.AllocationFailureCount(), static_cast<std::size_t>(0));
    EXPECT_FALSE(atlas.IsFull());

    // 参数非法拒绝不得误计为图集饱和或失败计数
    EXPECT_FALSE(atlas.Allocate(0, 16).valid);
    EXPECT_FALSE(atlas.Allocate(16, 0).valid);
    EXPECT_FALSE(atlas.Allocate(GLGlyphAtlas::kSize + 1, 16).valid);
    EXPECT_EQ(atlas.AllocationFailureCount(), static_cast<std::size_t>(0));
    EXPECT_FALSE(atlas.IsFull());

    // 填满 1024x1024（每格 255x255 含 1px 间隙占 256 步进，每行 4 格 × 4 行 = 16 格）
    for (int i = 0; i < 16; ++i)
    {
        const GlyphSlot ok = atlas.Allocate(255, 255);
        EXPECT_TRUE(ok.valid);
    }
    EXPECT_EQ(atlas.AllocationFailureCount(), static_cast<std::size_t>(0));
    EXPECT_FALSE(atlas.IsFull());

    // 第 17~19 次溢出必须确定性观测到 valid==false + IsFull()==true + 精确累计失败计数 1..3
    for (std::size_t fail = 1; fail <= 3; ++fail)
    {
        const GlyphSlot overflow = atlas.Allocate(255, 255);
        EXPECT_FALSE(overflow.valid);
        EXPECT_TRUE(atlas.IsFull());
        EXPECT_EQ(atlas.AllocationFailureCount(), fail);
    }
}

// ════════════════════════════════════════════════════════════════════════
// Phase 46 Batch 1：合成粗体 / 斜体核心 + 四态隔离与缓存防投毒（T46-1 / T46-2 / T46-3）
// ════════════════════════════════════════════════════════════════════════
static void Test46SyntheticBoldWidthRatioParity()
{
    WeightWindow w;
    if (!CreateWeightWindow(L"ECDI_T46_BoldParity", w))
    {
        return;
    }
    GDITextMeasurer gdi;
    gdi.Initialize(Win32RenderContext(w.hwnd));

    auto engine = std::shared_ptr<FontEngine>(MakeRealEngine().release());
    FreeTypeTextMeasurer ft(engine);
    ft.Initialize(Win32RenderContext(w.hwnd));

    // ① 550 vs 551 合成边界 + 常规一致
    EXPECT_EQ(engine->FaceIdFor("SimSun", 550, false), engine->FaceIdFor("SimSun", 400, false));
    EXPECT_NE(engine->FaceIdFor("SimSun", 551, false), engine->FaceIdFor("SimSun", 400, false));
    EXPECT_EQ(engine->FaceIdFor("SimSun", 551, false), engine->FaceIdFor("SimSun", 700, false));

    // ② 涵盖测试串的三字族五档 × 双串校准（abs(FT_ratio - GDI_ratio) <= 0.02）
    const std::string cjkText = "测试文本中文";
    const std::string mixText = "测试文本 test 123";
    for (const char* family : { "SimSun", "SimHei", "Microsoft YaHei" })
    {
        for (int px : { 12, 16, 20, 24, 32 })
        {
            const Font reg{ static_cast<float>(px), family, 400, false };
            const Font bold{ static_cast<float>(px), family, 700, false };
            for (const std::string& text : { cjkText, mixText })
            {
                const Size g4 = gdi.MeasureText(reg, text);
                const Size g7 = gdi.MeasureText(bold, text);
                const Size f4 = ft.MeasureText(reg, text);
                const Size f7 = ft.MeasureText(bold, text);
                EXPECT_TRUE(g4.width > 0.0f && f4.width > 0.0f);
                if (std::string(family) != "Microsoft YaHei")
                {
                    EXPECT_TRUE(f7.width > f4.width);   // 合成粗体须 xstr >= 64 整像素加粗，不可被 >> 6 吞宽
                }
                const double gdiRatio = static_cast<double>(g7.width) / static_cast<double>(g4.width);
                const double ftRatio = static_cast<double>(f7.width) / static_cast<double>(f4.width);
                EXPECT_TRUE(std::abs(ftRatio - gdiRatio) <= 0.02);
            }
        }
    }

    // ③ Consolas 真粗体面改前/改后零漂移（直接对照真粗体文件 consolab.ttf）
    auto engine96 = MakeRealEngine();
    const Font consReg{ 20.0f, "Consolas", 400, false };
    const Font consBold{ 20.0f, "Consolas", 700, false };
    const Font consBoldDirect{ 20.0f, "consolab.ttf", 400, false };
    EXPECT_EQ(engine96->FaceIdFor(consBold.family, consBold.weight, consBold.italic),
              engine96->FaceIdFor(consBoldDirect.family, consBoldDirect.weight, consBoldDirect.italic));
    EXPECT_EQ(engine96->MeasureText(consBold, "Consolas 123 W").width,
              engine96->MeasureText(consBoldDirect, "Consolas 123 W").width);
    GlyphBitmap gbFam, gbDir;
    EXPECT_TRUE(engine96->Glyph(consBold, U'W', gbFam));
    EXPECT_TRUE(engine96->Glyph(consBoldDirect, U'W', gbDir));
    ExpectGlyphsBitIdentical(gbFam, gbDir);

    // ④ FreeTypeTextMeasurer::MeasureText 与逐码点 Glyph::advance 之和严格相等
    FreeTypeTextMeasurer ft96(std::shared_ptr<FontEngine>(MakeRealEngine().release()));
    auto sharedEng = MakeRealEngine();
    for (const char* family : { "SimSun", "Microsoft YaHei" })
    {
        const Font f{ 20.0f, family, 700, false };
        float sumAdv = 0.0f;
        for (const char32_t cp : DecodeUtf8(mixText))
        {
            GlyphBitmap bmp;
            EXPECT_TRUE(sharedEng->Glyph(f, cp, bmp));
            sumAdv += bmp.advance;
        }
        EXPECT_NEAR(sharedEng->MeasureText(f, mixText).width, sumAdv, 0.001f);
        EXPECT_TRUE(sharedEng->MeasureText(f, mixText).width > sharedEng->MeasureText(Font{ 20.0f, family, 400, false }, mixText).width);
    }
    EXPECT_TRUE(consReg.weight == 400);
}

static void Test46SyntheticObliqueSlantGeometry()
{
    auto engine = MakeRealEngine();
    const std::vector<char32_t> fullSet = { U'测', U'国', U'H', U'W' };
    const std::vector<char32_t> latinSet = { U'H', U'W', U'A', U'I' };

    for (const char* family : { "SimSun", "Microsoft YaHei" })
    {
        const Font reg{ 20.0f, family, 400, false };
        const Font obl{ 20.0f, family, 400, true };
        for (const char32_t cp : fullSet)
        {
            GlyphBitmap gReg, gObl;
            EXPECT_TRUE(engine->Glyph(reg, cp, gReg));
            EXPECT_TRUE(engine->Glyph(obl, cp, gObl));
            EXPECT_TRUE(std::abs(TopBottomCentroidDx(gReg)) <= 0.4);
            EXPECT_TRUE(TopBottomCentroidDx(gObl) >= 1.2);
        }
        EXPECT_EQ(engine->MeasureText(obl, "测试文本 test H W").width,
                  engine->MeasureText(reg, "测试文本 test H W").width);
        EXPECT_EQ(engine->LineHeight(obl), engine->LineHeight(reg));
        EXPECT_EQ(engine->Ascent(obl), engine->Ascent(reg));
    }

    const Font tahomaReg{ 20.0f, "Tahoma", 400, false };
    const Font tahomaObl{ 20.0f, "Tahoma", 400, true };
    for (const char32_t cp : latinSet)
    {
        GlyphBitmap gReg, gObl;
        EXPECT_TRUE(engine->Glyph(tahomaReg, cp, gReg));
        EXPECT_TRUE(engine->Glyph(tahomaObl, cp, gObl));
        EXPECT_TRUE(std::abs(TopBottomCentroidDx(gReg)) <= 0.4);
        EXPECT_TRUE(TopBottomCentroidDx(gObl) >= 1.2);
    }
    EXPECT_EQ(engine->MeasureText(tahomaObl, "H W A I test").width,
              engine->MeasureText(tahomaReg, "H W A I test").width);

    // 真斜体族（Segoe UI / Consolas）不二次斜切：与直接加载真斜体文件逐位一致
    GlyphBitmap segoeFam, segoeDir, consFam, consDir;
    EXPECT_TRUE(engine->Glyph(Font{ 20.0f, "Segoe UI", 400, true }, U'H', segoeFam));
    EXPECT_TRUE(engine->Glyph(Font{ 20.0f, "segoeuii.ttf", 400, false }, U'H', segoeDir));
    ExpectGlyphsBitIdentical(segoeFam, segoeDir);

    EXPECT_TRUE(engine->Glyph(Font{ 20.0f, "Consolas", 400, true }, U'H', consFam));
    EXPECT_TRUE(engine->Glyph(Font{ 20.0f, "consolai.ttf", 400, false }, U'H', consDir));
    ExpectGlyphsBitIdentical(consFam, consDir);
}

static void Test46SynthesisIsolationAndCacheParity()
{
    FaceId ids[4]{};
    {
        // 在作用域内让 SimSun 共享单一物理 FT_Face 的 4 个逻辑态并析构，验证无双重释放崩溃
        auto engine = MakeRealEngine();
        const Font states[4] = {
            { 20.0f, "SimSun", 400, false },
            { 20.0f, "SimSun", 700, false },
            { 20.0f, "SimSun", 400, true },
            { 20.0f, "SimSun", 700, true },
        };
        for (int i = 0; i < 4; ++i)
        {
            ids[i] = engine->FaceIdFor(states[i].family, states[i].weight, states[i].italic);
            EXPECT_TRUE(ids[i] != 0);
        }
        for (int i = 0; i < 4; ++i)
        {
            for (int j = i + 1; j < 4; ++j)
            {
                EXPECT_NE(ids[i], ids[j]);
            }
        }

        // B->O 叠加：(700, T) 墨量 > (400, T) 且具斜切质心右偏 >= 1.2px
        GlyphBitmap bmpRegIta, bmpBoldIta, bmpBold, bmpReg;
        EXPECT_TRUE(engine->Glyph(states[0], U'测', bmpReg));
        EXPECT_TRUE(engine->Glyph(states[1], U'测', bmpBold));
        EXPECT_TRUE(engine->Glyph(states[2], U'测', bmpRegIta));
        EXPECT_TRUE(engine->Glyph(states[3], U'测', bmpBoldIta));
        EXPECT_TRUE(BitmapInkSum(bmpBoldIta) > BitmapInkSum(bmpRegIta));
        EXPECT_TRUE(TopBottomCentroidDx(bmpBoldIta) >= 1.2);

        // 六组缓存交替防投毒：familyCache / measureCache / ascentCache / lineHeightCache / advanceCache / glyphCache
        const std::string probeText = "测试 text";
        for (int pass = 0; pass < 2; ++pass)
        {
            const Size m0 = engine->MeasureText(states[0], probeText);
            const Size m1 = engine->MeasureText(states[1], probeText);
            const Size m2 = engine->MeasureText(states[2], probeText);
            const Size m3 = engine->MeasureText(states[3], probeText);
            EXPECT_EQ(m2.width, m0.width);
            EXPECT_EQ(m3.width, m1.width);
            EXPECT_TRUE(m1.width > m0.width);

            long adv0 = 0, adv1 = 0, adv2 = 0, adv3 = 0;
            EXPECT_TRUE(engine->AdvancePx(states[0], U'测', adv0));
            EXPECT_TRUE(engine->AdvancePx(states[1], U'测', adv1));
            EXPECT_TRUE(engine->AdvancePx(states[2], U'测', adv2));
            EXPECT_TRUE(engine->AdvancePx(states[3], U'测', adv3));
            EXPECT_EQ(adv2, adv0);
            EXPECT_EQ(adv3, adv1);
            EXPECT_TRUE(adv1 > adv0);

            EXPECT_EQ(engine->Ascent(states[3]), engine->Ascent(states[0]));
            EXPECT_EQ(engine->LineHeight(states[3]), engine->LineHeight(states[0]));

            GlyphBitmap g0, g1, g2, g3;
            EXPECT_TRUE(engine->Glyph(states[0], U'测', g0));
            EXPECT_TRUE(engine->Glyph(states[1], U'测', g1));
            EXPECT_TRUE(engine->Glyph(states[2], U'测', g2));
            EXPECT_TRUE(engine->Glyph(states[3], U'测', g3));
            ExpectGlyphsBitIdentical(g0, bmpReg);
            ExpectGlyphsBitIdentical(g1, bmpBold);
            ExpectGlyphsBitIdentical(g2, bmpRegIta);
            ExpectGlyphsBitIdentical(g3, bmpBoldIta);
            (void)pass;
        }
    }
}

/// T46-5：中文族名端到端解析（FaceId + MeasureText + GlyphByKey 逐位等价 + n1-only 边界 + 方案 ⓐ 文件存在性守卫）
static void Test46ChineseFamilyEndToEndResolution()
{
    std::shared_ptr<FontEngine> engine(MakeRealEngine().release());
    FreeTypeTextMeasurer measurer(engine);

    const auto assertPair = [&](const char* zhFamily, const char* enFamily) {
        const FaceId fZh = engine->FaceIdFor(zhFamily);
        const FaceId fEn = engine->FaceIdFor(enFamily);
        EXPECT_TRUE(fZh != 0);
        EXPECT_TRUE(fEn != 0);
        EXPECT_EQ(fZh, fEn);

        const Font zhFont{ 16.0f, zhFamily, 400, false };
        const Font enFont{ 16.0f, enFamily, 400, false };
        const std::string probe = "测试文本 ABC 123";
        const Size sZh = measurer.MeasureText(zhFont, probe);
        const Size sEn = measurer.MeasureText(enFont, probe);
        EXPECT_EQ(sZh.width, sEn.width);
        EXPECT_EQ(sZh.height, sEn.height);

        const char32_t cps[3] = { U'测', U'文', U'A' };
        for (char32_t cp : cps)
        {
            GlyphBitmap gz, ge;
            EXPECT_TRUE(engine->Glyph(zhFont, cp, gz));
            EXPECT_TRUE(engine->Glyph(enFont, cp, ge));
            ExpectGlyphsBitIdentical(gz, ge);
        }
    };

    if (FontFileExists(L"simsun.ttc"))
    {
        assertPair("宋体", "SimSun");
        assertPair("新宋体", "NSimSun");
        EXPECT_NE(engine->FaceIdFor("新宋体"), engine->FaceIdFor("宋体"));
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: simsun.ttc missing in Windows Fonts; skipping SimSun/NSimSun assertions");
    }

    if (FontFileExists(L"msyh.ttc"))
    {
        assertPair("微软雅黑", "Microsoft YaHei");
        EXPECT_EQ(engine->FaceIdFor("微软雅黑 UI"), engine->FaceIdFor(std::string()));
        EXPECT_NE(engine->FaceIdFor("微软雅黑 UI"), engine->FaceIdFor("Microsoft YaHei UI"));
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: msyh.ttc missing in Windows Fonts; skipping Microsoft YaHei assertions");
    }

    if (FontFileExists(L"simhei.ttf"))
    {
        assertPair("黑体", "SimHei");
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: simhei.ttf missing in Windows Fonts; skipping SimHei assertions");
    }

    if (FontFileExists(L"simkai.ttf"))
    {
        assertPair("楷体", "KaiTi");
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: simkai.ttf missing in Windows Fonts; skipping KaiTi assertions");
    }

    if (FontFileExists(L"simfang.ttf"))
    {
        assertPair("仿宋", "FangSong");
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: simfang.ttf missing in Windows Fonts; skipping FangSong assertions");
    }

    if (FontFileExists(L"Deng.ttf"))
    {
        assertPair("等线", "DengXian");
    }
    else
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-5 guard: Deng.ttf missing in Windows Fonts; skipping DengXian assertions");
    }
}

/// T46-6：中文族名 + 合成粗/斜联动 + 未知中文族名回落保形
static void Test46ChineseFamilySynthesisAndFallback()
{
    if (!FontFileExists(L"msyh.ttc") || !FontFileExists(L"msyhbd.ttc") || !FontFileExists(L"simsun.ttc"))
    {
        Logger::Log(LogLevel::Warning,
                    L"T46-6 guard: required CJK fonts (msyh.ttc/msyhbd.ttc/simsun.ttc) missing; skipping assertions");
        return;
    }

    auto engine = MakeRealEngine();

    // ① 微软雅黑 @700, italic=true：命中真粗体 msyhbd.ttc#0 + 合成斜切
    const FaceId zhYahei700Ita = engine->FaceIdFor("微软雅黑", 700, true);
    const FaceId enYahei700Ita = engine->FaceIdFor("Microsoft YaHei", 700, true);
    EXPECT_EQ(zhYahei700Ita, enYahei700Ita);
    EXPECT_NE(zhYahei700Ita, engine->FaceIdFor("微软雅黑", 400, true));
    EXPECT_NE(zhYahei700Ita, engine->FaceIdFor("微软雅黑", 700, false));

    GlyphBitmap yhBoldIta;
    EXPECT_TRUE(engine->Glyph({ 20.0f, "微软雅黑", 700, true }, U'测', yhBoldIta));
    EXPECT_TRUE(TopBottomCentroidDx(yhBoldIta) >= 1.2);
    EXPECT_EQ(engine->MeasureText({ 20.0f, "微软雅黑", 700, true }, "测试文本").width,
              engine->MeasureText({ 20.0f, "微软雅黑", 700, false }, "测试文本").width);

    // ② 宋体四态互异 + (700, true) 叠加合成粗+斜
    const FaceId ss00 = engine->FaceIdFor("宋体", 400, false);
    const FaceId ss10 = engine->FaceIdFor("宋体", 700, false);
    const FaceId ss01 = engine->FaceIdFor("宋体", 400, true);
    const FaceId ss11 = engine->FaceIdFor("宋体", 700, true);
    const FaceId ssIds[4] = { ss00, ss10, ss01, ss11 };
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            EXPECT_NE(ssIds[i], ssIds[j]);
        }
    }
    EXPECT_EQ(ss11, engine->FaceIdFor("SimSun", 700, true));
    GlyphBitmap ssBmpRegIta, ssBmpBoldIta;
    EXPECT_TRUE(engine->Glyph({ 20.0f, "宋体", 400, true }, U'测', ssBmpRegIta));
    EXPECT_TRUE(engine->Glyph({ 20.0f, "宋体", 700, true }, U'测', ssBmpBoldIta));
    EXPECT_TRUE(BitmapInkSum(ssBmpBoldIta) > BitmapInkSum(ssBmpRegIta));
    EXPECT_TRUE(TopBottomCentroidDx(ssBmpBoldIta) >= 1.2);

    // ③ 未知中文名告警回落默认面，且 @700, true 在回落面上保留粗斜合成态
    const FaceId unkReg = engine->FaceIdFor("不存在字体XYZ", 400, false);
    const FaceId unkBI = engine->FaceIdFor("不存在字体XYZ", 700, true);
    EXPECT_EQ(unkReg, engine->FaceIdFor(std::string(), 400, false));
    EXPECT_EQ(unkBI, engine->FaceIdFor(std::string(), 700, true));
    EXPECT_NE(unkReg, unkBI);
    GlyphBitmap unkBmpReg, unkBmpBI;
    EXPECT_TRUE(engine->Glyph({ 20.0f, "不存在字体XYZ", 400, false }, U'测', unkBmpReg));
    EXPECT_TRUE(engine->Glyph({ 20.0f, "不存在字体XYZ", 700, true }, U'测', unkBmpBI));
    EXPECT_TRUE(BitmapInkSum(unkBmpBI) > BitmapInkSum(unkBmpReg));
    EXPECT_TRUE(TopBottomCentroidDx(unkBmpBI) >= 1.2);
}

/// T46-7：英文快路径零扫描、@400 零漂移、精确大小写不折叠，以及懒索引一次性触发
static void Test46EnglishFastPathZeroScanAndParity()
{
    auto engine = MakeRealEngine();

    const FaceId fConsolaFile = engine->FaceIdFor("consola.ttf");
    const FaceId fConsolas = engine->FaceIdFor("Consolas");
    const FaceId fSimSun = engine->FaceIdFor("SimSun");
    const FaceId fDefault = engine->FaceIdFor(std::string());
    EXPECT_TRUE(fConsolaFile != 0);
    EXPECT_EQ(fConsolaFile, fConsolas);
    EXPECT_TRUE(fSimSun != 0);
    EXPECT_TRUE(fDefault != 0);
    EXPECT_EQ(engine->LocalizedScanCountForTests(), std::size_t{0});

    // @400 常规测量与栅格输出契约零漂移
    const Font consFont{ 16.0f, "Consolas", 400, false };
    const Size mCons = engine->MeasureText(consFont, "Hello 123");
    const float lhCons = engine->LineHeight(consFont);
    const float ascCons = engine->Ascent(consFont);
    EXPECT_TRUE(mCons.width > 0.0f);
    EXPECT_TRUE(lhCons > ascCons && ascCons > 0.0f);
    long advH = 0;
    EXPECT_TRUE(engine->AdvancePx(consFont, U'H', advH));
    EXPECT_TRUE(advH > 0);
    GlyphBitmap bmpH;
    EXPECT_TRUE(engine->Glyph(consFont, U'H', bmpH));
    EXPECT_TRUE(bmpH.width > 0 && bmpH.height > 0 && bmpH.advance == advH);
    EXPECT_EQ(engine->LocalizedScanCountForTests(), std::size_t{0});

    // "SIMSUN" 按精确 UTF-8 契约 Miss 英文快路径（触发扫描 0 -> 1 且不按大小写折叠命中）
    const FaceId upperSimSun = engine->FaceIdFor("SIMSUN");
    EXPECT_EQ(engine->LocalizedScanCountForTests(), std::size_t{1});
    EXPECT_EQ(upperSimSun, fDefault);

    // 对非默认字体验证大小写不折叠（"CONSOLAS" 不命中 "Consolas"），且多次中文/miss 扫描恒保持为 1
    EXPECT_EQ(engine->FaceIdFor("CONSOLAS"), fDefault);
    EXPECT_NE(engine->FaceIdFor("CONSOLAS"), fConsolas);
    EXPECT_EQ(engine->FaceIdFor("不存在字体ABC"), fDefault);
    EXPECT_EQ(engine->LocalizedScanCountForTests(), std::size_t{1});
    if (FontFileExists(L"msyh.ttc"))
    {
        EXPECT_EQ(engine->FaceIdFor("微软雅黑"), engine->FaceIdFor("Microsoft YaHei"));
        EXPECT_EQ(engine->LocalizedScanCountForTests(), std::size_t{1});
    }
}

void ECDI::Test::RegisterFontWeightTests()
{
    GetTestRegistry().Add("FontWeight.SimSunSyntheticBoldWidthBand", &Test36SimSunSyntheticBoldWidthBand);     // T36-2
    // ── Phase 36 批二：解析链 ──
    GetTestRegistry().Add("FontWeight.PickNearestTierTruthTable", &Test36PickNearestTierTruthTable);           // T36-3
    GetTestRegistry().Add("FontWeight.FaceIdPerFamilyAndWeight", &Test36FaceIdPerFamilyAndWeight);             // T36-4
    GetTestRegistry().Add("FontWeight.UnresolvedFallsBackAndCaches", &Test36UnresolvedFallsBackAndCaches);     // T36-5
    GetTestRegistry().Add("FontWeight.MeasureCacheKeyIncludesWeight", &Test36MeasureCacheKeyIncludesWeight);   // T36-6
    GetTestRegistry().Add("FontWeight.FaceIdentityTierFiltering", &Test36FaceIdentityTierFiltering);           // T36-8

    // ── Phase 45 批一：斜体选面与键完备性 ──
    GetTestRegistry().Add("FontWeight.ItalicFaceSelection", &Test45ItalicFaceSelection);                       // T45-2
    GetTestRegistry().Add("FontWeight.KeyCompletenessPoisoning", &Test45KeyCompletenessPoisoning);             // T45-7

    // ── Phase 46：图集饱和守卫 + 合成粗体/斜体与四态隔离 + 中文族名惰性解析 ──
    GetTestRegistry().Add("FontWeight.GlyphAtlasFullObservability", &Test46GlyphAtlasFullObservability);       // T46-8
    GetTestRegistry().Add("FontWeight.SyntheticBoldWidthRatioParity", &Test46SyntheticBoldWidthRatioParity);   // T46-1
    GetTestRegistry().Add("FontWeight.SyntheticObliqueSlantGeometry", &Test46SyntheticObliqueSlantGeometry);   // T46-2
    GetTestRegistry().Add("FontWeight.SynthesisIsolationAndCacheParity", &Test46SynthesisIsolationAndCacheParity); // T46-3
    GetTestRegistry().Add("FontWeight.ChineseFamilyEndToEndResolution", &Test46ChineseFamilyEndToEndResolution);   // T46-5
    GetTestRegistry().Add("FontWeight.ChineseFamilySynthesisAndFallback", &Test46ChineseFamilySynthesisAndFallback); // T46-6
    GetTestRegistry().Add("FontWeight.EnglishFastPathZeroScanAndParity", &Test46EnglishFastPathZeroScanAndParity);   // T46-7
}
