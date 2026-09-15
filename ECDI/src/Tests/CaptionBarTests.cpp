#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/EventSystem/EventRouter.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Window/CaptionBar.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/WindowState.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Render/RecordingBackend.h"
#include "Window/CaptionButton.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace ECDI;

namespace {

// ══════════════════════════════════════════════════════════════════
// Phase 13 CaptionBar：命中委托（D9 回归锚）/ 按钮命令 / 状态查询 / 绘制命令断言
// 坐标常量与 CaptionBar.cpp 的布局常量同源（w=800 / 按钮宽 46 / 左距 12 / 右留白 8）
// ══════════════════════════════════════════════════════════════════

constexpr int kWinW = 800;
constexpr int kWinH = 600;
constexpr int kCaption = 32;   ///< 行为区高度 = 实体区高度（D7 建议同值）
constexpr int kInset = 8;

constexpr int kBtnW = 46;

constexpr int kMinX = kWinW - 3 * kBtnW;   ///< 662
constexpr int kMaxX = kWinW - 2 * kBtnW;   ///< 708
constexpr int kCloseX = kWinW - kBtnW;     ///< 754

constexpr int kBtnCenterY = kCaption / 2;   ///< 16（caption 条带内，且 > inset ⇒ 非 resize 区）

constexpr int kTitleProbeX = 300;   ///< 标题 Label 覆盖区（标题几何 [12, 654)）
constexpr int kGapProbeX = kMinX - 4;   ///< 658 ∈ [654, 662) 标题右留白（无子控件 ⇒ 拖拽区）

/// @brief 捕获窗口状态事件 + 关闭请求的 Application（WindowChromeTests::TestApp 同款形态）
/// @details ⚠️ OnWindowCloseRequested **刻意不调基类**（基类默认 = Release()）——
/// 本文件用它验证「自绘 X 走的是可拦截的关闭请求」（D8），而非绕过应用的资源层销毁。
struct TestApp : public Application {

    std::vector<WindowState> seen;

    int closeRequestedCount = 0;

protected:

    void OnWindowStateChanged(const WindowStateChangedEvent& e) override {

        seen.push_back(e.GetState());

    }

    void OnWindowCloseRequested(const WindowCloseRequestedEvent&) override {

        ++closeRequestedCount;

    }

};

/// @brief 测试场景守卫（D-TST-1 同款：本对象必须**先于** Application 析构）
/// @details 非拥有 Window*——所有权归 Application（`Application::Create`）；析构只 Release HWND。
struct CaptionFixture {

    Window* window = nullptr;

    CaptionBar* bar = nullptr;

    explicit CaptionFixture(Application& a) {

        window = &a.Create("ECDI_CaptionTest", kWinW, kWinH);

        window->SetChromeMode(ChromeMode::Borderless);
        window->SetCaptionHeight(kCaption);
        window->SetResizeInset(kInset);

        auto barPtr = std::make_unique<CaptionBar>(*window, "ECDI");

        bar = barPtr.get();

        bar->SetPosition(0, 0);
        bar->SetSize(kWinW, kCaption);

        window->GetRootWidget().AddChild(std::move(barPtr));

    }

    ~CaptionFixture() {

        window->Release();   // 只销毁 HWND（WM_DESTROY → Application 回收 Window 对象）

    }

    /// @brief 取底层 HWND（WindowChromeTests::TestWindow::Handle 三跳同款）
    HWND Handle() const {

        return static_cast<const Win32RenderContext&>(
            window->GetPlatformWindow().GetRenderContext()).GetHandle();

    }

};

/// @brief 窗口局部坐标 → WM_NCHITTEST 查询（同步直达 WndProc——未显示窗口同样有效）
LRESULT HitTestLocal(HWND hwnd, int x, int y) {

    RECT wr{};
    GetWindowRect(hwnd, &wr);

    const POINT pt{ wr.left + x, wr.top + y };

    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));

}

/// @brief 合成一次左键点击（Down + Up 同坐标）——经**公开入口** Application::OnEvent 派发，
/// 走完整 HitTest → Target Dispatch → 隐式 Capture → Up 派发 链（无需真实鼠标消息 / 屏幕坐标换算）
void ClickAt(Application& app, Window& w, int x, int y) {

    const MouseButtonDownEvent down(&w, x, y, MouseButton::Left);

    app.OnEvent(down);

    const MouseButtonUpEvent up(&w, x, y, MouseButton::Left);

    app.OnEvent(up);

}

/// @brief 手动消息泵（Show 后状态变化需消息到达——WindowChromeTests::PumpMessages 同款）
void PumpMessages(int maxCount) {

    MSG msg{};

    for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {

        TranslateMessage(&msg);
        DispatchMessageW(&msg);

    }

}

/// @brief 统计命令缓冲中的 DrawLineCommand 条数（语义要素过滤——ClipTests::TraceClip 同款）
size_t CountLines(const CommandBuffer& commands) {

    size_t n = 0;

    for (const auto& cmd : commands) {

        if (std::holds_alternative<DrawLineCommand>(cmd)) ++n;

    }

    return n;

}

