#pragma once

#include "ECDI/Animation/Animation.h"
#include "ECDI/Animation/AnimationToken.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ECDI{

class PlatformWindow;   // 前置声明（能力接缝——manager 只持引用；cpp include 完整头）

/// @brief 动画管理器（9.6——per-Window 组合，Window 内表现层基础设施）
/// @details
/// 职责：时间推进、插值调度、easing 应用、动画生命周期。**不负责**状态、控件逻辑、渲染能力扩展。
/// 能力接缝（GPT 收紧）：只持 PlatformWindow&——启停 timer + 请求重绘两能力恰在契约上，
/// 类型上拿不到 Window&，操作 Window 内部无从发生。
///
/// 硬契约（详细设计 v1.1 §2.4，实现不得弱化）：
/// - 值回调式：Animation 不认识目标；onValue 由持有者写属性
/// - 替换式重启：同 token 再 Start = 旧动画静默移除（不调 onFinished），from 由调用方传当前呈现值
/// - onFinished 触发：正常到达终点才调；Replace / Cancel / token 失效一律不调
/// - 完成帧顺序：onValue(finalValue) → onFinished → 移除
/// - 重入安全：onValue / onFinished 内允许 Start / Cancel；本轮 Tick 启动的新动画下轮才推进
class AnimationManager{
public:

	// ── TimerId 保留段登记（d1：owner-held + 保留段——登记表见
	//    docs/phase9.6-animation-detailed-design.md §7；新 timer 必须更新该表）──
	//    保留段 1–15 框架保留：TextBox=1（kCaretBlinkTimer，不迁移）、Animation=2（本常量）

	static constexpr int kAnimationTickTimer = 2;	///< 动画统一 tick 定时器 ID（Window 级唯一）

	static constexpr unsigned int kAnimationTickIntervalMs = 16;	///< tick 间隔（约 60fps；间隔只决定推进频率，不参与插值）

	explicit AnimationManager(PlatformWindow& platformWindow) noexcept;

	/// @brief 析构（动画条目随之销毁；token 析构标脏语义不依赖 manager 存活——生命周期不变量）
	~AnimationManager();

	AnimationManager(const AnimationManager&) = delete;

	AnimationManager& operator=(const AnimationManager&) = delete;

	/// @brief 启动 float 值动画（d6 值回调式 + d7 token 替换键）
	/// @param token      替换判定键（同 token 再 Start = 替换重启）
	/// @param from       起始值——**调用方传当前呈现值**（替换式重启语义的兑现点）
	/// @param to         目标值
	/// @param duration   总时长（真实 elapsed 对比，非 tick 计数）
	/// @param easing     缓动曲线
	/// @param onValue    值回调（每 tick 调用，含最终帧）
	/// @param onFinished 完成回调（仅正常到达终点时调用；可选）
	void Start(AnimationToken& token, float from, float to,
	           std::chrono::milliseconds duration, Easing easing,
	           std::function<void(float)> onValue,
	           std::function<void()> onFinished = nullptr);

	/// @brief 启动 Color 值动画（RGBA 四通道各自 lerp；其余同 float 重载）
	void Start(AnimationToken& token, const Color& from, const Color& to,
	           std::chrono::milliseconds duration, Easing easing,
	           std::function<void(const Color&)> onValue,
	           std::function<void()> onFinished = nullptr);

	/// @brief 显式取消动画（不调 onFinished；Tick 重入期间延迟删除）
	void Cancel(AnimationToken& token);

	/// @brief 推进所有活动动画（d3+d9：elapsed 由调用方传入——Window 用 steady_clock
	/// 算真实值，测试传假值确定性推进；manager 不持时钟）
	/// @details 不变量：① 失效/取消的动画移除且不调 onFinished
	/// ② 完成动画 onValue(final) → onFinished → 移除
	/// ③ 遍历安全：回调内 Start/Cancel 合法，本轮新动画不推进
	/// ④ 有任何 onValue 实际执行 → 聚合一次 Invalidate；活跃归零 → StopTimer（空闲零开销）
	void Tick(std::chrono::milliseconds elapsed);

	/// @brief 是否有活动动画（测试断言用）
	bool HasActive() const noexcept;

	/// @brief 设置 timer 启动钩子（Window 用它重置 elapsed 锚点——首次 tick 的 elapsed
	/// 从 timer 启动时刻起算；测试替身场景可忽略）
	void SetOnTimerStarted(std::function<void()> callback);

private:

	/// @brief 活动动画条目（堆稳定——vector 扩容/重入回调期间 Entry 地址不变）
	struct Entry{

		std::uint64_t id;									///< 动画 id（token 关联键）

		std::shared_ptr<AnimationTokenState> state;			///< token 共享状态块（弱所有权共享——非 Widget 指针）

		std::unique_ptr<Animation> animation;				///< 值动画本体

		std::function<void()> onFinished;					///< 完成回调

		bool cancelled = false;								///< Tick 重入期间被 Cancel/Replace 标记——延迟删除

	};

	/// @brief 解绑 token 关联的动画（替换/Cancel 共用路径；不调 onFinished）
	void DetachAnimation(AnimationTokenState& state);

	/// @brief 注册动画公共路径（替换检查 + id 分配 + timer 启动）
	void StartInternal(AnimationToken& token, std::unique_ptr<Animation> animation,
	                   std::function<void()> onFinished);

	/// @brief 稳定删除条目（保持顺序——Tick 推进顺序确定性）
	void EraseAt(size_t index);

	PlatformWindow& m_platformWindow;	///< 能力接缝（StartTimer/StopTimer/Invalidate——唯一依赖）

	std::vector<std::unique_ptr<Entry>> m_active;	///< 活动动画（unique_ptr 保 Entry 堆稳定——重入安全）

	std::uint64_t m_nextId = 1;			///< 动画 id 分配器

	bool m_timerRunning = false;		///< tick timer 运行标志（避免重复 StartTimer 重置周期）

	bool m_ticking = false;				///< Tick 执行中标志（重入期间 Cancel/Replace 延迟删除）

	std::function<void()> m_onTimerStarted;	///< timer 启动钩子（Window 重置 elapsed 锚点）

};

}

