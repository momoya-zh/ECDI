#pragma once

#include "ECDI/Core/Size.h"
#include "ECDI/Widget/CaretGeometry.h"

namespace ECDI{

class PlatformWindowHost;   // 前置声明（构造注入 Host&）

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

	/// @brief 更新文本输入插入点（5.6 双通道：系统 caret + ImmSetCompositionWindow；
	/// 7.1.3 参数升级 CaretGeometry——插入点矩形 + 逻辑可见性）
	/// @param geometry 插入点几何（框架层 CaretGeometry，非 Win32 类型；坐标系语义封装在实现内）
	virtual void UpdateTextInputCaret(const CaretGeometry& geometry) = 0;

	/// @brief 销毁文本输入插入点（幂等）
	virtual void DestroyTextInputCaret() = 0;
};

}
