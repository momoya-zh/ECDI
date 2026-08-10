#include "ECDI/Widget/Label.h"

#include <utility>
namespace ECDI
{

Label::Label(const std::wstring& text): m_text(text){

}

Label::Label(std::wstring&& text): m_text(std::move(text)){

}


void Label::SetText(const std::wstring& text){

	m_text = text;

}

void Label::SetText(std::wstring&& text){

	m_text = std::move(text);

}

const std::wstring& Label::GetText() const noexcept{

	return m_text;

}
}
