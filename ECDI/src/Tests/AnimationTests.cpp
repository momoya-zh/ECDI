#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Animation/Easing.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Platform/PlatformRenderContext.h"
#include "ECDI/Core/Size.h"

#include <chrono>
#include <string>
#include <vector>

namespace ECDI{

namespace{

using namespace std::chrono_literals;

// ── 测试替身：PlatformWindow（7.1 抽象红利——timer 启停/Invalidate 调用序列可观测）──

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

	void StartTimer(int timerId, unsigned int intervalMs) override{

		++startCount;

		lastTimerId = timerId;

		lastIntervalMs = intervalMs;

		timerRunning = true;

	}

	void StopTimer(int timerId) override{

		++stopCount;

		timerRunning = false;

	}

	PlatformRenderContext m_context;	///< 空基类可实例化（无纯虚）——测试替身直接持有

	int startCount = 0;
	int stopCount = 0;
	int invalidateCount = 0;

	int lastTimerId = 0;
	unsigned int lastIntervalMs = 0;
	bool timerRunning = false;

};

// ── 1. Easing：端点 + 曲线抽样 ─────────────────────────────

void TestEasingEndpoints(){
	EXPECT_NEAR(ApplyEasing(Easing::Linear, 0.0f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::Linear, 1.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 0.0f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 1.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, 0.0f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, 1.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.0f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 1.0f), 1.0f, 1e-6);
}

void TestEasingCurves(){
	EXPECT_NEAR(ApplyEasing(Easing::Linear, 0.5f), 0.5f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 0.5f), 0.25f, 1e-6);      // 慢→快
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, 0.5f), 0.75f, 1e-6);     // 快→慢
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.25f), 0.125f, 1e-6); // 前半段 = 2t²
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.5f), 0.5f, 1e-6);    // 中点连续
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.75f), 0.875f, 1e-6); // 后半段 = 1-2(1-t)²
}

// ── 2/3. 插值 + Tick 推进（d9 参数化——确定性推进）──────────────

void TestFloatAnimationTickSequence(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;

	std::vector<float> values;
	bool finished = false;

	manager.Start(token, 0.0f, 100.0f, 100ms, Easing::Linear,
		[&](float v){ values.push_back(v); },
		[&]{ finished = true; });

	// 中间值序列：Linear 下 40ms → 40、80ms → 80、120ms → clamp 到 100（终值）
	manager.Tick(40ms);
	manager.Tick(40ms);
	manager.Tick(40ms);

	EXPECT_EQ(values.size(), size_t{ 3 });
	EXPECT_NEAR(values[0], 40.0f, 1e-4);
	EXPECT_NEAR(values[1], 80.0f, 1e-4);
	EXPECT_NEAR(values[2], 100.0f, 1e-4);   // 终值必须经过 onValue（GPT 评审 ③）

	EXPECT_TRUE(finished);                  // onFinished 恰调一次（在 onValue(100) 之后）
	EXPECT_FALSE(manager.HasActive());      // 完成即移除
}

// ── 4. 替换式重启（d7 token——旧动画不调 onFinished）─────────────

void TestReplaceRestart(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;

	std::vector<float> valuesA;
	std::vector<float> valuesB;
	bool aFinished = false;
	bool bFinished = false;

	manager.Start(token, 0.0f, 100.0f, 100ms, Easing::Linear,
		[&](float v){ valuesA.push_back(v); },
		[&]{ aFinished = true; });

	manager.Tick(30ms);   // A 推进到 30

	// 替换：from = 当前呈现值 30（调用方兑现点）——旧 A 静默移除
	manager.Start(token, 30.0f, 0.0f, 100ms, Easing::Linear,
		[&](float v){ valuesB.push_back(v); },
		[&]{ bFinished = true; });

	manager.Tick(100ms);   // B 直接到达终点

	EXPECT_FALSE(aFinished);          // 被替换的 A 不得执行完成逻辑（onFinished 触发契约）
	EXPECT_TRUE(bFinished);
	EXPECT_EQ(valuesA.size(), size_t{ 1 });   // A 只在被替换前推进过一次
	EXPECT_FALSE(valuesB.empty());
	EXPECT_NEAR(valuesB.back(), 0.0f, 1e-4);  // B 终值 = 新 to
	EXPECT_FALSE(manager.HasActive());
}

