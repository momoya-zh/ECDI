#pragma once

#include "ECDI/EventSystem/EventType.h"

namespace ECDI{

class Window;

/// @brief Framework 所有事件的基类
/// @details
/// 事件是轻量的"已发生的事实"，只携带数据，不负责创建/分发/监听/处理。
///
/// 设计原则：
/// - m_window：事件来源窗口
/// - m_handled：传播层状态（由 Application 控制，Widget 只读取）
/// - 虚 GetType()：用于 EventDispatcher 的类型分派
///
/// ★★ 审计 M22 修复（2026-10-13）：`m_handled` 此前是**死状态**——`SetHandled()`
///    编译通过但**无任何效果**（全库 0 个调用点；`Application` 的 6 个冒泡点
///    一律不查它）。⇒ 本批把**已声明的契约接上**：冒泡循环在每次派发后检查
///    `IsHandled()`，已置位则**停止向上冒泡**。
///    ★ 定线 = **实现**而非删除：`Event.h` 自述「由 Application 控制，Widget 只读取」
///      + `SetHandled()` 本就是 public —— 这是**已设计好但未接线**的能力；
///      删掉它等于砍掉一个既有公共 API。同族先例：Phase 20 的 DPI 契约
///      「**不是引入新概念，而是让既有契约生效**」。
///    ★ **零行为变化**（修复前全库 0 处调用 `SetHandled()`）：没有任何控件置位
///      ⇒ 没有任何一次冒泡会提前终止 ⇒ 既有行为逐位不变。新增的是**能力**。
class Event{

public:

	virtual ~Event() = default;

	/// @brief 获取事件来源窗口
	Window* GetWindow() const noexcept{

		return m_window;

	}

	/// @brief 获取事件类型（子类实现，用于 StaticType 分派）
	virtual EventType GetType() const = 0;

	/// @brief 事件是否已被处理
	bool IsHandled() const noexcept{

		return m_handled;

	}

	/// @brief 标记事件为已处理（**const 方法**——控件在 `const Event&` 回调里调用）
	/// @details ★★ 审计 M22 修复（2026-10-13）：本方法必须是 **const**，
	///          因为框架的全部事件回调签名都是 `const Event&`（`Widget.h:192-201`：
	///          `OnMouseMove(const MouseMoveEvent&)` 等）⇒ 非 const 的 `SetHandled()`
	///          **根本没法从控件里调用**——这正是它成为死代码的**机制性原因**之一
	///          （另一原因：Application 的冒泡循环不查 `IsHandled()`；两处都已修）。
	///          ★ `m_handled` 相应声明为 `mutable`：它是**传播层状态**而非事件数据
	///          （`Event.h` 类注释自述「由 Application 控制，Widget 只读取/标记」），
	///          const 事件对象上改它不破坏「事件是不可变事实」的契约。
	///          ★ **签名放宽而非收紧**：此前无人能调用 ⇒ 加 const 不破坏任何既有调用点。
	void SetHandled() const noexcept{

		m_handled = true;

	}

protected:

	/// @param window 事件来源窗口
	explicit Event(Window* window): m_window(window) {

	}

protected:

	Window* m_window=nullptr;	///< 事件来源窗口

	/// @brief 是否已被处理（传播层状态——★ `mutable`：经 const 事件的 `SetHandled()` 标记，
	///                 见该方法注释；它是传播状态而非事件数据）
	mutable bool m_handled = false;
};

}
