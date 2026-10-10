#pragma once

#include "BoxModel.h"
#include "VisualStyle.h"

#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"   // 判据形参（完整类型——OnMouseButtonDown override）
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/TextWidget.h"   // 标题 Label 成员类型（读/写文本）
#include "ECDI/Widget/Widget.h"

#include <functional>
#include <memory>
#include <string>    // std::string（BadgeButton 形参 / GetTitleText）——头自包含（C-M1-23）
#include <utility>   // std::move（SetOnXxx 内联）——头自包含（C-M1-23）

namespace ECDI::DesktopNest {

// ★ 前置声明须在本命名空间内（实施勘误 2026-10-09：写在 ECDI 里会声明出
//   `ECDI::BoxWindow` / `ECDI::MemberListPopup` 两个**不同**的类型 ⇒ 定义处不匹配）
class BoxWindow;
class MemberListPopup;

/// @brief 框根容器（BoxView 装配的根——点外关闭判据的落点，P1）。
/// @details OnPaint 发 DrawRoundedRect 铺满客户区（框背景，K1）+ 标题条分隔线；
///          OnMouseButtonDown = 点外关闭判据（C-M1-12 三判据 + 入口豁免）。
///          坐标约定：BoxRoot 满铺客户区 ⇒ 事件客户区坐标 == BoxRoot 局部坐标。
class BoxRoot : public Widget {
public:
	void SetPopup(MemberListPopup* popup) noexcept { m_popup = popup; }

	void SetEntryButton(Widget* entryButton) noexcept { m_entryButton = entryButton; }

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;

	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;

private:
	MemberListPopup* m_popup = nullptr;   ///< 非拥有——BoxRoot 子树（末位 AddChild）
	Widget* m_entryButton = nullptr;      ///< 非拥有——标题条入口按钮（判据豁免）
};

/// @brief 徽标按钮（标题条入口——成员数徽标，K3 视觉标识）。
/// @details 按钮本体 = 框架默认 ButtonStyle（Button::OnPaint）；徽标 =
///          自绘右上圆 + 数字（DrawRoundedRect + DrawText）。
class BadgeButton : public Button {
public:
	explicit BadgeButton(const std::string& text);

	void SetBadgeCount(int count) noexcept { m_badgeCount = count; }

	/// @brief 徽标计数（测试观测点——T-M1-1④）
	[[nodiscard]] int GetBadgeCount() const noexcept { return m_badgeCount; }

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:
	int m_badgeCount = 0;
};

/// @brief 框视图（P2 四件分离之 View——装配 + 重建，不持 Model 数据）。
class BoxView {
public:
	/// @brief 协调器动作（窗口生命周期类操作归协调器——View 不建/关窗）。
	enum class Action { Detach, Collect };

	using ActionCallback = std::function<void(Action /*action*/, BoxId /*source*/)>;
	using AbsorbedCallback = std::function<void()>;
	using PopupOpeningCallback = std::function<void()>;

	BoxView(BoxModel& model, BoxId box);

	/// @brief 析构注销订阅（C-M1-8③）
	~BoxView();

	BoxView(const BoxView&) = delete;
	BoxView& operator=(const BoxView&) = delete;

	/// @brief 装配（S3 幂等：全量重建——宿主根可为新根，C-M1-16⑧ 的重建入口）。
	/// @pre 已解装（m_root == nullptr——C-M1-16 解装不变量；重复装配 = Debug 断言）
	void Assemble(Widget& hostRoot);

	/// @brief 解装（协调器关窗后调用——非拥有指针清空；Assemble 的前置不变量）。
	void Disassemble() noexcept;

	/// @brief 几何请求通道（C-M1-14：可空——nullptr = 无头测试态，几何请求跳过）。
	void SetBoxWindow(BoxWindow* window) noexcept { m_window = window; }

	// ── 回调（由协调器注册）────────────────────────

	void SetOnAction(ActionCallback callback) { m_onAction = std::move(callback); }
	void SetOnAbsorbed(AbsorbedCallback callback) { m_onAbsorbed = std::move(callback); }
	void SetOnPopupOpening(PopupOpeningCallback callback) { m_onPopupOpening = std::move(callback); }

	/// @brief 订阅 handler（Model → UI 单向数据流的接收端——C-M1-13）。
	void OnModelChanged(BoxId box, ModelChange change);

	// ── 浮层（P6 切换契约——入口按钮点击回调的权威入口）────

	void OpenPopup();
	void ClosePopup();
	void TogglePopup();

	/// @brief 折叠切换（K2——唯一二态开关；几何请求经 C-M1-14 通道）。
	void ToggleCollapse();

	// ── 浮层内动作（MemberListPopup 回调——纯 Model 操作，K4 纪律）────

	void SelectMember(BoxId member);
	void RequestDetach();
	void RequestCollect(BoxId source);

	// ── 测试观测面 ──────────────────────────────────

	[[nodiscard]] bool IsPopupVisible() const noexcept;

	/// @brief 入口按钮徽标计数（无头测试态：未装配返回 -1）
	[[nodiscard]] int GetBadgeCount() const noexcept;

	/// @brief 内容面板（测试观测：内容行文本 / 展开态 / 内容可见性）
	[[nodiscard]] CollapsiblePanel* GetContentPanel() const noexcept { return m_contentPanel; }

	/// @brief 标题 Label 文本（测试观测：派生标题——C-M1-17）
	[[nodiscard]] std::string GetTitleText() const;

	/// @brief 根容器（测试观测：判据宿主——无头装配后供合成事件直调）
	[[nodiscard]] Widget* GetRoot() const noexcept { return m_root; }

private:
	/// @brief 重建（S3 幂等）：标题（C-M1-17）+ 徽标 + 内容 + 浮层（C-M1-19 R2）
	void Rebuild();

	/// @brief 派生标题（C-M1-17）：members 空 ⇒ 框标题；否则 ⇒ active 成员标题
	[[nodiscard]] std::string DerivedTitle() const;

	/// @brief 内容区假行重建（kFakeContentRows 行——标题随 active 成员）
	void RebuildContent();

	BoxModel& m_model;
	BoxId m_box;

	BoxWindow* m_window = nullptr;              ///< C-M1-14（几何通道——可空）
	Widget* m_root = nullptr;                   ///< BoxRoot（非拥有——宿主树拥有）
	Widget* m_titleBar = nullptr;               ///< 标题条容器（非拥有）
	BadgeButton* m_entryButton = nullptr;       ///< 入口按钮（非拥有）
	TextWidget* m_titleLabel = nullptr;         ///< 标题 Label（非拥有——TextWidget 接口读/写文本）
	Button* m_collapseButton = nullptr;         ///< 折叠按钮（非拥有）
	CollapsiblePanel* m_contentPanel = nullptr; ///< 内容面板（非拥有）
	MemberListPopup* m_popup = nullptr;         ///< 浮层（非拥有——BoxRoot 子树，常驻隐藏态）
	BoxModel::Subscription m_subscription = 0;  ///< 订阅句柄（析构注销）
	ActionCallback m_onAction;
	AbsorbedCallback m_onAbsorbed;
	PopupOpeningCallback m_onPopupOpening;
};

}   // namespace ECDI::DesktopNest
