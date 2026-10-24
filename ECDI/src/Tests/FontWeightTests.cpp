#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "Platform/Win32/Win32RenderContext.h"
#include "Platform/Win32/Win32FontSource.h"
#include "Render/FontEngine.h"
#include "Render/FontWeightRule.h"
#include "Render/GDITextMeasurer.h"

#include <cstdint>
#include <memory>
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
    // P0-② 边界钉：ZH 族名不解析（需求非目标 3）⇒ 回退默认 face（与空 family 同 face）+ 告警
    EXPECT_EQ(engine->FaceIdFor("微软雅黑"), engine->FaceIdFor(std::string()));
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

void ECDI::Test::RegisterFontWeightTests()
{
    GetTestRegistry().Add("FontWeight.SimSunSyntheticBoldWidthBand", &Test36SimSunSyntheticBoldWidthBand);     // T36-2
    // ── Phase 36 批二：解析链 ──
    GetTestRegistry().Add("FontWeight.PickNearestTierTruthTable", &Test36PickNearestTierTruthTable);           // T36-3
    GetTestRegistry().Add("FontWeight.FaceIdPerFamilyAndWeight", &Test36FaceIdPerFamilyAndWeight);             // T36-4
    GetTestRegistry().Add("FontWeight.UnresolvedFallsBackAndCaches", &Test36UnresolvedFallsBackAndCaches);     // T36-5
    GetTestRegistry().Add("FontWeight.MeasureCacheKeyIncludesWeight", &Test36MeasureCacheKeyIncludesWeight);   // T36-6
    GetTestRegistry().Add("FontWeight.FaceIdentityTierFiltering", &Test36FaceIdentityTierFiltering);           // T36-8
}
