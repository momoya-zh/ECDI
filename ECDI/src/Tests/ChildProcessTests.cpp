#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Platform/ChildProcess.h"

#include <chrono>
#include <string>
#include <thread>

using namespace ECDI;

namespace{

// ── P1：ChildProcess 集成测试（真实子进程——cmd.exe Windows 必有；不依赖 probe.exe 产物）──
// 网络层已由 probe-go P0 命令行验证；此处聚焦进程生命周期契约（详设 §8 用例 1-3）

void TestChildProcessStartReadEcho()
{
	// StartReadEcho：spawn `cmd.exe /c echo hello` → 非阻塞轮询读回 hello
	auto proc = ChildProcess::Create();
	EXPECT_TRUE(proc->Start("cmd.exe", { "/c", "echo hello" }));
	std::string buffer;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (buffer.find("hello") == std::string::npos && std::chrono::steady_clock::now() < deadline){
		buffer += proc->ReadAvailable();   // 非阻塞——GUI 轮询同款
		if (buffer.find("hello") != std::string::npos)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_TRUE(buffer.find("hello") != std::string::npos);
	EXPECT_TRUE(proc->WaitForExit(2000));
}

void TestChildProcessEofExit()
{
	// EofExit：交互式 cmd（无参——等 stdin EOF）→ CloseInput → 自退（EOF = 退出信号契约）
	auto proc = ChildProcess::Create();
	EXPECT_TRUE(proc->Start("cmd.exe", {}));
	EXPECT_TRUE(proc->IsRunning());
	proc->CloseInput();   // 关写端 → 子进程读 EOF 自退
	EXPECT_TRUE(proc->WaitForExit(3000));
	EXPECT_FALSE(proc->IsRunning());
}

void TestChildProcessTerminateTimeout()
{
	// TerminateTimeout：`cmd /c ping 127.0.0.1 -n 5`（约 4s，不依赖 stdin——pause 在管道 stdin 下会立即退出）
	// → WaitForExit 超时 → Terminate 强杀
	auto proc = ChildProcess::Create();
	EXPECT_TRUE(proc->Start("cmd.exe", { "/c", "ping", "127.0.0.1", "-n", "5" }));
	EXPECT_FALSE(proc->WaitForExit(100));   // 超时（仍在运行）
	proc->Terminate();
	EXPECT_TRUE(proc->WaitForExit(2000));   // 强杀后退出
}

} // anonymous namespace

void ECDI::Test::RegisterChildProcessTests()
{
	GetTestRegistry().Add("ChildProcess.StartReadEcho",      &TestChildProcessStartReadEcho);
	GetTestRegistry().Add("ChildProcess.EofExit",            &TestChildProcessEofExit);
	GetTestRegistry().Add("ChildProcess.TerminateTimeout",   &TestChildProcessTerminateTimeout);
}
