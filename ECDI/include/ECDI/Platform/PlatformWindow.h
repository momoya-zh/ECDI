#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Widget/CaretGeometry.h"

#include <string>

namespace ECDI{

class PlatformWindowHost;   // 前置声明（构造注入 Host&）
class PlatformRenderContext;   // 前置声明（GetRenderContext 返回 const&——零 include 依赖，7.1.4）

/// @brief 平台窗口抽象（7.1）：平台负责"窗口存在"，框架负责"窗口里面发生什么"
/// @details Window 组合此接口——Window 不接触 HWND/创建细节；
/// 生命周期 + 平台能力（重绘请求/客户区查询/文本输入插入点）下沉。
/// 唯一实现：Win32PlatformWindow（X11/Wayland 只留接口，YAGNI）。
/// 零 Win32 类型——接口全部用框架层类型（Size/CaretGeometry），平台细节封装在实现内。
class PlatformWindow{
public:
	virtual ~PlatformWindow() = default;

	/// @brief 显示窗口
	virtual void Show() = 0;

	/// @brief 销毁底层窗口句柄（幂等——重复调用返回 true 不报错）
	virtual bool Release() noexcept = 0;

	/// @brief 请求重绘整个客户区（异步可合并——契约语义，非平台细节）
	virtual void Invalidate() = 0;

	/// @brief 客户区尺寸（框架层 Size，非 Win32 RECT——类型封装在实现内）
	virtual Size GetClientSize() const = 0;

	/// @brief 平台渲染上下文（7.1.4：后端经此拿平台句柄——"参数识别"→"平台返回"，
	/// Window 层零识别；识别发生在平台实现内部 static_cast）
	virtual const PlatformRenderContext& GetRenderContext() const = 0;

	/// @brief 更新文本输入插入点（5.6 双通道：系统 caret + ImmSetCompositionWindow；
	/// 7.1.3 参数升级 CaretGeometry——插入点矩形 + 逻辑可见性）
	/// @param geometry 插入点几何（框架层 CaretGeometry，非 Win32 类型；坐标系语义封装在实现内）
	virtual void UpdateTextInputCaret(const CaretGeometry& geometry) = 0;

	/// @brief 销毁文本输入插入点（幂等）
	virtual void DestroyTextInputCaret() = 0;

	/// @brief 从系统剪贴板读取文本（8.5.1；平台能力——UTF-8，转换封装在实现内）
	/// @return 空字符串 = 剪贴板无文本数据（或非文本格式）
	virtual std::string GetClipboardText() const = 0;

	/// @brief 写入文本到系统剪贴板（8.5.1；UTF-8）
	/// @param text 待写入文本（空串 = 清空剪贴板——调用方负责避免误清）
	virtual void SetClipboardText(const std::string& text) = 0;

	/// @brief 启动周期定时器（8.5.1；通用平台能力——ID 语义由调用方定义，平台不知道业务）
	/// @param timerId    定时器标识（调用方自定义；重复 Start 同 id = 重置周期）
	/// @param intervalMs 触发间隔（毫秒）
	virtual void StartTimer(int timerId, unsigned int intervalMs) = 0;

	/// @brief 停止定时器（幂等——未启动/已停止返回无动作）
	virtual void StopTimer(int timerId) = 0;
};

}
