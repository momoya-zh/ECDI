#include "ECDI/Platform/Win32/Win32PlatformApplication.h"

#include <Windows.h>

namespace ECDI{

int Win32PlatformApplication::Run(){

	// 标准 Win32 消息循环：GetMessage 返回 0 时退出（收到 WM_QUIT）
	MSG message{};

	while (GetMessageW(&message, nullptr, 0, 0)){

		TranslateMessage(&message);

		DispatchMessageW(&message);

		// 每条消息后的延迟清理时机（资源生命周期管理——框架经 SetDeferredCleanup 注册）
		PerformDeferredCleanup();

	}

	return static_cast<int>(message.wParam);

}

void Win32PlatformApplication::RequestExit(){

	PostQuitMessage(0);

}

}
