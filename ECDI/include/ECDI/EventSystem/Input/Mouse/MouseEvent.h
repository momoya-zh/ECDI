#pragma once

#include "ECDI/EventSystem/Input/InputEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

namespace ECDI{

/// @brief 鼠标事件基类
/// @details
/// 所有鼠标事件（Move/ButtonDown/ButtonUp/Wheel）的公共基类。
/// 携带鼠标在窗口客户区中的坐标 (x, y)。
/// Phase 19：另携带事件发生时「按下的鼠标键」与「修饰键状态」两维（均为**当时状态**，
/// 与按钮事件的「本次是哪个键」正交并存）。
class MouseEvent :public InputEvent {

protected:

	/// @param window 事件来源窗口
	/// @param mouseX 鼠标 X 坐标（客户区坐标）
	/// @param mouseY 鼠标 Y 坐标（客户区坐标）
	/// @param pressedButtons 此刻按下的鼠标键位掩码（**位序 = MouseButton 枚举序**；0 = 无键按下）
	/// @note ★ `pressedButtons` 是**低层构造通道参数**，**不是**推荐消费者使用的语义接口——
	///       消费侧请用 `IsButtonDown()`。掩码只为两件事存在：① 平台翻译器（WindowMessageHandler）
	///       填入；② 既有测试构造点保持兼容（默认值 0 = 空状态）。请勿把 `1u << 序号` 写进业务逻辑。
	/// @param modifiers 此刻的修饰键状态（与 `KeyEvent` 同类型同语义）
	/// @note **Alt 恒不置位**——**当前 Win32 鼠标映射**不从 `wParam` 产生 Alt（`WM_*MOUSE*` 无
	///       `MK_ALT`）。这是**映射边界**，**不是**事件模型不支持 Alt（`KeyModifier` 本就含 `Alt`）。
	///       理由、措辞纪律与重启条件见 docs/phase19-mouse-event-dimensions-preliminary-design.md §2.3。
	MouseEvent(
		Window* window,
		int mouseX,
		int mouseY,
		unsigned int pressedButtons = 0,
		KeyModifier modifiers = KeyModifier::None
	): InputEvent(window),m_mouseX(mouseX),m_mouseY(mouseY),
	  m_pressedButtons(pressedButtons),m_modifiers(modifiers){

	}

public:

	/// @brief 获取鼠标 X 坐标（客户区坐标）
	int GetMouseX() const noexcept{

		return m_mouseX;

	}

	/// @brief 获取鼠标 Y 坐标（客户区坐标）
	int GetMouseY() const noexcept{

		return m_mouseY;

	}

	/// @brief 此刻指定的鼠标键是否处于按下状态（**原始事实**——不做“拖动”之类语义结论）
	/// @note 位序 = `MouseButton` 枚举序（见构造注释）；“未按下”与“无信息”不作区分（默认值即空）
	bool IsButtonDown(MouseButton button) const noexcept{

		return (m_pressedButtons & (1u << static_cast<unsigned int>(button))) != 0u;

	}

	/// @brief 是否按下指定修饰键组合（位与 == 全含——`HasModifier(Ctrl | Shift)` 可组合查询）
	/// @note 与 `KeyEvent::HasModifier` **同名同义**（「一个概念只用一个词」）
	/// @note `KeyModifier::Alt` 当前**恒返回 false**（当前 Win32 鼠标映射不产生 Alt，见构造注释）
	/// @note ★ **`KeyModifier::None` 恒返回 true**——“**全含**”语义对**空集合空洞成立**（`x & 0 == 0`）。
	///       这是**继承 `KeyEvent` 的既有语义**，本项**有意保持一致、不改**（两处必须同名同义）。
	///       改它属**独立的 API 语义问题** ⇒ 记账于 `framework-defect-audit.md` **§7 A-10**（含重启条件）。
	bool HasModifier(KeyModifier modifier) const noexcept{

		return (static_cast<int>(m_modifiers) & static_cast<int>(modifier)) == static_cast<int>(modifier);

	}

private:

	int m_mouseX;
	int m_mouseY;
	unsigned int m_pressedButtons = 0;			///< ★ P19：位序 = MouseButton 枚举序
	KeyModifier m_modifiers = KeyModifier::None;	///< ★ P19：与 KeyEvent 同源同类型


 };

}
