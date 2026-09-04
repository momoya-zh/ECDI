#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Animation/Easing.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Platform/PlatformRenderContext.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Widget/ProgressBar.h"

#include <chrono>
#include <string>

namespace ECDI{

namespace{

using namespace std::chrono_literals;

constexpr float kEps = 0.001f;   // EXPECT_NEAR 精度（同 WidgetTests）

// ── 测试替身：PlatformWindow（动画宿主——Tick 传假 elapsed 确定性推进，无窗口无 sleep）──

class TestPlatformWindow final: public PlatformWindow{

public:

	void Show() override{}

	bool Release() noexcept override{ return true; }

	void Invalidate() override{ ++invalidateCount; }

	Size GetClientSize() const override{ return Size{ 800.0f, 600.0f }; }

	const PlatformRenderContext& GetRenderContext() const override{ return m_context; }

	void UpdateTextInputCaret(const CaretGeometry&) override{}

	void DestroyTextInputCaret() override{}

	std::string GetClipboardText() const override{ return {}; }

	void SetClipboardText(const std::string&) override{}

	void StartTimer(int, unsigned int) override{ ++startCount; }

	void StopTimer(int) override{ ++stopCount; }

	PlatformRenderContext m_context;   ///< 空基类可实例化（无纯虚）——测试替身直接持有

	int startCount = 0;
	int stopCount = 0;
	int invalidateCount = 0;

};

// ── TestableProgressBar：注入替身 manager（详设 §2.2 接缝消费点）+ 呈现值只读暴露 ──

class TestableProgressBar final: public ProgressBar{

public:

	void SetTestAnimationManager(AnimationManager* manager) noexcept{ m_testManager = manager; }

	float DisplayProgress() const noexcept{ return m_displayProgress; }

	/// @brief 样式只读访问（自由函数断言用——protected 成员只有派生类成员函数内可读，需此中转）
	const ProgressBarStyle& Style() const noexcept{ return m_style; }

protected:

	AnimationManager* ResolveAnimationManager() const noexcept override{ return m_testManager; }

private:

	AnimationManager* m_testManager = nullptr;   ///< 替身宿主（nullptr = 无宿主 → 瞬时降级）

};

// ── 1. 默认值：逻辑/呈现双零（§2.1 状态分离）────────────────

void TestDefaultValues()
{
	TestableProgressBar bar;
	EXPECT_EQ(bar.GetProgress(), 0.0f);
	EXPECT_EQ(bar.DisplayProgress(), 0.0f);
}

// ── 2. 值域钳制（无宿主——瞬时到位，逻辑/呈现同步）────────────

void TestSetProgressClamp()
{
	TestableProgressBar bar;
	bar.SetProgress(2.0f);
	EXPECT_EQ(bar.GetProgress(), 1.0f);
	EXPECT_EQ(bar.DisplayProgress(), 1.0f);

	bar.SetProgress(-1.0f);
	EXPECT_EQ(bar.GetProgress(), 0.0f);
	EXPECT_EQ(bar.DisplayProgress(), 0.0f);
}

// ── 3. 百分比包装（clamp [0,100]）────────────────────────

void TestSetPercent()
{
	TestableProgressBar bar;
	bar.SetPercent(50);
	EXPECT_NEAR(bar.GetProgress(), 0.5f, kEps);

	bar.SetPercent(150);
	EXPECT_EQ(bar.GetProgress(), 1.0f);

	bar.SetPercent(-5);
	EXPECT_EQ(bar.GetProgress(), 0.0f);
}

// ── 4. 两层绘制（命令流 4 条；auto 圆角 = height/2；填充宽跟随呈现值）──

void TestPaintTwoLayers()
{
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);

	TestableProgressBar bar;
	bar.SetSize(400, 20);
	bar.Paint(ctx, 0, 0);

	EXPECT_EQ(commands.size(), size_t{ 4 });
	EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));          // 控件边界（Widget::Paint）

	const auto& track = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_NEAR(track.cornerRadius, 10.0f, kEps);   // auto 圆角 = height/2（§2.5 语义）
	EXPECT_NEAR(track.rect.width, 400.0f, kEps);

	const auto& fill = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_NEAR(fill.cornerRadius, 10.0f, kEps);         // 与轨道同心圆角（v1.3 方案 D）
	EXPECT_NEAR(fill.rect.width, 400.0f * 0.0f, kEps);   // 初始呈现值 0 → 填充宽 0

	EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[3]));           // 严格配对
}

// ── 5. 零进度填充（SetProgress(0) 构造即 0——no-op 不重启）─────

void TestPaintZeroProgress()
{
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);

	TestableProgressBar bar;
	bar.SetSize(400, 20);
	bar.SetProgress(0.0f);   // 构造即 0——no-op（目标 vs 目标），不启动新动画
	bar.Paint(ctx, 0, 0);

	const auto& fill = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_NEAR(fill.rect.width, 0.0f, kEps);
}

// ── 6. 样式覆盖（D7：覆盖后 ApplyTheme 不回退）──────────────

void TestSetStyle()
{
	const Color custom = Color::FromRGBA8(10, 20, 30);

	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	TestableProgressBar bar;

	bar.SetSize(400, 20);
	bar.SetProgress(0.5f);
	bar.SetStyle(ProgressBarStyleOverride{ .fillColor = custom });
	bar.Paint(ctx, 0, 0);

	const auto& fill = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_EQ(fill.color, custom);   // 覆盖后立即反映（§2.6——颜色不参与动画）

	CommandBuffer commands2;
	PaintContext ctx2(commands2, measurer);
	bar.ApplyTheme(GetDefaultTheme());   // D7：overridden 字段 Apply 被忽略
	bar.Paint(ctx2, 0, 0);

	const auto& fill2 = std::get<DrawRoundedRectCommand>(commands2[2]);
	EXPECT_EQ(fill2.color, custom);
}

