#include "RunAllTests.h"

#include <windows.h>   // SetConsoleOutputCP（平台 API 置于 ECDI 头之后，避免其宏污染）

/// @brief 测试运行入口（仅 CMake 侧 ecdi_tests target——不登记 vcxproj，GUI exe 的 wWinMain 会冲突）
/// @details 退出码 = 失败数（0 = 全绿）；失败明细走 stdout（PrintSummary 另走 OutputDebugString/MessageBox）。
/// @note 控制台输出代码页切 UTF-8：本工程开了 /utf-8（CMakeLists.txt:45 / :101），窄字面量按 UTF-8 字节输出，
///       而控制台默认 CP936(GBK) 按双字节解码 ⇒ 中文变怪字（「尝试」→「灏濊瘯」）。此处一次设定，进程内全部
///       输出受益；仅影响真实控制台的显示——重定向到文件时字节本就是 UTF-8（用 UTF-8 编辑器打开即正常）。
int main()
{
	// 禁止 DWM 幽灵窗口（2026-09-14 定性）：反锯齿测试会连续数秒不取消息（读 4 万像素/帧 ≈ 1.5 s × 多帧），
	// 超过阈值后系统判定窗口无响应，用类名 `Ghost` 的替身窗口以相同 z-order / 位置 / 大小替换它
	// ⇒ 原窗口不再被合成绘制 ⇒ 其 DC 可见区变空（NULLREGION）⇒ GetPixel 恒 CLR_INVALID。
	// 定性依据：GetClipBox 实测 clipType=1（NULLREGION）+ WindowFromPoint 命中 [Ghost] ≠ self。
	DisableProcessWindowsGhosting();

	SetConsoleOutputCP(CP_UTF8);
	return ECDI::Test::RunAllTests();
}
