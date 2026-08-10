#include"ECDI/Window/WindowClass.h"
#include"ECDI/Window/Window.h"
#include"ECDI/Core/String.h"

#include<system_error>
namespace ECDI
{

WindowClass::WindowClass(const std::string& className, WNDPROC windowProc):
	m_className(UTF8ToWide(className)),
	m_instance(GetModuleHandleW(nullptr)),
	m_windowProc(windowProc){

	// 填充 WNDCLASSW 结构体
	WNDCLASSW wc{};

	wc.lpfnWndProc = m_windowProc;   // 消息回调

	wc.hInstance = m_instance;

	wc.lpszClassName = m_className.c_str();
	
	// 设置默认箭头光标
	wc.hCursor = LoadCursorW(
	nullptr,
	IDC_ARROW
);
	// 注册窗口类
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

// ── 移动构造：转移注册所有权 ──────────────────────
WindowClass::WindowClass(WindowClass&& other)noexcept

	:m_className(std::move(other.m_className)),

	m_instance(other.m_instance),

	m_windowProc(other.m_windowProc), 
	
	m_registered(other.m_registered) {

	// 源对象置空，防止析构时反注册
	other.m_instance = nullptr;

	other.m_registered = false;

	other.m_windowProc = nullptr;
}


// ── 移动赋值：先释放自身，再接管源 ─────────────────
WindowClass& WindowClass::operator=(WindowClass&& other) noexcept
{
	if (this != &other){

		// 先释放自身的注册
		if (!Release()){

			return *this;

		}


		m_className = std::move(other.m_className);

		m_instance = other.m_instance;

		m_windowProc = other.m_windowProc;
		m_registered = other.m_registered;

		// 源对象置空
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
	// 反注册窗口类
	const BOOL result = UnregisterClassW(m_className.c_str(),m_instance);

	if (result) {

		m_registered = false;

		m_instance = nullptr;

		m_windowProc = nullptr;

		return true;

	}

	return false;
}

}
