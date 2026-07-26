#include "EventRouter.h"

#include "EventDispatcher.h"

#include "Window/WindowCreatedEvent.h"
#include "Window/WindowDestroyEvent.h"
#include "Window/WindowResizedEvent.h"
#include "Window/WindowCloseRequsted.h"
#include"Input/Mouse/MouseMoveEvent.h"
#include "Input/Mouse/MouseButtonDownEvent.h"
#include "Input/Mouse/MouseButtonUpEvent.h"
#include "Input/Mouse/MouseWheelEvent.h"
#include "Input/KeyBoard/KeyDownEvent.h"
#include "Input/KeyBoard/KeyUpEvent.h"
#include "Input/KeyBoard/CharInputEvent.h"

void EventRouter::OnEvent(
	const Event& event
)
{
	EventDispatcher dispatcher(event);


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