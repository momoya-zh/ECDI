#include "ECDI/Widget/StateWidget.h"

#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"

#include <utility>

namespace ECDI{

StateWidget::StateWidget(): TextWidget(){
	// TextStyle 已由 TextWidget 构造注入；本类无专属 Style（CheckBox/Radio 构造再注入）
}

StateWidget::StateWidget(const std::string& text): TextWidget(text){
}

void StateWidget::SetChecked(bool checked){

	if (m_checked == checked)
		return;   // 相同值 no-op——Radio 交互不可取消的保证（已选中再 SetChecked(true) 无通知）
	m_checked = checked;
	Invalidate();
	RaiseCheckedChanged();

}

void StateWidget::SetOnCheckedChanged(CheckedChangedCallback callback){

	m_onCheckedChanged = std::move(callback);

}

// ── 键鼠共享（契约 6：Space/点击都走 OnClickToggle——虚方法隔离 CheckBox/Radio 差异）──

void StateWidget::OnClickToggle(){

	SetChecked(!m_checked);   // CheckBox 默认语义（反转）；Radio override = SetChecked(true)

}

void StateWidget::OnMouseButtonDown(const MouseButtonDownEvent&){

	// 焦点获取由 Application 前置处理（CanFocus 已 true——同 Button 模式）
	OnClickToggle();

}

void StateWidget::OnKeyDown(const KeyDownEvent& event){

	if (event.GetKeyCode() == KeyCode::Space)
		OnClickToggle();

}

// ── 通知（D4 RaiseXxx 分离模式：虚方法基座 + 回调独立通道）──

void StateWidget::RaiseCheckedChanged(){

	OnCheckedChanged(m_checked);              // ① 虚方法（子类可 override 扩展）

	if (m_onCheckedChanged)                   // ② 回调（独立通道，override 无法吞掉）
		m_onCheckedChanged(m_checked);

}

}
