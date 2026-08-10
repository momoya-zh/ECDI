#pragma once

#include "ECDI/EventSystem/Input/InputEvent.h"

namespace ECDI{

/// @brief 字符输入事件
/// @details
/// 由 WM_CHAR 翻译而来，携带一个 Unicode 码点（char32_t）。
/// 与 KeyEvent 平级（不继承 KeyEvent），因为 CharInput 的数据是字符而非物理按键。
/// 一个事件 = 一个码点：Win32 翻译器负责把 UTF-16 代理对组合成完整码点
/// （emoji 等 BMP 外字符收到一个事件，而非两个代理项）。
/// 框架层不暴露 wchar_t、不绑定 UTF-16 编码表示——编码表示泄露是分层破坏
/// （wchar_t 本身不是问题，问题是事件层暴露了平台的 UTF-16 表示）。
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
	/// @param codepoint 输入的 Unicode 码点（U+0000 - U+10FFFF，跨平台语义明确）
	CharInputEvent(
		Window* window,
		char32_t codepoint
	):InputEvent(window),m_codepoint(codepoint){

	}


	/// @brief 获取输入的 Unicode 码点
	char32_t GetCodepoint() const noexcept {

		return m_codepoint;

	}


private:

	char32_t m_codepoint;

};

}
