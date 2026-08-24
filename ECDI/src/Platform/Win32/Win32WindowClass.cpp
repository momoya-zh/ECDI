#include "ECDI/Platform/Win32/Win32WindowClass.h"

#include "ECDI/Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/Core/String.h"

#include <system_error>

namespace ECDI{

namespace{   // 匿名 namespace：窗口类内部常量
const char* kWindowClassName = "ECDI FrameWork";   // 窗口类名（原 Application 构造硬编码，7.1.5 下沉）
}

WindowClass& WindowClass::Instance(){

	// 7.1.5：窗口类注册归"窗口系统"自身——static 局部 RAII（注册一次跨窗口共用，进程退出反注册）。
	// WindowProc 符号平台内闭环（Win32PlatformWindow——cpp 同族引用，Application 永不知晓）
	static WindowClass windowClass(kWindowClassName, &Win32PlatformWindow::WindowProc);

	return windowClass;

}

WindowClass::WindowClass(const std::string& className, WNDPROC windowProc):
	m_className(UTF8ToWide(className)),
	m_instance(GetModuleHandleW(nullptr)),
	m_windowProc(windowProc){

	// 填充 WNDCLASSW 结构体
	WNDCLASSW wc{};

	wc.style = CS_DBLCLKS;   // 8.5.2：支持双击消息（WM_LBUTTONDBLCLK——TextBox 双击选词前置；不加收不到）

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
