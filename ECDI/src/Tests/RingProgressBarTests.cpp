#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Animation/Easing.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Platform/PlatformRenderContext.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/Renderer.h"
#include "Render/GDIBackend.h"
#include "Render/RecordingBackend.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Theme/RingProgressBarStyle.h"
#include "ECDI/Widget/RingProgressBar.h"
#include "ECDI/Window/WindowState.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <variant>

namespace ECDI{

namespace{

using namespace std::chrono_literals;

constexpr float kEps = 0.001f;

// ── 替身：PlatformWindow（动画宿主——沿 ProgressBarTests 的 TestPlatformWindow 形态）──

class TestPlatformWindow final: public PlatformWindow{

public:

	void Show() override{}
	bool Release() noexcept override{ return true; }
	void Invalidate() override{ ++invalidateCount; }
	Size GetClientSize() const override{ return Size{ 800.0f, 600.0f }; }
	float GetDpiScale() const noexcept override{ return 1.0f; }
	const PlatformRenderContext& GetRenderContext() const override{ return m_context; }
	void UpdateTextInputCaret(const CaretGeometry&) override{}
	void DestroyTextInputCaret() override{}
	std::string GetClipboardText() const override{ return {}; }
	void SetClipboardText(const std::string&) override{}
	void StartTimer(int, unsigned int) override{ ++startCount; }
	void StopTimer(int) override{ ++stopCount; }
	void SetChromeMode(ChromeMode) override{}
	void SetCaptionHeight(int) override{}
	void SetResizeInset(int) override{}
	void SetWindowLayer(WindowLayer) override{}
	void Minimize() override{}
	void Maximize() override{}
	void Restore() override{}
	WindowState GetWindowState() const noexcept override{ return WindowState::restored; }
	void Hide() override{}
	void SetFileDropEnabled(bool) override{}
	void ApplyStartupSize() override{}
	void SetBounds(const Rect&) override{}
	[[nodiscard]] Rect GetBounds() const override{ return {}; }

	PlatformRenderContext m_context;
	int startCount = 0;
	int stopCount = 0;
	int invalidateCount = 0;
};

// ── TestableRingProgressBar：接缝注入替身 manager + 呈现值/样式只读暴露 ──

class TestableRingProgressBar final: public RingProgressBar{

public:

	void SetTestAnimationManager(AnimationManager* manager) noexcept{ m_testManager = manager; }

	float DisplayProgress() const noexcept{ return m_displayProgress; }

	const RingProgressBarStyle& Style() const noexcept{ return m_style; }

	/// @brief OnPaint 公开转发（protected 虚——测试需跳过 Widget::Paint 的 clip 包装直测控件语义）
	void PaintDirect(PaintContext& ctx, int x, int y){ OnPaint(ctx, x, y); }

protected:

	AnimationManager* ResolveAnimationManager() const noexcept override{ return m_testManager; }

private:

