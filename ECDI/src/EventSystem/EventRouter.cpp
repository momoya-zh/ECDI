#include "ECDI/EventSystem/EventRouter.h"

#include "ECDI/EventSystem/EventDispatcher.h"
#include "ECDI/EventSystem/Window/WindowCreatedEvent.h"
#include "ECDI/EventSystem/Window/WindowDestroyEvent.h"
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"

namespace ECDI{

void EventRouter::OnEvent(const Event& event){

	EventDispatcher dispatcher(event);

	// 依次尝试分派到各事件类型的具名处理函数
	// （匹配成功后 Dispatch 内部已调用处理函数，未匹配则继续下一个）

	// ── 窗口事件 ────────────────────────────────────
	dispatcher.Dispatch<WindowCreatedEvent>([this](const WindowCreatedEvent& e){

			OnWindowCreated(e);

		});

	dispatcher.Dispatch<WindowDestroyedEvent>([this](const WindowDestroyedEvent& e){

			OnWindowDestroyed(e);

		});

	dispatcher.Dispatch<WindowResizedEvent>([this](const WindowResizedEvent& e){

			OnWindowResized(e);

		});

	dispatcher.Dispatch<WindowCloseRequestedEvent>([this](const WindowCloseRequestedEvent& e){

			OnWindowCloseRequested(e);

		});

	dispatcher.Dispatch<TimerEvent>([this](const TimerEvent& e){

			OnTimer(e);

		});

	// ── 鼠标事件 ────────────────────────────────────
	dispatcher.Dispatch<MouseMoveEvent>([this](const MouseMoveEvent& e){

			OnMouseMove(e);

		});

	dispatcher.Dispatch<MouseButtonDownEvent>([this](const MouseButtonDownEvent& e){

			OnMouseButtonDown(e);

		});


	dispatcher.Dispatch<MouseButtonUpEvent>([this](const MouseButtonUpEvent& e){

			OnMouseButtonUp(e);

		});

	dispatcher.Dispatch<MouseWheelEvent>([this](const MouseWheelEvent& e){

			OnMouseWheel(e);

		});

	// ── 键盘事件 ────────────────────────────────────
	dispatcher.Dispatch<KeyDownEvent>([this](const KeyDownEvent& e){

			OnKeyDown(e);

		});

	dispatcher.Dispatch<KeyUpEvent>([this](const KeyUpEvent& e){

			OnKeyUp(e);

		});

	dispatcher.Dispatch<CharInputEvent>([this](const CharInputEvent& e){

			OnCharInput(e);

		});

}

}
