#pragma once

// ── Phase 32：衬底失败注入缝（详设 D32-N）───────────────────────────────────
// 零公共 API——实现（internal linkage 静态存储）位于 Win32PlatformWindow.cpp；
// 本头只对**库内测试**开放（WindowBackdropTests.cpp include——WindowBackgroundTests.cpp:11
// 「测试可含库内内部头」先例）。grep 锚 = backdropInjection。

namespace ECDI {

/// @brief 衬底失败注入（**测试专用**——T32-5）
/// @details
/// - `None` = 正常路径（生产恒此值）。
/// - `FailResolve` = 模拟**动态解析失败**——`SetWindowCompositionAttribute` 视为不存在，
///   **不调用未解析函数**，直接进 Degraded（外部评审 P2-4：解析失败分支的独立覆盖）。
/// - `FailCall` = 函数指针有效但**调用返回失败**（验证状态翻转 + 无材质残留 + renderer
///   收到 Degraded 清屏语义——调用路径本身被真实执行，仅结果强制 FALSE）。
/// ⚠️ **进程级全局**：注册表模型串行执行 ⇒ 测试用后必须恢复 `None`；
/// 不得在多窗口并行场景使用（T32-5 注释钉明——外部评审 P2-4 的串行假设）。
enum class BackdropInjection {
	None = 0,
	FailResolve,
	FailCall,
};

/// @brief 设置注入模式（测试专用——用后必须恢复 `None`）
void SetBackdropInjectionForTests(BackdropInjection injection) noexcept;


} // namespace ECDI