// ── 5. token 析构（d5 弱引用保护——失效不调 onFinished）──────────

void TestTokenDestruction(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	std::vector<float> values;
	bool finished = false;

	{
		AnimationToken token;

		manager.Start(token, 0.0f, 100.0f, 100ms, Easing::Linear,
			[&](float v){ values.push_back(v); },
			[&]{ finished = true; });

		manager.Tick(30ms);
	}

	// token 析构（标脏 alive=false）——下次 Tick 清理，不调 onFinished、不再 onValue
	manager.Tick(30ms);

	EXPECT_FALSE(finished);
	EXPECT_EQ(values.size(), size_t{ 1 });   // 析构前的推进保留，之后不再推进
	EXPECT_FALSE(manager.HasActive());
}

// ── 6. Cancel（不调 onFinished；幂等）────────────────────────

void TestCancel(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;
	bool finished = false;

	manager.Start(token, 0.0f, 100.0f, 100ms, Easing::Linear,
		[&](float){},
		[&]{ finished = true; });

	manager.Cancel(token);

	EXPECT_FALSE(manager.HasActive());   // 立即移除（非 Tick 期）

	manager.Cancel(token);               // 幂等 no-op

	manager.Tick(50ms);

	EXPECT_FALSE(finished);              // 被取消的动画不得执行完成逻辑
}

// ── 7. timer 启停（0→1 启动、归零停止、重复 Start 不重置）───────

void TestTimerStartStop(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken tokenA;
	AnimationToken tokenB;

	// 首个 Start → 启动 timer（kAnimationTickTimer=2、16ms）
	manager.Start(tokenA, 0.0f, 1.0f, 1000ms, Easing::Linear, [&](float){});
	EXPECT_EQ(platform.startCount, 1);
	EXPECT_EQ(platform.lastTimerId, AnimationManager::kAnimationTickTimer);
	EXPECT_EQ(platform.lastIntervalMs, AnimationManager::kAnimationTickIntervalMs);
	EXPECT_TRUE(platform.timerRunning);

	// 第二个 Start → 不重复启动（防 StartTimer 重置周期）
	manager.Start(tokenB, 0.0f, 1.0f, 1000ms, Easing::Linear, [&](float){});
	EXPECT_EQ(platform.startCount, 1);

	// 全部完成 → 停 timer（空闲零开销）
	manager.Tick(1000ms);
	EXPECT_EQ(platform.stopCount, 1);
	EXPECT_FALSE(platform.timerRunning);
	EXPECT_FALSE(manager.HasActive());

	// 空闲后再 Start → 重新启动
	manager.Start(tokenA, 0.0f, 1.0f, 1000ms, Easing::Linear, [&](float){});
	EXPECT_EQ(platform.startCount, 2);
}

// ── 8. 回调重入（d 契约——onFinished 内 Start 下轮才推进；onValue 内 Cancel 不调 onFinished）──

void TestReentrantStartInOnFinished(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;
	bool secondAdvanced = false;
	bool firstDone = false;

	// onFinished 内同 token 启动第二段（解绑已完成——Start 正常注册）
	manager.Start(token, 0.0f, 1.0f, 50ms, Easing::Linear,
		[&](float){},
		[&]{
			firstDone = true;

			manager.Start(token, 0.0f, 1.0f, 50ms, Easing::Linear,
				[&](float){ secondAdvanced = true; });
		});

	manager.Tick(50ms);   // 第一段完成 → onFinished 注册第二段——本轮不得推进它

	EXPECT_TRUE(firstDone);
	EXPECT_FALSE(secondAdvanced);   // 重入契约：本轮 Start 的新动画下轮才推进
	EXPECT_TRUE(manager.HasActive());

	manager.Tick(50ms);   // 第二段推进

	EXPECT_TRUE(secondAdvanced);
}