	AnimationManager* m_testManager = nullptr;

};

// ── 命令流取弧命令的助手（commands[1] / commands[2] = 轨道 / 进度——Widget::Paint 前后包 clip）──

bool ArcAt(const CommandBuffer& cmds, std::size_t idx, DrawArcCommand& out)
{
	if (idx >= cmds.size()) return false;
	const auto* p = std::get_if<DrawArcCommand>(&cmds[idx]);
	if (p == nullptr) return false;
	out = *p;
	return true;
}

// ══════════════════════════════════════════════════════════════════
// T33-13：Theme 契约（DefaultTheme 三字段非空默认 + Override 不被 ApplyTheme 覆盖）
// ══════════════════════════════════════════════════════════════════

void Test33_13ThemeStyleContract()
{
	// ① DefaultTheme 三字段非空默认（★ 破坏面回归锚：第 9 个纯虚的实现存在且给值）
	const RingProgressBarStyle defaults = GetDefaultTheme().GetRingProgressBarStyle();
	const Color track = defaults.trackColor.value;
	const Color fill  = defaults.fillColor.value;
	EXPECT_TRUE(track.a > 0);                  // 轨道色非空（alpha 非 0 = 有像素）
	EXPECT_TRUE(fill.a > 0);
	EXPECT_TRUE(defaults.thickness.value > 0.0f);
	// fillColor 与 ProgressBarStyle::background 同值（80,120,220——同族控件同色，条 95①）
	const auto progressBar = GetDefaultTheme().GetProgressBarStyle();
	EXPECT_EQ(fill.r, progressBar.fillColor.value.r);
	EXPECT_EQ(fill.g, progressBar.fillColor.value.g);
	EXPECT_EQ(fill.b, progressBar.fillColor.value.b);

	// ② Override 的 Set 后 ApplyTheme 不覆盖（StyleField D7 契约）
	const Color custom = Color::FromRGBA8(10, 20, 30);
	TestableRingProgressBar bar;
	bar.SetStyle(RingProgressBarStyleOverride{ .trackColor = custom });
	EXPECT_TRUE(bar.Style().trackColor.overridden);
	bar.ApplyTheme(GetDefaultTheme());
	EXPECT_EQ(bar.Style().trackColor.value, custom);   // overridden ⇒ Apply 被忽略
	// 未覆盖字段照常跟随主题
	EXPECT_TRUE(bar.Style().fillColor.value.a > 0);
}

// ══════════════════════════════════════════════════════════════════
// T33-14：值契约（clamp / NaN 拒绝 / 同目标 no-op）
// ══════════════════════════════════════════════════════════════════

void Test33_14ValueContract()
{
	// ① clamp [-1→0, 2→1]（无宿主——瞬时到位）
	TestableRingProgressBar bar;
	bar.SetProgress(-1.0f);
	EXPECT_EQ(bar.GetProgress(), 0.0f);
	EXPECT_EQ(bar.DisplayProgress(), 0.0f);
	bar.SetProgress(2.0f);
	EXPECT_EQ(bar.GetProgress(), 1.0f);
	EXPECT_EQ(bar.DisplayProgress(), 1.0f);

	// ② ★ NaN ⇒ 值不变且不启动动画（C33-10①——先于 clamp 拒绝）
	TestPlatformWindow platform;
	AnimationManager manager(platform);
	TestableRingProgressBar bar2;
	bar2.SetTestAnimationManager(&manager);
	bar2.SetProgress(0.5f);
	manager.Tick(300ms);                       // 走完一轮
	EXPECT_EQ(bar2.GetProgress(), 0.5f);

	bar2.SetProgress(std::numeric_limits<float>::quiet_NaN());
	EXPECT_EQ(bar2.GetProgress(), 0.5f);       // 值不变
	EXPECT_FALSE(manager.HasActive());         // ★ 未启动动画

	// ±Inf 同样拒绝（isfinite 覆盖）
	bar2.SetProgress(std::numeric_limits<float>::infinity());
	EXPECT_EQ(bar2.GetProgress(), 0.5f);
	EXPECT_FALSE(manager.HasActive());

	// ③ 同目标 no-op（目标 vs 目标判断键）
	bar2.SetProgress(0.3f);
	manager.Tick(300ms);
	EXPECT_FALSE(manager.HasActive());
	bar2.SetProgress(0.3f);                    // 同目标——no-op
	EXPECT_FALSE(manager.HasActive());
}

// ══════════════════════════════════════════════════════════════════
// T33-15：动画重入（替换式重启——from == 替换瞬间呈现值，沿 B24）
// ══════════════════════════════════════════════════════════════════

void Test33_15AnimationReentrant()
{
	TestPlatformWindow platform;
	AnimationManager manager(platform);
	TestableRingProgressBar bar;
	bar.SetTestAnimationManager(&manager);

	bar.SetProgress(1.0f);
	manager.Tick(50ms);
	const float p1 = bar.DisplayProgress();
	EXPECT_TRUE(p1 > 0.0f && p1 < 1.0f);       // 过渡中

	// 核心不变量：SetProgress 只改目标、不动呈现——替换动画 from == p1
	bar.SetProgress(0.5f);
	EXPECT_EQ(bar.DisplayProgress(), p1);

	manager.Tick(1ms);                          // 新动画从 p1 出发——未跳回 0
	EXPECT_TRUE(bar.DisplayProgress() > 0.0f);

	manager.Tick(300ms);
	EXPECT_EQ(bar.DisplayProgress(), 0.5f);    // 终值必达
}

// ══════════════════════════════════════════════════════════════════
// T33-16：绘制命令流（恒 2 条 DrawArc：轨道 sweep=360 + 进度 p*360；p=0 仍 2 条）
// ══════════════════════════════════════════════════════════════════

void Test33_16PaintCommands()
{
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);

