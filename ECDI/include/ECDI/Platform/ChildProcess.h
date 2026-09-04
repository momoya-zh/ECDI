#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ECDI{

/// @brief 子进程 + 匿名管道（stdin/stdout）抽象——demo/测试零 Win32 类型
/// @details 生命周期：Start → WriteLine×N → (ReadAvailable 轮询)×N → CloseInput(EOF) → WaitForExit / Terminate
/// stdin EOF = 子进程退出信号（协议约定——probe.exe 读到 EOF 自退）
/// P1 边界（modelprobe-p1-detailed-design v1.1 §2.2）：可控的管道式子进程，
/// 非完整进程管理器——不做 ReadAsync/WriteAsync/环境变量/工作目录/PTY（YAGNI）。
class ChildProcess{
public:
	virtual ~ChildProcess() = default;

	/// @brief 工厂——返回平台实现（Win32ChildProcess）；测试可注入 fake
	static std::unique_ptr<ChildProcess> Create();

	/// @brief 启动（UTF-8 可执行路径 + 参数列表）+ 建立匿名管道；失败返回 false（原因查日志）
	/// @details 参数以 vector 传入（**非完整命令行**）——引号/空格转义是 Win32 实现职责，
	/// 接口不暴露 Windows command line quoting（GPT 评审边界冻结）；P1 传空 args
	virtual bool Start(const std::string& executablePath, const std::vector<std::string>& args = {}) = 0;

	/// @brief 存活查询（非阻塞）
	virtual bool IsRunning() const = 0;

	/// @brief 写一行（UTF-8 字节 + 自动补 \n；协议约束：单行 < 4KB——管道缓冲内，不阻塞）
	virtual bool WriteLine(const std::string& line) = 0;

	/// @brief 非阻塞读 stdout 当前全部可用字节（无数据返回空串；UTF-8 文本）
	virtual std::string ReadAvailable() = 0;

	/// @brief 关 stdin（幂等）→ 子进程读 EOF 自退
	virtual void CloseInput() = 0;

	/// @brief 等退出（超时返回 false）
	virtual bool WaitForExit(unsigned int timeoutMs) = 0;

	/// @brief 兜底强杀
	virtual void Terminate() = 0;
};

}
