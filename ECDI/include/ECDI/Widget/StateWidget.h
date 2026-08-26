#pragma once

#include "ECDI/Widget/TextWidget.h"

#include <functional>

namespace ECDI{

class KeyDownEvent;
class MouseButtonDownEvent;

/// @brief 状态控件基类（6.2；第二个状态控件出现时抽取——TextWidget 抽取先例）
/// @details **行为复用，非视觉复用**：checked 状态/键鼠共享/通知机制在本类；
/// CheckBox/Radio 各自持有专属 Style（CheckBoxStyle/RadioStyle）并实现绘制。
/// 文字视觉（foreground/font）继承自 TextWidget::m_style（TextStyle——单一真相，不重复定义）。
class StateWidget: public TextWidget{

public:

	StateWidget();                                   // 空文本（TextWidget() 已注入 TextStyle）
	explicit StateWidget(const std::string& text);   // 文本（TextWidget(text) 已注入 TextStyle）

	bool CanFocus() const noexcept override { return true; }

	/// @brief 设置选中状态（唯一状态入口——契约 2；virtual——Radio override 扩展互斥必须）
	/// @details 值变化才触发通知（m_checked != checked 时：OnCheckedChanged + 回调 + Invalidate）；
	/// 相同值 no-op（Radio 交互不可取消的保证基础）。
	/// 架构含义："唯一入口" = **所有状态修改必须最终经过 StateWidget::SetChecked()**——
	/// 互斥内部 `sibling->StateWidget::SetChecked(false)` 显式限定基类也符合此契约（强制取消，不重入互斥策略）。
	virtual void SetChecked(bool checked);

	bool IsChecked() const noexcept { return m_checked; }

	// ── 回调（7.5 两套并存——D2：继承 override 基座 + 回调业务便利层）──

	using CheckedChangedCallback = std::function<void(bool)>;   ///< 回调参数 = 新状态

	/// @brief 注册选中状态变化回调（覆盖式；传空 = 解除注册）
	/// @details 回调在 RaiseCheckedChanged() 内、OnCheckedChanged() 虚方法之后调用——
	/// 子类 override OnCheckedChanged 不影响回调触发（D4 RaiseXxx 分离模式）
	void SetOnCheckedChanged(CheckedChangedCallback callback);

protected:

	/// @brief 选中状态变化虚方法（契约 3；子类可 override 扩展；带新状态参数）
	virtual void OnCheckedChanged(bool /*checked*/){}

	/// @brief 键鼠共享切换逻辑（契约 6——虚方法隔离 CheckBox/Radio 差异）
	/// @details 默认 = CheckBox 语义：SetChecked(!m_checked)；Radio override = SetChecked(true)（非反转）
	virtual void OnClickToggle();

	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 点击 → OnClickToggle
	void OnKeyDown(const KeyDownEvent&) override;                    // Space → OnClickToggle

private:

	/// @brief 状态变化通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先 OnCheckedChanged() 虚方法，再 m_onCheckedChanged() 回调——彼此独立
	void RaiseCheckedChanged();

	bool m_checked = false;   ///< 选中状态（契约 1：状态属控件自身）

	CheckedChangedCallback m_onCheckedChanged;   ///< 回调（7.5 业务便利层）

};

}