void TestCancelSelfInOnValue(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;
	std::vector<float> values;
	bool finished = false;

	// onValue 内 Cancel 自己——移除且不调 onFinished（防「已取消却执行收尾」）
	manager.Start(token, 0.0f, 10.0f, 100ms, Easing::Linear,
		[&](float v){
			values.push_back(v);

			manager.Cancel(token);
		},
		[&]{ finished = true; });

	manager.Tick(25ms);

	EXPECT_EQ(values.size(), size_t{ 1 });   // 仅第一次 onValue
	EXPECT_FALSE(finished);
	EXPECT_FALSE(manager.HasActive());
}

// ── 9. Color 插值 + 终值顺序 ────────────────────────────────

void TestColorAnimationEndpoints(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;

	std::vector<Color> values;
	Color finalValue;

	manager.Start(token, Color::Black(), Color::White(), 100ms, Easing::Linear,
		[&](const Color& c){ values.push_back(c); finalValue = c; });

	manager.Tick(50ms);   // 中点 = 0.5 各通道
	EXPECT_NEAR(values.back().r, 0.5f, 1e-4);
	EXPECT_NEAR(values.back().g, 0.5f, 1e-4);
	EXPECT_NEAR(values.back().b, 0.5f, 1e-4);

	manager.Tick(50ms);   // 终点
	EXPECT_NEAR(finalValue.r, 1.0f, 1e-4);
	EXPECT_NEAR(finalValue.a, 1.0f, 1e-4);
}

// ── 10. 生命周期不变量（manager 先亡、token 后析构 = 安全 no-op）──

void TestManagerDiesBeforeToken(){
	AnimationToken* token = new AnimationToken();   // token 比 manager 长寿

	{
		TestPlatformWindow platform;
		AnimationManager manager(platform);

		manager.Start(*token, 0.0f, 1.0f, 100ms, Easing::Linear, [&](float){});
	}

	// manager 已析构——token 析构标脏不依赖 manager 存活（不崩溃即通过）
	delete token;
}

void TestZeroDuration(){
	TestPlatformWindow platform;
	AnimationManager manager(platform);

	AnimationToken token;
	std::vector<float> values;
	bool finished = false;

	// 零时长 = 首次 Tick 直达终值（NormalizeProgress 防除零）
	manager.Start(token, 5.0f, 9.0f, 0ms, Easing::Linear,
		[&](float v){ values.push_back(v); },
		[&]{ finished = true; });

	manager.Tick(16ms);

	EXPECT_EQ(values.size(), size_t{ 1 });
	EXPECT_NEAR(values[0], 9.0f, 1e-4);
	EXPECT_TRUE(finished);
}

} // anonymous namespace

void ECDI::Test::RegisterAnimationTests()
{
    GetTestRegistry().Add("Animation.EasingEndpoints", &TestEasingEndpoints);
    GetTestRegistry().Add("Animation.EasingCurves", &TestEasingCurves);
    GetTestRegistry().Add("Animation.FloatTickSequence", &TestFloatAnimationTickSequence);
    GetTestRegistry().Add("Animation.ReplaceRestart", &TestReplaceRestart);
    GetTestRegistry().Add("Animation.TokenDestruction", &TestTokenDestruction);
    GetTestRegistry().Add("Animation.Cancel", &TestCancel);
    GetTestRegistry().Add("Animation.TimerStartStop", &TestTimerStartStop);
    GetTestRegistry().Add("Animation.ReentrantStartInOnFinished", &TestReentrantStartInOnFinished);
    GetTestRegistry().Add("Animation.CancelSelfInOnValue", &TestCancelSelfInOnValue);
    GetTestRegistry().Add("Animation.ColorEndpoints", &TestColorAnimationEndpoints);
    GetTestRegistry().Add("Animation.ManagerDiesBeforeToken", &TestManagerDiesBeforeToken);
    GetTestRegistry().Add("Animation.ZeroDuration", &TestZeroDuration);
}

} // namespace ECDI
