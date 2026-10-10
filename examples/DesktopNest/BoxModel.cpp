#include "BoxModel.h"   // .cpp 对应头（第一）

#include "ECDI/Core/ECDIAssert.h"   // FRAMEWORK_ASSERT（前置条件口径——C-M1-24）

#include <algorithm>   // std::find_if（RemoveHandler）/ std::remove_if
#include <utility>     // std::move

namespace ECDI::DesktopNest {

BoxId BoxModel::AddBox(std::string title){

	const BoxId id = m_nextId++;

	m_boxes.push_back(Box{ id, std::move(title), BoxState::TopLevel, {}, -1, Rect{} });

	return id;
}

BoxId BoxModel::AddMergedBox(std::string title, std::vector<std::string> memberTitles){

	// 非空前置（C-M1-24）：空列表会使下方 active 取 front() 成为 UB
	FRAMEWORK_ASSERT(!memberTitles.empty());

	if (memberTitles.empty()){

		// Release 兜底（沿 Application.cpp:139-146 口径：Debug 断言暴露缺陷 + Release 分支避免 UB）
		return -1;
	}

	// ★ 四步固定顺序（详设 △1 / 复评 S4）——不依赖「先建成员还是先建框」的脆弱插入序：
	// ① 先一次性确定全部 ID（自 0 单调、不跳号 ⇒ 分配完成时 id == 下标恒成立）
	const BoxId boxId = m_nextId;
	const std::size_t memberCount = memberTitles.size();

	std::vector<BoxId> memberIds;
	memberIds.reserve(memberCount);

	for (std::size_t i = 0; i < memberCount; ++i){

		memberIds.push_back(boxId + 1 + static_cast<BoxId>(i));
	}

	// ② 再构造全部记录（合并框在前、成员随后）——此步一次性 push_back，期间不对外暴露引用
	m_boxes.push_back(Box{ boxId, std::move(title), BoxState::TopLevel, {}, -1, Rect{} });

	for (std::size_t i = 0; i < memberCount; ++i){

		// 成员：state = Merged、placement 空（= 无记忆 ⇒ 日后拆出走级联）
		m_boxes.push_back(Box{ memberIds[i], std::move(memberTitles[i]), BoxState::Merged, {}, -1, Rect{} });
	}

	// ③ 最后回填关系（此步不再扩容 ⇒ 元素引用稳定）
	Box& box = m_boxes[static_cast<std::size_t>(boxId)];

	box.members = memberIds;
	box.active = memberIds.front();

	// ④ 分配器前移到已用区间之后（完成状态初始化后才返回——返回即对外可见）
	m_nextId = boxId + 1 + static_cast<BoxId>(memberCount);

	return boxId;
}

void BoxModel::Collect(BoxId target, BoxId source, const Rect& sourcePlacement){

	// 前置条件（C-M1-24）：Debug 断言暴露缺陷；Release 由 UI 门控保证（弹层只列合法项）
	FRAMEWORK_ASSERT(target >= 0 && target < static_cast<BoxId>(m_boxes.size()));
	FRAMEWORK_ASSERT(source >= 0 && source < static_cast<BoxId>(m_boxes.size()));
	FRAMEWORK_ASSERT(target != source);
	FRAMEWORK_ASSERT(m_boxes[static_cast<std::size_t>(target)].state == BoxState::TopLevel);
	FRAMEWORK_ASSERT(m_boxes[static_cast<std::size_t>(source)].state == BoxState::TopLevel);

	if (target < 0 || target >= static_cast<BoxId>(m_boxes.size())
	    || source < 0 || source >= static_cast<BoxId>(m_boxes.size())
	    || target == source){

		return;   // Release 兜底：不执行非法变更
	}

	Box& targetBox = m_boxes[static_cast<std::size_t>(target)];
	Box& sourceBox = m_boxes[static_cast<std::size_t>(source)];

	// 数据先一致，再通知（C-M1-10 派发顺序契约：handler 被调用时能查到已完成的变更）
	targetBox.members.push_back(source);

	// ★ 实施勘误 E-2（2026-10-09，行为探针发现）：**首次收编须同时确定 active**——
	//   否则 target.members 非空而 active == -1 ⇒ 派生标题（C-M1-17）退化、Detach 读 active
	//   命中前置失败。已有 active 时不改（保持用户当前选中项）。
	if (targetBox.active < 0){

		targetBox.active = source;
	}

	sourceBox.state = BoxState::Merged;
	sourceBox.placement = sourcePlacement;

	Notify(target, ModelChange::Collected);
	Notify(source, ModelChange::Absorbed);
}

void BoxModel::Detach(BoxId box){

	// 前置条件（C-M1-24）：members.size() > 1（弹层拆出项可用性门控同源）
	FRAMEWORK_ASSERT(box >= 0 && box < static_cast<BoxId>(m_boxes.size()));
	FRAMEWORK_ASSERT(CanDetach(box));

	if (box < 0 || box >= static_cast<BoxId>(m_boxes.size()) || !CanDetach(box)){

		return;   // Release 兜底
	}

	Box& owner = m_boxes[static_cast<std::size_t>(box)];
	const BoxId member = owner.active;

	FRAMEWORK_ASSERT(member >= 0 && member < static_cast<BoxId>(m_boxes.size()));

	if (member < 0 || member >= static_cast<BoxId>(m_boxes.size())){

		return;
	}

	// 从 members 移除 active
	owner.members.erase(std::remove(owner.members.begin(), owner.members.end(), member),
	                    owner.members.end());

	// active 保有效指向（移除后 members 必非空——前置保证 > 1）
	owner.active = owner.members.front();

	// 被拆成员恢复独立；placement 记录保留（拆出时优先恢复——§9-6）
	Box& released = m_boxes[static_cast<std::size_t>(member)];

	released.state = BoxState::TopLevel;

	Notify(box, ModelChange::MemberDetached);
	Notify(member, ModelChange::Released);
}

void BoxModel::SetActive(BoxId box, BoxId member){

	FRAMEWORK_ASSERT(box >= 0 && box < static_cast<BoxId>(m_boxes.size()));

	if (box < 0 || box >= static_cast<BoxId>(m_boxes.size())){

		return;
	}

	const Box& owner = m_boxes[static_cast<std::size_t>(box)];
	const bool isMember = std::find(owner.members.begin(), owner.members.end(), member)
	                      != owner.members.end();

	FRAMEWORK_ASSERT(isMember);

	if (!isMember){

		return;   // Release 兜底：不能切到不属于本框的成员
	}

	m_boxes[static_cast<std::size_t>(box)].active = member;

	Notify(box, ModelChange::Activated);
}

const Box& BoxModel::GetBox(BoxId box) const{

	return m_boxes[static_cast<std::size_t>(box)];
}

std::vector<BoxId> BoxModel::GetTopLevelBoxes() const{

	std::vector<BoxId> result;

	for (const Box& box : m_boxes){

		if (box.state == BoxState::TopLevel){

			result.push_back(box.id);
		}
	}

	return result;
}

bool BoxModel::CanDetach(BoxId box) const{

	if (box < 0 || box >= static_cast<BoxId>(m_boxes.size())){

		return false;
	}

	return m_boxes[static_cast<std::size_t>(box)].members.size() > 1;
}

BoxModel::Subscription BoxModel::AddHandler(ChangeCallback callback){

	const Subscription subscription = m_nextSubscription++;

	m_handlers.emplace_back(subscription, std::move(callback));

	return subscription;
}

void BoxModel::RemoveHandler(Subscription subscription){

	m_handlers.erase(
		std::remove_if(m_handlers.begin(), m_handlers.end(),
		               [subscription](const std::pair<Subscription, ChangeCallback>& entry){
			               return entry.first == subscription;
		               }),
		m_handlers.end());
}

void BoxModel::Notify(BoxId box, ModelChange change){

	// 快照派发（C-M1-8②）：值拷贝订阅表后遍历——handler 内注销只改活列表，不影响本轮；
	// 派发期新注册者本轮不收到、下一轮起收到（T-M1-1⑤ 判据）。
	const auto snapshot = m_handlers;

	for (const auto& entry : snapshot){

		if (entry.second){

			entry.second(box, change);
		}
	}
}

}   // namespace ECDI::DesktopNest
