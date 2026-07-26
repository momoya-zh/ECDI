#pragma once

#include "Input/InputEvent.h"

class CharInputEvent : public InputEvent{

public:

	static EventType StaticType() {

		return EventType::CharInput;

	}


	EventType GetType() const noexcept override {

		return StaticType();

	}


public:

	CharInputEvent(
		Window* window,
		wchar_t character
	):InputEvent(window),m_character(character){

	}


	wchar_t GetCharacter() const noexcept {

		return m_character;

	}


private:

	wchar_t m_character;

};