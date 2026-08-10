#pragma once

#include"Widget.h"

#include<string>

namespace ECDI{

class Label: public Widget{

public:

	Label() = default;

	explicit Label(const std::wstring&text);

	explicit Label(std::wstring&&text);

	void SetText(const std::wstring& text);

	void SetText(std::wstring&& text);

	const std::wstring& GetText()const noexcept;

private:

	std::wstring m_text;

};


}
