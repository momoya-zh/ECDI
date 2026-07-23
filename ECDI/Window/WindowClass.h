#pragma once

#include<Windows.h>
#include<string>

class WindowClass
{
public:

	WindowClass(const std::wstring& className,WNDPROC windowProc);

	WindowClass(const WindowClass&) = delete;

	WindowClass& operator=(const WindowClass&) = delete;

	WindowClass(WindowClass&& other) noexcept;

	WindowClass& operator=(WindowClass&& other) noexcept;

	~WindowClass();

	const wchar_t* GetClassName() const;

	HINSTANCE GetInstance() const;

	bool Release() noexcept;

private:

	std::wstring m_className;

	HINSTANCE m_instance=nullptr;

	WNDPROC m_windowProc=nullptr;

	bool m_registered = false;
};