#pragma once

namespace ECDI::Test {

/// @brief 运行所有无窗口单元测试（Debug 模式调用）
/// @details orchestration 层（初步设计 §3.7）：注册全部测试 → Runner 统一执行 → 汇总报告。
/// 平台入口只换这里——测试核心（Registry/Runner/Assert/Summary）平台无关。
void RunAllTests();

// 各模块测试注册入口（定义在对应的 Tests/*.cpp 中；其中 TestFrameworkTests 为基础设施自测）
void RegisterWidgetTests();
void RegisterLayoutTests();
void RegisterTextBoxTests();
void RegisterRendererTests();
void RegisterEventTests();
void RegisterTestFrameworkTests();

} // namespace ECDI::Test
