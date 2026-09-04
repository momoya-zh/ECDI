#include "ECDI/Animation/AnimationManager.h"

#include "ECDI/Platform/PlatformWindow.h"

#include <cstddef>
#include <utility>

namespace ECDI{

AnimationManager::AnimationManager(PlatformWindow& platformWindow) noexcept
	: m_platformWindow(platformWindow){}

AnimationManager::~AnimationManager(){

	// token 析构标脏语义不依赖本对象存活（生命周期不变量）——无需遍历通知；
	// 条目销毁后，遗留 token 的析构仅置 alive=false（no-op 安全）

	if (m_timerRunning){

		m_platformWindow.StopTimer(kAnimationTickTimer);   // 幂等，防御性停止

	}

}

// ── 启动（d6 值回调式 + d7 token 替换键）────────────────────────────

void AnimationManager::Start(AnimationToken& token, float from, float to,
                             std::chrono::milliseconds duration, Easing easing,
                             std::function<void(float)> onValue,
                             std::function<void()> onFinished){

	StartInternal(
		token,
		std::make_unique<FloatAnimation>(from, to, duration, easing, std::move(onValue)),
		std::move(onFinished));

}

void AnimationManager::Start(AnimationToken& token, const Color& from, const Color& to,
                             std::chrono::milliseconds duration, Easing easing,
                             std::function<void(const Color&)> onValue,
                             std::function<void()> onFinished){

	StartInternal(
		token,
		std::make_unique<ColorAnimation>(from, to, duration, easing, std::move(onValue)),
		std::move(onFinished));

}

void AnimationManager::StartInternal(AnimationToken& token, std::unique_ptr<Animation> animation,
                                     std::function<void()> onFinished){

	// token 状态块按需创建（移动后源 token 复用场景防御）
	if (token.m_state == nullptr){

		token.m_state = std::make_shared<AnimationTokenState>();

	}

	// 替换式重启：同 token 旧动画静默移除（不调 onFinished——onFinished 触发契约）
	DetachAnimation(*token.m_state);

	const bool wasEmpty = m_active.empty();

	const std::uint64_t id = m_nextId++;

	token.m_state->animationId = id;

	auto entry = std::make_unique<Entry>();

	entry->id = id;
	entry->state = token.m_state;
	entry->animation = std::move(animation);
	entry->onFinished = std::move(onFinished);

	m_active.push_back(std::move(entry));

	// 空闲 → 活跃：启动统一 tick timer（0 → 1 才启动，避免重复调用重置周期）
	if (wasEmpty){

		m_platformWindow.StartTimer(kAnimationTickTimer, kAnimationTickIntervalMs);

		m_timerRunning = true;

		if (m_onTimerStarted){

			m_onTimerStarted();   // Window 重置 elapsed 锚点（首次 tick 的 elapsed 从此刻起算）

		}

	}

}

// ── 取消（d5——不调 onFinished；Tick 重入期间延迟删除）────────────────

void AnimationManager::Cancel(AnimationToken& token){

	if (token.m_state == nullptr || token.m_state->animationId == 0){

		return;   // 无关联动画——幂等 no-op

	}

	DetachAnimation(*token.m_state);

}

void AnimationManager::DetachAnimation(AnimationTokenState& state){

	if (state.animationId == 0){

		return;

	}

	for (size_t i = 0; i < m_active.size(); ++i){

		if (m_active[i]->id == state.animationId){

			if (m_ticking){

				m_active[i]->cancelled = true;   // 延迟删除——遍历安全（重入契约）

			}
			else{

				m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(i));

			}

			break;

		}

	}

	state.animationId = 0;

}

// ── 推进（d3+d9：elapsed 参数化；硬契约见类注释）────────────────────

void AnimationManager::Tick(std::chrono::milliseconds elapsed){

	if (m_active.empty()){

		return;

	}

	m_ticking = true;

	// 重入契约：本轮只推进 Tick 开始时已存在的动画——tickEnd 之后追加的（回调内 Start 的）不在本轮
	size_t tickEnd = m_active.size();

	size_t i = 0;

	bool anyApplied = false;

	while (i < tickEnd){

		Entry& entry = *m_active[i];

		// ① 失效/取消（token 析构标脏 / 重入期间 Cancel）→ 移除，不调 onFinished
		if (entry.cancelled || !entry.state->alive){

			EraseAt(i);

			--tickEnd;

			continue;   // 稳定删除：下一元素左移到 i，i 不增

		}

		// ② 推进（Advance 每次必触发 onValue——含最终帧）
		anyApplied = true;

		const bool finished = entry.animation->Advance(elapsed);

		// ③ 推进期间被 Cancel（含自身 onValue 内）——移除，不调 onFinished
		//（推进后立即检查：非完成路径的自身取消也要当帧出清，HasActive 即时反映）
		if (entry.cancelled){

			EraseAt(i);

			--tickEnd;

			continue;

		}

		if (!finished){

			++i;

			continue;

		}

		// ④ 完成：解绑 token → 移除 → onFinished（重入安全——entry 已不在列表，
		// onFinished 内 Start/Cancel 对 m_active 的修改不影响本循环）
		entry.state->animationId = 0;

		std::function<void()> onFinished = std::move(entry.onFinished);

		entry.cancelled = true;

		EraseAt(i);

		--tickEnd;

		if (onFinished){

			onFinished();

		}

	}

	m_ticking = false;

	// ⑤ 失效聚合：本轮有任何 onValue 实际执行 → 一次重绘请求
	if (anyApplied){

		m_platformWindow.Invalidate();

	}

	// ⑥ 活跃归零 → 停 timer（空闲零开销）
	if (m_active.empty() && m_timerRunning){

		m_platformWindow.StopTimer(kAnimationTickTimer);

		m_timerRunning = false;

	}

}

bool AnimationManager::HasActive() const noexcept{

	return !m_active.empty();

}

void AnimationManager::SetOnTimerStarted(std::function<void()> callback){

	m_onTimerStarted = std::move(callback);

}

void AnimationManager::EraseAt(size_t index){

	m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(index));

}

}