// ══════════════════════════════════════════════════════════════════
// T13-1：命中委托八态（D9 回归锚——「HitTest 命中」≠「应返回 HTCLIENT」）
// ══════════════════════════════════════════════════════════════════

void TestHitTestDelegation()
{
    TestApp app;
    CaptionFixture fx(app);

    const HWND hwnd = fx.Handle();

    // ① 标题 Label 覆盖区 → HTCAPTION（命中 Label，但 Label 不消费鼠标——D9 核心反例）
    EXPECT_EQ(HitTestLocal(hwnd, kTitleProbeX, kBtnCenterY), HTCAPTION);

    // ②③④ 三按钮中心 → HTCLIENT（消费鼠标 ⇒ 平台层走客户区、事件派发给控件）
    EXPECT_EQ(HitTestLocal(hwnd, kMinX + kBtnW / 2, kBtnCenterY), HTCLIENT);
    EXPECT_EQ(HitTestLocal(hwnd, kMaxX + kBtnW / 2, kBtnCenterY), HTCLIENT);
    EXPECT_EQ(HitTestLocal(hwnd, kCloseX + kBtnW / 2, kBtnCenterY), HTCLIENT);

    // ⑤ 标题右留白条带 [654, 662)（无子控件）→ HTCAPTION（空白即拖拽区）
    EXPECT_EQ(HitTestLocal(hwnd, kGapProbeX, kBtnCenterY), HTCAPTION);

    // ⑥ (2,2) → HTTOPLEFT（resize 优先于 caption——Phase 12 T2 原断言不受影响）
    EXPECT_EQ(HitTestLocal(hwnd, 2, 2), HTTOPLEFT);

    // ⑦ caption 条带之外 → HTCLIENT（原语义不变；取客户区中部避开 DPI 相关的条带边界）
    EXPECT_EQ(HitTestLocal(hwnd, kTitleProbeX, kWinH / 2), HTCLIENT);

    // ⑧ 禁用 max 按钮 → 落回 HTCAPTION（HitTest 过滤 Enabled ⇒ 该区域变回拖拽区）；
    //    重新启用 → 恢复 HTCLIENT（证明是禁用态而非布局问题）
    Widget* maxButton = fx.bar->GetChildAt(2);   // 0=标题 / 1=min / 2=max / 3=close
    EXPECT_TRUE(maxButton != nullptr);

    maxButton->SetEnabled(false);
    EXPECT_EQ(HitTestLocal(hwnd, kMaxX + kBtnW / 2, kBtnCenterY), HTCAPTION);

    maxButton->SetEnabled(true);
    EXPECT_EQ(HitTestLocal(hwnd, kMaxX + kBtnW / 2, kBtnCenterY), HTCLIENT);
}

// ══════════════════════════════════════════════════════════════════
// T13-2：按钮命令路径（min / max·restore 二态 / close 可拦截；D8 语义）
// ══════════════════════════════════════════════════════════════════

void TestButtonCommands()
{
    TestApp app;
    CaptionFixture fx(app);

    fx.window->Show();   // 运行期 API（Minimize/Maximize）有 @pre Show()
    PumpMessages(64);

    // ① max → maximized
    ClickAt(app, *fx.window, kMaxX + kBtnW / 2, kBtnCenterY);
    PumpMessages(64);
    EXPECT_TRUE(!app.seen.empty());
    EXPECT_EQ(app.seen.back(), WindowState::maximized);

    // ② 再点 max（二态切换）→ restored
    ClickAt(app, *fx.window, kMaxX + kBtnW / 2, kBtnCenterY);
    PumpMessages(64);
    EXPECT_EQ(app.seen.back(), WindowState::restored);

    // ③ close → 关闭请求被应用拦截（override 不调基类 ⇒ 窗口仍存活——D8 的核心断言）
    ClickAt(app, *fx.window, kCloseX + kBtnW / 2, kBtnCenterY);
    PumpMessages(16);
    EXPECT_EQ(app.closeRequestedCount, 1);
    EXPECT_TRUE(IsWindow(fx.Handle()) != FALSE);

    // ④ min（放最后——最小化后窗口坐标系不再可靠）→ minimized
    ClickAt(app, *fx.window, kMinX + kBtnW / 2, kBtnCenterY);
    PumpMessages(64);
    EXPECT_EQ(app.seen.back(), WindowState::minimized);
}

// ══════════════════════════════════════════════════════════════════
// T13-3：状态查询与事件同事实源（R3）
// ══════════════════════════════════════════════════════════════════

void TestStateQuery()
{
    TestApp app;
    CaptionFixture fx(app);

    fx.window->Show();
    PumpMessages(64);

    EXPECT_EQ(fx.window->GetWindowState(), WindowState::restored);

    fx.window->Maximize();
    PumpMessages(64);
    EXPECT_EQ(fx.window->GetWindowState(), WindowState::maximized);
    EXPECT_TRUE(!app.seen.empty());
    EXPECT_EQ(app.seen.back(), WindowState::maximized);   // 查询与事件不矛盾

    fx.window->Restore();
    PumpMessages(64);
    EXPECT_EQ(fx.window->GetWindowState(), WindowState::restored);
    EXPECT_EQ(app.seen.back(), WindowState::restored);
}

