#pragma once

#include <string>

namespace ECDI{
namespace Platform{

/// @brief 当前可执行文件所在目录（UTF-8；**不带尾部分隔符**——调用方自行拼 `\`）
/// @details P1 定位 `<exe_dir>/networkbackend/probe.exe`；P2 资源释放复用。
/// 薄封装（Win32 GetModuleFileNameW）——无单元测试，集成验证。
std::string GetExecutableDirectory();

}
}