	TestableRingProgressBar bar;
	bar.SetSize(100, 100);
	bar.SetProgress(0.75f);   // 无宿主——瞬时到位
	bar.Paint(ctx, 0, 0);

	EXPECT_EQ(commands.size(), size_t{ 4 });   // PushClip + 轨道 + 进度 + PopClip
	EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));
	EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[3]));

	// ① 轨道：sweep = 360、start = 270（12 点钟）、半径 = D/2 - t/2 = 50 - 3 = 47
	DrawArcCommand track{};
	EXPECT_TRUE(ArcAt(commands, 1, track));
	EXPECT_NEAR(track.sweepAngleDeg, 360.0f, kEps);
	EXPECT_NEAR(track.startAngleDeg, 270.0f, kEps);
	EXPECT_NEAR(track.radius, 47.0f, kEps);
	EXPECT_NEAR(track.center.x, 50.0f, kEps);
	EXPECT_NEAR(track.center.y, 50.0f, kEps);
	EXPECT_NEAR(track.width, 6.0f, kEps);      // DefaultTheme thickness

	// ② 进度：sweep = 0.75 * 360 = 270
	DrawArcCommand arc{};
	EXPECT_TRUE(ArcAt(commands, 2, arc));
	EXPECT_NEAR(arc.sweepAngleDeg, 270.0f, kEps);
	EXPECT_NEAR(arc.startAngleDeg, 270.0f, kEps);

	// ③ p = 0 ⇒ 仍 2 条（第二条 sweep = 0——命令流可断言，沿 ProgressBar 先例）
	CommandBuffer zero;
	PaintContext ctx2(zero, measurer);
	TestableRingProgressBar bar0;
	bar0.SetSize(100, 100);
	bar0.Paint(ctx2, 0, 0);
	EXPECT_EQ(zero.size(), size_t{ 4 });
	DrawArcCommand zArc{};
	EXPECT_TRUE(ArcAt(zero, 2, zArc));
	EXPECT_NEAR(zArc.sweepAngleDeg, 0.0f, kEps);
}

// ══════════════════════════════════════════════════════════════════
// T33-17：尺寸策略（min(w,h) 作直径、圆心居中；D=0 ⇒ 0 条命令）
// ══════════════════════════════════════════════════════════════════

