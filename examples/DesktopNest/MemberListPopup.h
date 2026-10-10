#pragma once

#include "BoxModel.h"   // BoxId（同一 ID 空间——详设 §1.2）
#include "VisualStyle.h"

#include "ECDI/Widget/Button.h"   // 行 = Button（文本读写 + 可用性；行类型即本类实现细节的公开化）
#include "ECDI/Widget/Widget.h"

#include <functional>
#include <string>
#include <utility>   // std::move（SetOnXxx 内联）——头自包含（C-M1-23）
#include <vector>

namespace ECDI::DesktopNest {

/// @brief 成员列表浮层（K3——裸 Widget 容器）。
/// @details ★ G-5 红线：**禁用 Panel**——`Panel::ContainsPoint` 恒 false ⇒ 空白点击穿透。
///          两分区（需求 K3）：① 成员单选行（勾选即切换）；② 成员管理（拆出当前 + 加入其他独立框）。
///          行 = Button（`ConsumesMouseInput() == true`：命中即 HTCLIENT，按钮可点；
///          浮层不在 caption 区，与标题条拖动语义无交集）。
/// ★ 重建纪律（C-M1-19）：全量重建（Rebuild）只在 Open 时；浮层可见期间的模型变更
///   只可能是 Activated（成员数不变）⇒ Refresh 原位更新文本、**绝不销毁行**——
///   派发链安全论证见详设 §4 C-M1-19（冒泡派发「调用后才读父」）。
class MemberListPopup : public Widget {
public:
	using SelectionCallback = std::function<void(BoxId /*member*/)>;
	using DetachCallback = std::function<void()>;
	using CollectCallback = std::function<void(BoxId /*source*/)>;

	MemberListPopup();

	/// @brief 全量重建两分区（每次 Open 前——S3 幂等：先拆旧行再建新行）。
	void Rebuild(const BoxModel& model, BoxId box);

	/// @brief 原位刷新（仅浮层可见时——成员数不变，只更新文本/可用性）。
	/// @note 不创建、不销毁任何控件（C-M1-19 R2）。
	void Refresh(const BoxModel& model, BoxId box);

	// ── 回调（由 BoxView 注册——K4 纪律：纯 Model 操作入口）────

	void SetOnMemberSelected(SelectionCallback callback) { m_onSelected = std::move(callback); }
	void SetOnDetachRequested(DetachCallback callback) { m_onDetach = std::move(callback); }
	void SetOnCollectRequested(CollectCallback callback) { m_onCollect = std::move(callback); }

	// ── 测试观测面 ──────────────────────────────────

	/// @brief 成员行文本（i = 序号；越界返回空串）——供无头断言勾选前缀
	[[nodiscard]] std::string GetMemberRowText(std::size_t index) const;

	/// @brief 拆出行是否可用（= CanDetach 的呈现值）——供无头断言可用性门控
	[[nodiscard]] bool IsDetachRowEnabled() const;

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:
	/// @brief 拆旧行（RemoveChild 返 unique_ptr——离开作用域即销毁）
	void ClearRows();

	/// @brief 行工厂（Button——文本 + 可用性）
	Button* MakeRow(const std::string& text, bool enabled, int y);

	/// @brief 成员行文本组装（勾选前缀 + 标题）
	[[nodiscard]] static std::string MemberRowText(const BoxModel& model, const Box& box, BoxId member);

	SelectionCallback m_onSelected;
	DetachCallback m_onDetach;
	CollectCallback m_onCollect;

	std::vector<Button*> m_memberRows;   ///< 分区① 行（非拥有——子树拥有）
	Button* m_detachRow = nullptr;       ///< 分区② 拆出行（非拥有）
	std::vector<Button*> m_joinRows;     ///< 分区② 加入行（非拥有）
};

}   // namespace ECDI::DesktopNest
