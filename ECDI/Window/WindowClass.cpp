#include"WindowClass.h"
#include"Window.h"
#include<system_error>

WindowClass::WindowClass(const std::wstring& className, WNDPROC windowProc):
	m_className(className),
	m_instance(GetModuleHandleW(nullptr)),
	m_windowProc(windowProc)
{
	WNDCLASSW wc{};

	wc.lpfnWndProc = m_windowProc;   

	wc.hInstance = m_instance;

	wc.lpszClassName = m_className.c_str();
	
	wc.hCursor = LoadCursorW(
	nullptr,
	IDC_ARROW
);
	ATOM atom = RegisterClassW(&wc);

	if (atom == 0) {

		throw std::system_error(

			std::error_code(static_cast<int>(GetLastError()), std::system_category()),

			"RegisterClassW failed");

	}

	m_registered = true;

}

WindowClass::~WindowClass()noexcept {

	Release();
}

const wchar_t* WindowClass:: GetClassName()const {

	return m_className.c_str();

}

HINSTANCE WindowClass::GetInstance()const {

	return m_instance;

}

WindowClass::WindowClass(WindowClass&& other)noexcept

	:m_className(std::move(other.m_className)),

	m_instance(other.m_instance),

	m_registered(other.m_registered),

	m_windowProc(other.m_windowProc){

	other.m_instance = nullptr;

	other.m_registered = false;

	other.m_windowProc = nullptr;
}


WindowClass& WindowClass::operator=(WindowClass&& other) noexcept
{
	if (this != &other){

		if (!Release()){

			return *this;

		}

		m_registered = other.m_registered;

		m_className = std::move(other.m_className);

		m_instance = other.m_instance;

		m_windowProc = other.m_windowProc;

		other.m_instance = nullptr;

		other.m_windowProc = nullptr;

		other.m_registered = false;
	}

	return *this;
}

bool WindowClass::Release()noexcept {
	if (m_registered == false) {
		return true;
	}
	const BOOL result = UnregisterClassW(m_className.c_str(),m_instance);
	if (result) {
		m_registered = false;
		m_instance = nullptr;
		m_windowProc = nullptr;
		return true;
	}
	return false;
}