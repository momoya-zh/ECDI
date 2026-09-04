#include "ECDI/Platform/Win32/Win32ChildProcess.h"

#include "ECDI/Core/String.h"

namespace ECDI{

namespace{

/// @brief Windows command line 参数引号包裹（含空格/内嵌引号安全——quoting 是 Win32 实现职责）
std::wstring QuoteArg(const std::wstring& arg){
	std::wstring q = L"\"";
	for (wchar_t c : arg){
		if (c == L'"'){
			q += L"\\\"";
		}
		else{
			q += c;
		}
	}
	q += L"\"";
	return q;
}

}

Win32ChildProcess::~Win32ChildProcess(){
	// 安全网：正常路径 demo 先 CloseInput + WaitForExit；此处兜底防残留
	if (IsRunning()){
		Terminate();
	}
	CloseHandles();
}

bool Win32ChildProcess::Start(const std::string& executablePath, const std::vector<std::string>& args){
	if (m_hProcess != nullptr){
		return false;   // 已启动（P1 单实例场景——防御性拒绝，防句柄泄漏）
	}

	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };   // bInheritHandle=TRUE——子进程需继承子端

	HANDLE hStdinRead = nullptr;
	HANDLE hStdinWrite = nullptr;
	HANDLE hStdoutRead = nullptr;
	HANDLE hStdoutWrite = nullptr;

	if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)){
		return false;
	}
	if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)){
		CloseHandle(hStdinRead);
		CloseHandle(hStdinWrite);
		return false;
	}

	// ⚠️ P0 硬问题（GPT 评审）：父侧句柄清继承——否则子进程意外继承父写端，
	// 父 CloseInput() 后子仍持写端 → 永远收不到 EOF（WaitForExit 卡死）。
	SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

	// cmdline：exe 路径引号包裹 + 逐参引号包裹（quoting 在此完成——接口不收命令行字符串）
	std::wstring cmdline = QuoteArg(UTF8ToWide(executablePath));
	for (const auto& arg : args){
		cmdline += L" " + QuoteArg(UTF8ToWide(arg));
	}

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.hStdInput = hStdinRead;
	si.hStdOutput = hStdoutWrite;
	si.hStdError = hStdoutWrite;   // stderr 合并 stdout（后端崩溃输出可排查）
	si.dwFlags = STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi{};
	// CREATE_NO_WINDOW 必设——否则 GUI 弹控制台窗
	const BOOL ok = CreateProcessW(
		nullptr, cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		nullptr, nullptr, &si, &pi);

	// 无论成败：父侧子端拷贝立即关闭
	CloseHandle(hStdinRead);
	CloseHandle(hStdoutWrite);

	if (!ok){
		CloseHandle(hStdinWrite);
		CloseHandle(hStdoutRead);
		return false;
	}

	CloseHandle(pi.hThread);
	m_hProcess = pi.hProcess;
	m_hStdinWrite = hStdinWrite;
	m_hStdoutRead = hStdoutRead;
	return true;
}

bool Win32ChildProcess::IsRunning() const{
	return m_hProcess != nullptr && WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT;
}

bool Win32ChildProcess::WriteLine(const std::string& line){
	if (m_hStdinWrite == nullptr){
		return false;
	}
	std::string data = line;
	data += '\n';
	// 循环写满（小命令一次性完成；防部分写）
	DWORD total = 0;
	while (total < data.size()){
		DWORD written = 0;
		if (!WriteFile(m_hStdinWrite, data.data() + total,
			static_cast<DWORD>(data.size() - total), &written, nullptr)){
			return false;
		}
		total += written;
	}
	return true;
}

std::string Win32ChildProcess::ReadAvailable(){
	if (m_hStdoutRead == nullptr){
		return {};
	}
	DWORD available = 0;
	if (!PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &available, nullptr)){
		return {};   // 管道异常/已关——空
	}
	if (available == 0){
		return {};
	}
	std::string data(available, '\0');
	DWORD read = 0;
	if (!ReadFile(m_hStdoutRead, data.data(), available, &read, nullptr)){
		return {};
	}
	data.resize(read);
	return data;
}

void Win32ChildProcess::CloseInput(){
	if (m_hStdinWrite != nullptr){
		CloseHandle(m_hStdinWrite);
		m_hStdinWrite = nullptr;
	}
}

bool Win32ChildProcess::WaitForExit(unsigned int timeoutMs){
	if (m_hProcess == nullptr){
		return true;   // 未启动 = 已退出
	}
	return WaitForSingleObject(m_hProcess, timeoutMs) == WAIT_OBJECT_0;
}

void Win32ChildProcess::Terminate(){
	if (m_hProcess != nullptr){
		TerminateProcess(m_hProcess, 1);
	}
}

void Win32ChildProcess::CloseHandles() noexcept{
	if (m_hStdinWrite != nullptr){
		CloseHandle(m_hStdinWrite);
		m_hStdinWrite = nullptr;
	}
	if (m_hStdoutRead != nullptr){
		CloseHandle(m_hStdoutRead);
		m_hStdoutRead = nullptr;
	}
	if (m_hProcess != nullptr){
		CloseHandle(m_hProcess);
		m_hProcess = nullptr;
	}
}

std::unique_ptr<ChildProcess> ChildProcess::Create(){
	return std::make_unique<Win32ChildProcess>();
}

}
