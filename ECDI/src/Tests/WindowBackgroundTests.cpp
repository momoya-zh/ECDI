#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Color.h"
#include "Render/RecordingBackend.h"          // 测试替身（库内内部头——同 RendererTests.cpp:11）
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Window/Window.h"

#include <cstddef>
#include <memory>
#include <utility>

using namespace ECDI;

// ── Phase 18：窗口客户区底色（端到端）────────────────────────────────────────
// 覆盖链：Application → Window::SetBackgroundColor / PaintFrame → Renderer::BeginFrame
//         → RenderingBackend::BeginFrame → RecordingBackend::frameBackgrounds
//
// ★ 本组是**端到端**用例（详设 §2）：帧必须由 `Show()` + 泵消息驱动——
//   `Window::PaintFrame()` 是 private（不能手动驱动），而 `Application::Create` 的第 4 形参
//   （Phase 18 打通的 `RenderServices` 注入通路）让测试能观察 Window 内部的后端。
// ★ 边界（详设 §5/§6）：alpha 只断言「**原样传递**」——`GDIBackend` 内部是否走
//   `BlendAlphaSolid` 由一个测试装置**观察不到**，由源码级结构性审查（A4）保证。**不假装测到了。**

namespace {

constexpr float kEpsilon = 0.001f;   // 与 RendererTests 同基准

/// @brief 手动消息泵（仿 WindowChromeTests.cpp:99-109——处理至多 maxCount 条消息后返回）
/// @details 有上限，故不受「测试替身不消费 update region ⇒ WM_PAINT 洪水」影响。
void PumpMessages(int maxCount) {

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {

		TranslateMessage(&msg);
		DispatchMessageW(&msg);

	}
}

/// @brief 测试装置：Application + 注入的 RecordingBackend（观察面 = frameBackgrounds）
/// @details 成员声明序保证 `app` **后于**本结构的其余成员析构 ⇒ 窗口在 Application 之前释放
/// （与 WindowChromeTests 的 TestWindow 同约束）。
struct BackgroundProbe {

	Application app;

	RecordingBackend* recorder = nullptr;   ///< **非拥有**（所有权已 move 进 RenderServices → Window）

	Window* window = nullptr;               ///< **非拥有**（所有权归 Application）

	explicit BackgroundProbe(const char* title) {

		// RecordingBackend 同时实现 RenderingBackend 与 TextMeasurer，而 RenderServices 要求
		// 两个**独立对象**（RenderServices.h:11-13）⇒ 必须构造两个实例；观察者留裸指针。
		auto recorderOwned = std::make_unique<RecordingBackend>();
		recorder = recorderOwned.get();

		RenderServices services{
			std::move(recorderOwned),
			std::make_unique<RecordingBackend>()   // 测量占位（不参与断言）
		};

		window = &app.Create(title, 400, 300, std::move(services));
	}

	~BackgroundProbe() { window->Release(); }

	/// @brief 显示并泵一帧（WM_PAINT → OnPaint → PaintFrame → BeginFrame(m_backgroundColor)）
	void ShowAndPump() { window->Show(); PumpMessages(64); }

