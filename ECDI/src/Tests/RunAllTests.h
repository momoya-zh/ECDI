#pragma once

namespace ECDI::Test {

/// @brief 运行所有无窗口单元测试（Debug 模式调用；失败即终止）
void RunAllTests();

// 各模块测试入口（定义在对应的 Tests/*.cpp 中）
void RunRendererTests();
void RunWidgetTests();
void RunLayoutTests();
void RunTextBoxTests();
// void RunEventTests();  // P2

} // namespace ECDI::Test