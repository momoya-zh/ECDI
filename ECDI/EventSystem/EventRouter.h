#pragma once

#include"Event.h"

class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class WindowCloseRequestedEvent;
class MouseMoveEvent;
class MouseButtonUpEvent;
class MouseButtonDownEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;

class EventRouter{
public:

	virtual ~EventRouter() = default;

	void OnEvent(const Event&event);

protected:

	virtual void OnWindowCreated(
		const WindowCreatedEvent& event
	){}

	virtual void OnWindowDestroyed(
		const WindowDestroyedEvent&event
	){}

	virtual void OnWindowResized(
		const WindowResizedEvent&event
	){}

	virtual void OnWindowCloseRequested(
		const WindowCloseRequestedEvent&evennt
	){}

	virtual void OnMouseMove(
		const MouseMoveEvent& event
	){}

	virtual void OnMouseButtonDown(
		const MouseButtonDownEvent& event
	){}

	virtual void OnMouseButtonUp(
		const MouseButtonUpEvent& event
	){}

	virtual void OnMouseWheel(
		const MouseWheelEvent& event
	){}

	virtual void OnKeyDown(
		const KeyDownEvent& event
	){}

	virtual void OnKeyUp(
		const KeyUpEvent& event
	){}

	virtual void OnCharInput(
		const CharInputEvent& event
	){}

};