	/// @brief 最近一帧送给后端的背景色
	/// @details 无帧时返回 `(-1,-1,-1,-1)` 哨兵——`EXPECT_TRUE` 失败**不中断**用例，
	/// 若无条件 `back()` 会越界（本组用哨兵把「无帧」变成可断言的取值）。
	Color LastBackground() const {

		if (recorder->frameBackgrounds.empty()) { return Color{ -1.0f, -1.0f, -1.0f, -1.0f }; }

		return recorder->frameBackgrounds.back();

	}
};

/// @brief T18-1 默认底色 = 白（C1 的第一面：不配置时的行为）
void TestDefaultWhite() {

	BackgroundProbe probe("ECDI_BgDefault");

	probe.ShowAndPump();

	EXPECT_TRUE(!probe.recorder->frameBackgrounds.empty());   // 帧确实到达了后端

	const Color bg = probe.LastBackground();
	EXPECT_NEAR(bg.r, 1.0f, kEpsilon);
	EXPECT_NEAR(bg.g, 1.0f, kEpsilon);
	EXPECT_NEAR(bg.b, 1.0f, kEpsilon);
	EXPECT_NEAR(bg.a, 1.0f, kEpsilon);

}

/// @brief T18-2 自定义色逐段到达后端（C5：每帧传递，无缓存）
void TestCustomColorReachesBackend() {

	BackgroundProbe probe("ECDI_BgCustom");

	probe.window->SetBackgroundColor(Color::FromRGBA8(0x0F, 0x11, 0x15));

	probe.ShowAndPump();

	EXPECT_TRUE(!probe.recorder->frameBackgrounds.empty());

	const Color bg = probe.LastBackground();
	EXPECT_NEAR(bg.r, 15.0f / 255.0f, kEpsilon);
	EXPECT_NEAR(bg.g, 17.0f / 255.0f, kEpsilon);
	EXPECT_NEAR(bg.b, 21.0f / 255.0f, kEpsilon);
	EXPECT_NEAR(bg.a, 1.0f, kEpsilon);

}

/// @brief T18-3 改色 ⇒ 下一帧生效（O1 的配套证据：状态变化 + 请求重绘 ⇒ 新帧用新色）
void TestChangeTakesEffectNextFrame() {

	BackgroundProbe probe("ECDI_BgChange");

	probe.ShowAndPump();

	const std::size_t n0 = probe.recorder->frameBackgrounds.size();
	EXPECT_TRUE(n0 > 0);

	probe.window->SetBackgroundColor(Color::Red());   // 内含 Invalidate()（请求重绘）

	PumpMessages(64);

	EXPECT_TRUE(probe.recorder->frameBackgrounds.size() > n0);   // 确实产生了新帧

	const Color bg = probe.LastBackground();
	EXPECT_NEAR(bg.r, 1.0f, kEpsilon);
	EXPECT_NEAR(bg.g, 0.0f, kEpsilon);
	EXPECT_NEAR(bg.b, 0.0f, kEpsilon);

}

/// @brief T18-4 显式 `Color::White()` 与默认取值**逐位相同**（C1 的另一面 / D5 的「恢复默认」）
/// @details ⚠️ 措辞纪律（详设 §6）：本用例验证的是「**默认背景参数 == 显式 `White()`**」，
/// **不是**「GDI 像素输出与 Phase 17 逐位相同」——本相位不做像素级验收（O3）。
void TestExplicitWhiteEqualsDefault() {

	BackgroundProbe byDefault("ECDI_BgDefWhite");
	byDefault.ShowAndPump();
	EXPECT_TRUE(!byDefault.recorder->frameBackgrounds.empty());
	const Color a = byDefault.LastBackground();

	BackgroundProbe byExplicit("ECDI_BgExpWhite");
	byExplicit.window->SetBackgroundColor(Color::White());
	byExplicit.ShowAndPump();
	EXPECT_TRUE(!byExplicit.recorder->frameBackgrounds.empty());
	const Color b = byExplicit.LastBackground();

	EXPECT_EQ(a.r, b.r);
	EXPECT_EQ(a.g, b.g);
	EXPECT_EQ(a.b, b.b);
	EXPECT_EQ(a.a, b.a);

}

/// @brief T18-5 alpha **原样传递**（C4 的可测部分）
/// @details 只断言链路不改写 alpha；「GDI 侧不进 BlendAlphaSolid」由 A4 源码审查承担。
void TestAlphaIgnored() {

	BackgroundProbe probe("ECDI_BgAlpha");

	probe.window->SetBackgroundColor(Color{ 1.0f, 0.0f, 0.0f, 0.5f });

	probe.ShowAndPump();

	EXPECT_TRUE(!probe.recorder->frameBackgrounds.empty());

	const Color bg = probe.LastBackground();
	EXPECT_NEAR(bg.a, 0.5f, kEpsilon);   // ★ alpha 原样到达后端（未被就地归零/钳制）
	EXPECT_NEAR(bg.r, 1.0f, kEpsilon);
	EXPECT_NEAR(bg.g, 0.0f, kEpsilon);
	EXPECT_NEAR(bg.b, 0.0f, kEpsilon);

}

}   // namespace

void ECDI::Test::RegisterWindowBackgroundTests() {

	GetTestRegistry().Add("WindowBackground.DefaultWhite",              &TestDefaultWhite);
	GetTestRegistry().Add("WindowBackground.CustomColorReachesBackend", &TestCustomColorReachesBackend);
	GetTestRegistry().Add("WindowBackground.ChangeTakesEffectNextFrame", &TestChangeTakesEffectNextFrame);
	GetTestRegistry().Add("WindowBackground.ExplicitWhiteEqualsDefault", &TestExplicitWhiteEqualsDefault);
	GetTestRegistry().Add("WindowBackground.AlphaIgnored",              &TestAlphaIgnored);

}
