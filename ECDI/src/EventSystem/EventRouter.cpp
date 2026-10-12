#include "ECDI/EventSystem/EventRouter.h"

#include "ECDI/EventSystem/EventDispatcher.h"
#include "ECDI/Core/ECDIAssert.h"   // ★ 审计 M21：FRAMEWORK_ASSERT（穷尽性收口）
#include "ECDI/EventSystem/Window/WindowCreatedEvent.h"
#include "ECDI/EventSystem/Window/WindowDestroyEvent.h"
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Window/DropFilesEvent.h"
#include "ECDI/EventSystem/Application/TrayEvent.h"

namespace ECDI{

void EventRouter::OnEvent(const Event& event){

	EventDispatcher dispatcher(event);

	// ★ 审计 M21：`Dispatch<T>` 的返回值被丢弃 ⇒ 第 16 种事件类型会被**静默丢弃**
	//   （无 default、无断言、无日志）。`EventDispatcher.h:34-49` 特意返回 bool 正是
	//   为了能提前退出/收口——这里累计起来做「无一命中」断言，与
	//   `WindowMessageHandler.cpp:378` 对未知 `WM_*BUTTON` 的 `FRAMEWORK_ASSERT(false)`
	//   对齐（原来自相矛盾：那边炸、这边静默）。
	//   ⚠️ Release 下断言为空 ⇒ 仍静默——那是全库一致的断言口径（§7.3 记账项），
	//   本处只把「漏登记」变成 **Debug 可暴露**，不擅自改 Release 语义。
	bool handled = false;

	// 依次尝试分派到各事件类型的具名处理函数
	// （匹配成功后 Dispatch 内部已调用处理函数，未匹配则继续下一个）

	// ── 窗口事件 ────────────────────────────────────
	handled |= dispatcher.Dispatch<WindowCreatedEvent>([this](const WindowCreatedEvent& e){

			OnWindowCreated(e);

		});

	handled |= dispatcher.Dispatch<WindowDestroyedEvent>([this](const WindowDestroyedEvent& e){

			OnWindowDestroyed(e);

		});

	handled |= dispatcher.Dispatch<WindowResizedEvent>([this](const WindowResizedEvent& e){

			OnWindowResized(e);

		});

	handled |= dispatcher.Dispatch<WindowStateChangedEvent>([this](const WindowStateChangedEvent& e){

			OnWindowStateChanged(e);

		});

	handled |= dispatcher.Dispatch<WindowCloseRequestedEvent>([this](const WindowCloseRequestedEvent& e){

			OnWindowCloseRequested(e);

		});

	handled |= dispatcher.Dispatch<TimerEvent>([this](const TimerEvent& e){

			OnTimer(e);

		});

	// ── 鼠标事件 ────────────────────────────────────
	handled |= dispatcher.Dispatch<MouseMoveEvent>([this](const MouseMoveEvent& e){

			OnMouseMove(e);

		});

	handled |= dispatcher.Dispatch<MouseButtonDownEvent>([this](const MouseButtonDownEvent& e){

			OnMouseButtonDown(e);

		});


	handled |= dispatcher.Dispatch<MouseButtonUpEvent>([this](const MouseButtonUpEvent& e){

			OnMouseButtonUp(e);

		});

	handled |= dispatcher.Dispatch<MouseWheelEvent>([this](const MouseWheelEvent& e){

			OnMouseWheel(e);

		});

	// ── 键盘事件 ────────────────────────────────────
	handled |= dispatcher.Dispatch<KeyDownEvent>([this](const KeyDownEvent& e){

			OnKeyDown(e);

		});

	handled |= dispatcher.Dispatch<KeyUpEvent>([this](const KeyUpEvent& e){

			OnKeyUp(e);

		});

	handled |= dispatcher.Dispatch<CharInputEvent>([this](const CharInputEvent& e){

			OnCharInput(e);

		});

	// ── Phase 14：拖入 / 托盘 ────────────────────────
	handled |= dispatcher.Dispatch<DropFilesEvent>([this](const DropFilesEvent& e){

			OnDropFiles(e);

		});

	handled |= dispatcher.Dispatch<TrayEvent>([this](const TrayEvent& e){

			OnTrayEvent(e);

		});

	// ★ 审计 M21：穷尽性收口。当前 15 种事件全部覆盖；**漏登记第 16 种时 Debug 立刻暴露**
	//   （Release 仍静默——全库一致的断言口径，见 §7.3 记账项）。
	FRAMEWORK_ASSERT(handled);

}

}