// ══════════════════════════════════════════════════════════════════
// T13-4a：CaptionButton 四态命令数（内部小对象 ⇒ 严格断言总数）
// 命令流 = PushClip → [背景(仅悬停/按下)] → glyph DrawLine → PopClip
// ══════════════════════════════════════════════════════════════════

void TestGlyphCommandCount()
{
    struct GlyphCase {

        CaptionButton::Glyph glyph;

        size_t total;   ///< 期望总命令数（含 PushClip / PopClip）

        size_t lines;   ///< 期望 DrawLine 条数

        const char* name;

    };

    const GlyphCase cases[] = {

        { CaptionButton::Glyph::Minimize, 3, 1, "Minimize" },
        { CaptionButton::Glyph::Close,    4, 2, "Close" },
        { CaptionButton::Glyph::Maximize, 6, 4, "Maximize" },
        { CaptionButton::Glyph::Restore,  8, 6, "Restore" },

    };

    for (const GlyphCase& c : cases) {

        RecordingBackend backend;   // 仅作 TextMeasurer 占位（PaintContext 构造需要）

        CommandBuffer commands;

        PaintContext ctx(commands, backend);

        CaptionButton btn(c.glyph, {});

        btn.SetSize(kBtnW, kCaption);
        btn.Paint(ctx, 0, 0);

        EXPECT_EQ(commands.size(), c.total);
        EXPECT_EQ(CountLines(commands), c.lines);
        EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands.front()));
        EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands.back()));

    }

    // Maximize 几何：46×32 按钮、绘制于 (0,0) ⇒ 中心 (23,16)、图标框 [18,28] × [11,21]
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    CaptionButton maxBtn(CaptionButton::Glyph::Maximize, {});
    maxBtn.SetSize(kBtnW, kCaption);
    maxBtn.Paint(ctx, 0, 0);

    const auto& first = std::get<DrawLineCommand>(commands[1]);   // [0]=PushClip

    EXPECT_NEAR(first.start.x, 18.0f, 0.01f);
    EXPECT_NEAR(first.start.y, 11.0f, 0.01f);
    EXPECT_NEAR(first.end.x, 28.0f, 0.01f);
    EXPECT_NEAR(first.end.y, 11.0f, 0.01f);
}

// ══════════════════════════════════════════════════════════════════
// T13-4b：CaptionBar 组合（标题 + 三按钮；组合体 ⇒ 只断语义要素，不绑定总条数）
// ══════════════════════════════════════════════════════════════════

void TestBarCombination()
{
    TestApp app;
    CaptionFixture fx(app);

    EXPECT_TRUE(fx.bar->GetTitle() == "ECDI");

    fx.bar->SetTitle("Hello");
    EXPECT_TRUE(fx.bar->GetTitle() == "Hello");

    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    fx.bar->Paint(ctx, 0, 0);

    size_t titleDraws = 0;
    bool titleInsideLabel = false;

    for (const auto& cmd : commands) {

        if (const auto* t = std::get_if<DrawTextCommand>(&cmd)) {

            if (t->text == "Hello") {

                ++titleDraws;

                titleInsideLabel = (t->pos.x >= 12.0f && t->pos.x < 654.0f);

            }

        }

    }

    EXPECT_EQ(titleDraws, 1);
    EXPECT_TRUE(titleInsideLabel);

    // 窗口未 Show ⇒ 状态 restored ⇒ max 按钮画 Maximize：min 1 + max 4 + close 2 = 7
    EXPECT_EQ(CountLines(commands), 7);

    // 宽度变化 → 子控件跟随（可重复调用契约：只按当前尺寸重算，无累积偏移）
    fx.bar->SetSize(600, kCaption);

    RecordingBackend backend2;
    CommandBuffer commands2;
    PaintContext ctx2(commands2, backend2);

    fx.bar->Paint(ctx2, 0, 0);

    EXPECT_EQ(CountLines(commands2), 7);   // 布局量变化不影响命令结构（无累积/无泄漏）

    // 三按钮在 600 宽下重新靠右：close 中心 = 600-46+23 = 577
    const Widget* closeButton = fx.bar->GetChildAt(3);
    EXPECT_TRUE(closeButton != nullptr);
    EXPECT_EQ(closeButton->GetX(), 600 - kBtnW);
    EXPECT_EQ(closeButton->GetY(), 0);
    EXPECT_EQ(closeButton->GetHeight(), kCaption);
}

}   // anonymous namespace

void ECDI::Test::RegisterCaptionBarTests()
{
    GetTestRegistry().Add("CaptionBar.HitTestDelegation", &TestHitTestDelegation);
    GetTestRegistry().Add("CaptionBar.ButtonCommands",    &TestButtonCommands);
    GetTestRegistry().Add("CaptionBar.StateQuery",        &TestStateQuery);
    GetTestRegistry().Add("CaptionBar.GlyphCommandCount", &TestGlyphCommandCount);
    GetTestRegistry().Add("CaptionBar.BarCombination",    &TestBarCombination);
}
