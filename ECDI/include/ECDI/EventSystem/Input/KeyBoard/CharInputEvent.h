#pragma once

#include "ECDI/EventSystem/Input/InputEvent.h"

/// @brief 字符输入事件
/// @details
/// 由 WM_CHAR 翻译而来，携带一个 wchar_t 字符码。
/// 与 KeyEvent 平级（不继承 KeyEvent），因为 CharInput 的数据是字符而非物理按键。
/// 一个 wchar_t = 一个事件（接受 emoji 拆成两个代理项的限制）。
class CharInputEvent : public InputEvent{

public:

	static EventType StaticType() {

		return EventType::CharInput;

	}


	EventType GetType() const noexcept override {

		return StaticType();

	}


public:

	/// @param window    事件来源窗口
	/// @param character 输入的字符（wchar_t）
	CharInputEvent(
		Window* window,
		wchar_t character
	):InputEvent(window),m_character(character){

	}


	/// @brief 获取输入的字符
	wchar_t GetCharacter() const noexcept {

		return m_character;

	}


private:

	wchar_t m_character;

};