// ── 7. 主题默认注入（构造后 Style == DefaultTheme 值）────────

void TestApplyTheme()
{
	TestableProgressBar bar;
	const ProgressBarStyle defaults = GetDefaultTheme().GetProgressBarStyle();

	EXPECT_EQ(bar.Style().trackColor.value,   defaults.trackColor.value);
	EXPECT_EQ(bar.Style().fillColor.value,    defaults.fillColor.value);
	EXPECT_EQ(bar.Style().cornerRadius.value, defaults.cornerRadius.value);
}

// ── 8. 动画推进（接缝注入；中间态存在 → 终值必达；不绑 easing 数值）──

void TestAnimationProgresses()
{
	TestPlatformWindow platform;
	AnimationManager manager(platform);
	TestableProgressBar bar;
	bar.SetTestAnimationManager(&manager);

	bar.SetProgress(1.0f);
	manager.Tick(1ms);   // 短 tick——只验「中间态存在」，不依赖具体时长实现（v1.1 GPT 修订）
	EXPECT_TRUE(bar.DisplayProgress() > 0.0f && bar.DisplayProgress() < 1.0f);

	manager.Tick(300ms);   // 终值必达（完成帧 onValue(final) 恰写 to——Animation.h 语义）
	EXPECT_EQ(bar.DisplayProgress(), 1.0f);
	EXPECT_EQ(bar.GetProgress(), 1.0f);
}

// ── 9. 替换式重启（核心不变量：替换动画 from == 替换瞬间呈现值）──

void TestAnimationReplacement()
{
	TestPlatformWindow platform;
	AnimationManager manager(platform);
	TestableProgressBar bar;
	bar.SetTestAnimationManager(&manager);

	bar.SetProgress(1.0f);
	manager.Tick(50ms);
	const float p1 = bar.DisplayProgress();
	EXPECT_TRUE(p1 > 0.0f && p1 < 1.0f);

	// 核心不变量（v1.1 GPT 修订）：SetProgress 只改目标、不动呈现——替换动画 from == p1
	bar.SetProgress(0.5f);
	EXPECT_EQ(bar.DisplayProgress(), p1);

	manager.Tick(1ms);   // 新动画从 p1 出发向 0.5 运动——未跳回 0
	EXPECT_TRUE(bar.DisplayProgress() > 0.0f);

	manager.Tick(300ms);
	EXPECT_EQ(bar.DisplayProgress(), 0.5f);
}

// ── 10. 同目标 no-op（判断键 target ↔ m_progress——不重启）────

void TestIdempotentTarget()
{
	// 无宿主：瞬时路径下重复 SetProgress 幂等
	TestableProgressBar bar;
	bar.SetProgress(0.5f);
	const float before = bar.GetProgress();
	bar.SetProgress(0.5f);
	EXPECT_EQ(bar.GetProgress(), before);

	// 接缝：完成一次动画后再次设置**同一目标** → 不启动新动画（HasActive 保持 false）
	TestPlatformWindow platform;
	AnimationManager manager(platform);
	bar.SetTestAnimationManager(&manager);

	bar.SetProgress(0.3f);
	manager.Tick(300ms);
	EXPECT_FALSE(manager.HasActive());

	bar.SetProgress(0.3f);   // 同目标（含 clamp 后相等）——no-op
	EXPECT_FALSE(manager.HasActive());
}

// ── 11. 填充几何跟随 Widget 宽度（resize 后填充宽 = w × 呈现值）──

void TestResizeFillGeometry()
{
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);

	TestableProgressBar bar;
	bar.SetProgress(0.25f);   // 无宿主——瞬时到位（呈现值 0.25）
	bar.SetSize(400, 20);
	bar.Paint(ctx, 0, 0);

	const auto& fill = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_NEAR(fill.rect.width, 400.0f * 0.25f, kEps);
}

} // anonymous namespace

void ECDI::Test::RegisterProgressBarTests()
{
	GetTestRegistry().Add("ProgressBar.DefaultValues",        &TestDefaultValues);
	GetTestRegistry().Add("ProgressBar.SetProgressClamp",     &TestSetProgressClamp);
	GetTestRegistry().Add("ProgressBar.SetPercent",           &TestSetPercent);
	GetTestRegistry().Add("ProgressBar.PaintTwoLayers",       &TestPaintTwoLayers);
	GetTestRegistry().Add("ProgressBar.PaintZeroProgress",    &TestPaintZeroProgress);
	GetTestRegistry().Add("ProgressBar.SetStyle",             &TestSetStyle);
	GetTestRegistry().Add("ProgressBar.ApplyTheme",           &TestApplyTheme);
	GetTestRegistry().Add("ProgressBar.AnimationProgresses",  &TestAnimationProgresses);
	GetTestRegistry().Add("ProgressBar.AnimationReplacement", &TestAnimationReplacement);
	GetTestRegistry().Add("ProgressBar.IdempotentTarget",     &TestIdempotentTarget);
	GetTestRegistry().Add("ProgressBar.ResizeFillGeometry",   &TestResizeFillGeometry);
}

}