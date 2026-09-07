#include "RunAllTests.h"

/// @brief 测试运行入口（仅 CMake 侧 ecdi_tests target——不登记 vcxproj，GUI exe 的 wWinMain 会冲突）
/// @details 退出码 = 失败数（0 = 全绿）；失败明细走 stdout（PrintSummary 另走 OutputDebugString/MessageBox）。
int main()
{
	return ECDI::Test::RunAllTests();
}