void Test33_17SizeStrategy()
{
	// ① SetSize(200,100) ⇒ 直径 = 100、圆心 = (100, 50)（min 边 + 居中——不拉伸）
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);

	TestableRingProgressBar bar;
	bar.SetSize(200, 100);
	bar.SetProgress(0.5f);
	bar.Paint(ctx, 0, 0);

	DrawArcCommand track{};
	EXPECT_TRUE(ArcAt(commands, 1, track));
	EXPECT_NEAR(track.center.x, 100.0f, kEps);   // x + w/2
	EXPECT_NEAR(track.center.y, 50.0f, kEps);    // y + h/2
	EXPECT_NEAR(track.radius, 100.0f * 0.5f - 3.0f, kEps);   // D/2 - t/2，D = min(200,100) = 100

	// ② R <= 0（环宽 >= 直径）⇒ 命令仍发（退化判定归 NormalizeArc——C33-4 唯一实现）
	CommandBuffer thick;
	PaintContext ctx2(thick, measurer);
	TestableRingProgressBar bar2;
	bar2.SetSize(100, 100);
	bar2.SetStyle(RingProgressBarStyleOverride{ .thickness = 120.0f });   // t > D ⇒ R < 0
	bar2.Paint(ctx2, 0, 0);
	EXPECT_EQ(thick.size(), size_t{ 4 });        // ★ 不早退——命令流恒 2 条弧
	DrawArcCommand tArc{};
	EXPECT_TRUE(ArcAt(thick, 1, tArc));
	EXPECT_TRUE(tArc.radius < 0.0f);             // 半径可为负——后端拒绝（C33-4②）

	// ③ SetSize(0,0) ⇒ 控件自身发 **0 条弧**（D <= 0 早退——详设 △12）
	//    ★ E33-8（批三实测订正详设 T33-17）：「0 条命令」写错了——Widget::Paint 的 clip 对
	//    （PushClip/PopClip）在剔除栈为空时**照常发出**（PaintContext 双参构造 = 无界，
	//    IsRectVisible 恒 true）⇒ 实测 2 条 clip + 0 条弧。控件层语义用 **OnPaint 直调**断言
	//    （跳过 clip 包装）——这才是「控件自身发了几条弧」的正确口径。
	CommandBuffer empty;
	PaintContext ctx3(empty, measurer);
	TestableRingProgressBar bar3;
	bar3.SetSize(0, 0);
	bar3.PaintDirect(ctx3, 0, 0);
	EXPECT_EQ(empty.size(), size_t{ 0 });
	// 对照：走完整 Paint() 则是 2 条 clip（Push/Pop）——0 条弧不变
	CommandBuffer viaPaint;
	PaintContext ctx4(viaPaint, measurer);
	bar3.Paint(ctx4, 0, 0);
	EXPECT_EQ(viaPaint.size(), size_t{ 2 });
	EXPECT_TRUE(std::holds_alternative<PushClipCommand>(viaPaint[0]));
	EXPECT_TRUE(std::holds_alternative<PopClipCommand>(viaPaint[1]));
}

// ══════════════════════════════════════════════════════════════════
// T33-18：真窗口 E2E（p=0.75：进度弧像素可见——装置沿 T24-8b 先例）
// ══════════════════════════════════════════════════════════════════

struct RingAAWindow
{
	HWND hwnd = nullptr;
	~RingAAWindow(){ if (hwnd) DestroyWindow(hwnd); }
};

bool CreateRingAAWindow(const wchar_t* className, RingAAWindow& out)
{
	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	WNDCLASSW wc{};
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = className;
	if (!RegisterClassW(&wc))
	{
		EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
	}
	out.hwnd = CreateWindowExW(0, className, className, WS_POPUP,
	                           screenW - 210, 10, 200, 200,
	                           nullptr, nullptr, wc.hInstance, nullptr);
	EXPECT_TRUE(out.hwnd != nullptr);
	if (out.hwnd == nullptr) return false;
	ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
	return true;
}

COLORREF ReadRingPixel(HWND hwnd, int x, int y)
{
	HDC dc = GetDC(hwnd);
	const COLORREF color = GetPixel(dc, x, y);
	ReleaseDC(hwnd, dc);
	return color;
}

