#pragma once

#include "ECDI/Platform/PlatformApplication.h"

namespace ECDI{

/// @brief Win32 平台应用（7.1.5：GetMessageW 消息泵 + PostQuitMessage——消息循环唯一归属）
/// @details 消息驱动模型：Run 循环内每条消息后 PerformDeferredCleanup
/// （延迟销毁安全时机——框架经 SetDeferredCleanup 注册逻辑，时机平台控制）。
class Win32PlatformApplication final : public PlatformApplication{
public:
	int Run() override;

	void RequestExit() override;
};

}
