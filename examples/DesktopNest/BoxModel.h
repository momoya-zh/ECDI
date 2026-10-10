#pragma once

#include "ECDI/Core/Rect.h"   // placement（收编时保存的对方窗口位置）

#include <cstddef>
#include <functional>
#include <string>
#include <utility>   // std::pair（订阅表条目）——头自包含（C-M1-23：不依赖传递包含）
#include <vector>

namespace ECDI::DesktopNest {

/// @brief 框 ID——★ 与成员同一 ID 空间（详设 §1.2）：收编的框即成员，
///        「成员表」是「框表」在合并框内的投影（需求稿 §2.2 两层结构的 M1 读法）。
/// @details ★ id 即 m_boxes 下标：Box 不销毁（协调器持有至退出——C-M1-10①），
///          id 自 0 单调分配 ⇒ GetBox(box) == m_boxes[box]。
using BoxId = int;

/// @brief 框状态：TopLevel = 独立窗口；Merged = 被收编（作为他人的成员）。
enum class BoxState { TopLevel, Merged };

/// @brief 变更通知种类（详设 §1.3——按「对接收方意味着什么」分种，
///        非按「什么操作」分种：同一变更的两个接收方反应不同）。
enum class ModelChange {
	Collected,        ///< 收编方：我吸收了一个成员
	Absorbed,         ///< 被收编方：我成为他人成员
	MemberDetached,   ///< 源框：我少了一个成员
	Released,         ///< 被拆出成员：我恢复独立（旧树随旧窗销毁——延迟到泵清理点，C-M1-20；无本地操作）
	Activated         ///< 切换当前成员
};

/// @brief 框记录（纯数据——P3 单向数据流的唯一事实源）。
struct Box {
	BoxId id = -1;                ///< 框 ID（== 在 m_boxes 中的下标）
	std::string title;            ///< 框自身标题（派生标题见 C-M1-17）
	BoxState state = BoxState::TopLevel;   ///< 独立 / 被收编
	std::vector<BoxId> members;   ///< 收编的框（= 成员）ID；空 = 独立框
	BoxId active = -1;            ///< 当前成员（仅 Merged 有意义）
	Rect placement{};             ///< 收编时保存的对方窗口位置（空 Rect = 无记忆）
};

/// @brief 框模型（纯数据层——ModelProbeTests 先例：demo 逻辑类可无窗构造）。
/// @details 变更 = 纯数据操作（C-M1-6：几何一律作数据参数传入，Model 不读窗口）；
///          变更后经订阅通知（C-M1-8：注册/注销成对 + 快照派发 + handler 只关自己窗口）。
class BoxModel {
public:
	using ChangeCallback = std::function<void(BoxId /*box*/, ModelChange /*change*/)>;

	/// @brief 订阅句柄（C-M1-11：std::function 不可比较 ⇒ 注销按句柄；单调递增、不复用）。
	using Subscription = std::size_t;

	/// @brief 新增独立框（标题即显示标题）；返回分配的 ID（自 0 单调分配）。
	BoxId AddBox(std::string title);

	/// @brief 新建合并框（K6 冻结初始态用——成员记录一并创建）。
	/// @param memberTitles 成员标题列表；**非空前置**（C-M1-24：空列表会使 active 取 front 成 UB）
	/// @pre !memberTitles.empty()（Debug FRAMEWORK_ASSERT）
	/// @note placement 无记忆 ⇒ 成员日后拆出走级联偏移（§9-6 拍板）。
	BoxId AddMergedBox(std::string title, std::vector<std::string> memberTitles);

	/// @brief 收编：source 作为成员并入 target。
	/// @param sourcePlacement 调用方在调用前读 source 窗口位置（Phase 31 GetBounds——
	///        「记 placement 要读」）；Model 只存数据（C-M1-6）。
	/// @pre target 与 source 均为有效 id 且**均 TopLevel**（Merged 框不可再被收编），
	///      且 source != target（C-M1-24；弹层「加入成员」只列独立框——UI 门控同源）
	void Collect(BoxId target, BoxId source, const Rect& sourcePlacement);

	/// @brief 拆出「当前成员」：active 从 members 移除并恢复 TopLevel（placement 记录保留）。
	/// @pre CanDetach(box)（members.size() > 1——弹层拆出项的可用性门控），
	///      且 box 有效、active 指向本框成员（C-M1-24）
	void Detach(BoxId box);

	/// @brief 切换当前成员（active 指向换人；框尺寸不变——内容重建由通知驱动）。
	/// @pre member ∈ GetBox(box).members（不能切到不属于本框的成员）、二者 id 均有效（C-M1-24）
	void SetActive(BoxId box, BoxId member);

	// ── 查询 ────────────────────────────────────────

	/// @brief 取框记录（id 即下标——见 BoxId 注释）
	/// @note ★ 引用寿命（C-M1-24 / 复评 S4）：返回引用指向内部 std::vector<Box> 元素
	///       ⇒ AddBox/AddMergedBox 的 push_back 一旦扩容即**全部失效**；调用点须
	///       **即取即用**（或按值拷贝）——不得跨越 AddBox/AddMergedBox 持有。
	[[nodiscard]] const Box& GetBox(BoxId box) const;

	/// @brief 「加入成员…」列表 = 全部独立框（含 owner 自身——由弹层排除自身）。
	[[nodiscard]] std::vector<BoxId> GetTopLevelBoxes() const;

	/// @brief 拆出项可用性判据（members.size() > 1）
	[[nodiscard]] bool CanDetach(BoxId box) const;

	// ── 订阅（C-M1-8：注册/注销成对；派发 = 快照——注销不影响本轮）────

	Subscription AddHandler(ChangeCallback callback);

	/// @brief 按句柄注销（幂等：未知句柄无副作用）
	void RemoveHandler(Subscription subscription);

private:
	/// @brief 快照派发（C-M1-8②）：值拷贝订阅表后遍历——注销只改活列表、不影响本轮
	void Notify(BoxId box, ModelChange change);

	std::vector<Box> m_boxes;                                        ///< 框表（id 即下标）
	std::vector<std::pair<Subscription, ChangeCallback>> m_handlers; ///< 订阅表（快照派发的源）
	Subscription m_nextSubscription = 0;                             ///< 句柄分配器（单调递增不复用）
	BoxId m_nextId = 0;                                              ///< 框 ID 分配器（单调递增）
};

}   // namespace ECDI::DesktopNest