void Test33_18WindowedPaintE2E()
{
	RingAAWindow w;
	if (!CreateRingAAWindow(L"ECDI_RingAA_E2E", w)) return;

	GDIBackend backend;
	backend.Initialize(Win32RenderContext(w.hwnd));
	backend.SetAntiAliasing(true);
	InvalidateRect(w.hwnd, nullptr, FALSE);   // 确保 BeginPaint 拿到有效 update region（L2 先例）
	backend.BeginFrame(Color::White());

	// 完整链路：控件 → PaintContext 命令流 → Renderer 折算 → 真后端像素
	//（控件 200×100——宽高不等，顺带验证 min 边策略；进度 0.75 ⇒ sweep = 270°：12 点钟 → 9 点钟）
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	TestableRingProgressBar bar;
	bar.SetSize(200, 100);
	bar.SetProgress(0.75f);
	bar.Paint(ctx, 0, 0);

	Renderer renderer(backend);
	renderer.Execute(commands, 1.0f);
	backend.EndFrame();

	// 几何真值（与控件同源：D = min(200,100) = 100，圆心 = (100, 50)，R = 50 - 3 = 47，t = 6）
	// 270°（12 点钟）处 = 圆心正上方 47 px ⇒ (100, 3)——该点在**进度弧上**（0.75 经过 12 点钟）
	// ★ 判据 = 「进度弧像素 ≠ 轨道色」：轨道 (230,230,235) vs 进度 (80,120,220)，G 通道差 115 ⇒ 可分辨
	const COLORREF atTop = ReadRingPixel(w.hwnd, 100, 3);
	EXPECT_TRUE(atTop != CLR_INVALID);
	// 9 点钟（= 270°+270° = 180°… 精确：start=270° 顺时针 270° ⇒ 终点角 = 180° = 9 点钟）
	const COLORREF atLeft = ReadRingPixel(w.hwnd, 53, 50);
	EXPECT_TRUE(atLeft != CLR_INVALID);

	// 判据①：两采样点都必须**非白**（背景）——弧真的画上去了
	EXPECT_TRUE(atTop != CLR_INVALID && !(GetRValue(atTop) >= 250 && GetGValue(atTop) >= 250 && GetBValue(atTop) >= 250));
	EXPECT_TRUE(atLeft != CLR_INVALID && !(GetRValue(atLeft) >= 250 && GetGValue(atLeft) >= 250 && GetBValue(atLeft) >= 250));

	// 判据②：进度弧终点附近（9 点钟）**不是轨道色**——轨道只在整环，进度色覆盖其上
	// 轨道 G=230、进度 G=120：G 通道 < 180 ⇒ 已被进度色覆盖（容差避开 AA 过渡带）
	EXPECT_TRUE(GetGValue(atLeft) < 180);

	// ③ ★ E33-9（批三实测订正判据方位）：start=270°（12 点钟）顺时针 270° ⇒ 覆盖
	//    12 点钟 → 3 点钟 → 6 点钟 → 9 点钟，**3 点钟在进度弧内**（G≈120）、
	//    **缺口 = 左上象限（9→12 点钟）**。判据 = 弧上点（3 点钟）取进度色 G<180、
	//    弧外缺口（10:30 方向）保留轨道色 G>180。
	const COLORREF atRight = ReadRingPixel(w.hwnd, 147, 50);   // 3 点钟——弧上
	EXPECT_TRUE(atRight != CLR_INVALID);
	EXPECT_TRUE(GetGValue(atRight) < 180);   // 进度色覆盖（G≈120）
	const COLORREF atGap = ReadRingPixel(w.hwnd, 67, 17);      // 10:30 方向——缺口
	EXPECT_TRUE(atGap != CLR_INVALID);
	EXPECT_TRUE(GetGValue(atGap) > 180);     // 轨道色保留——「进度弧只在 sweep 段内」
}

} // anonymous namespace

void ECDI::Test::RegisterRingProgressBarTests()
{
	GetTestRegistry().Add("Theme.RingProgressBarStyleContract",   &Test33_13ThemeStyleContract);
	GetTestRegistry().Add("RingProgressBar.ValueContract",        &Test33_14ValueContract);
	GetTestRegistry().Add("RingProgressBar.AnimationReentrant",   &Test33_15AnimationReentrant);
	GetTestRegistry().Add("RingProgressBar.PaintCommands",        &Test33_16PaintCommands);
	GetTestRegistry().Add("RingProgressBar.SizeStrategy",         &Test33_17SizeStrategy);
	GetTestRegistry().Add("RingProgressBar.WindowedPaintE2E",     &Test33_18WindowedPaintE2E);
}

}
