#pragma once

#include <cstdint>
#include <memory>

namespace ECDI{

class AnimationManager;

/// @brief Token 共享状态块（9.6 d5+d7——共享生命周期状态块，**非**弱 Widget 指针）
/// @details 生命周期不变量（GPT 评审 ① 写死）：
/// - 「析构标脏 / Tick 清理」：token 析构只置 alive=false，**不依赖 manager 存活**；
///   动画的实际移除发生在下一次 Tick 的 alive 检查
/// - 本状态块被 Token（shared_ptr）与 AnimationManager 的活动条目（shared_ptr）共享持有——
///   无任何一方指向另一方的裸所有权；不存在 manager↔token 生命周期环
/// - 无 AnimationManager 指针字段：token 永远不回调 manager（最彻底的"可失效引用"）
struct AnimationTokenState{

	std::uint64_t animationId = 0;	///< 关联的活跃动画 id（0 = 无关联）

	bool alive = true;				///< token 析构置 false（标脏；Tick 时清理）

};

/// @brief 动画令牌（9.6——替换判定键 + RAII 生命周期保护，一个机制覆盖两个议题）
/// @details 使用契约：
/// - 持有者为每个动画属性持一个 token 成员（如 Button 的背景色 token）
/// - 同 token 再 Start = 替换式重启（旧动画静默移除、**不调 onFinished**；
///   新 from 由调用方传当前呈现值）
/// - token 析构 = 标脏失效 → 下次 Tick 动画被移除（**不调 onFinished**）——
///   回调永不打在死对象上（弱引用保护）
/// - 禁复制、允许移动（句柄语义——移动后源 token 与动画脱钩）
class AnimationToken{
public:

	AnimationToken() = default;

	/// @brief 析构标脏（ alive=false；不调用 manager——生命周期不变量）
	~AnimationToken(){

		if (m_state != nullptr){

			m_state->alive = false;

		}

	}

	AnimationToken(const AnimationToken&) = delete;

	AnimationToken& operator=(const AnimationToken&) = delete;

	AnimationToken(AnimationToken&& other) noexcept
		: m_state(std::move(other.m_state)){}

	AnimationToken& operator=(AnimationToken&& other) noexcept{

		if (this != &other){

			// 自身已关联动画时先标脏（移动赋值不丢失失效语义）
			if (m_state != nullptr){

				m_state->alive = false;

			}

			m_state = std::move(other.m_state);

		}

		return *this;

	}

private:

	friend class AnimationManager;

	std::shared_ptr<AnimationTokenState> m_state;	///< 共享状态块（移动后为 nullptr；Start 时按需创建）

};

}

