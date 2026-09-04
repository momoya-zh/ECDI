#pragma once

#include "ECDI/Platform/ChildProcess.h"

#include <Windows.h>

#ifdef DrawText
#undef DrawText
#endif

namespace ECDI{

/// @brief Win32 子进程实现（匿名管道——CreateProcess + PeekNamedPipe 非阻塞读）
/// @details 句柄继承矩阵（GPT 评审冻结）：stdin 管道子 Read 可继承/父 Write 不可继承；
/// stdout 管道子 Write 可继承/父 Read 不可继承——父侧两端 SetHandleInformation 清标志，
/// 否则子进程继承父写端 → CloseInput 后收不到 EOF。
class Win32ChildProcess final: public ChildProcess{
public:
	Win32ChildProcess() = default;

	~Win32ChildProcess() override;   // 安全网：仍在运行 → Terminate + 关全部句柄

	bool Start(const std::string& executablePath, const std::vector<std::string>& args = {}) override;

	bool IsRunning() const override;

	bool WriteLine(const std::string& line) override;

	std::string ReadAvailable() override;

	void CloseInput() override;

	bool WaitForExit(unsigned int timeoutMs) override;

	void Terminate() override;

private:

	/// @brief 关全部句柄（幂等；析构/失败清理共用）
	void CloseHandles() noexcept;

	HANDLE m_hProcess = nullptr;     ///< 子进程句柄
	HANDLE m_hStdinWrite = nullptr;  ///< stdin 写端（父持有；CloseInput 关闭 = EOF）
	HANDLE m_hStdoutRead = nullptr;  ///< stdout 读端（父持有；PeekNamedPipe 非阻塞）
};

}
