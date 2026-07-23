#pragma once

#include"Event.h"

class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class WindowCloseRequestedEvent;

class EventRouter{
public:

	virtual ~EventRouter() = default;

	void OnEvent(const Event&event);

protected:

	virtual void OnWindowCreated(
		const WindowCreatedEvent& event
	){

	}

	virtual void OnWindowDestroyed(
		const WindowDestroyedEvent&event
	){
	
	}

	virtual void OnWindowResized(
		const WindowResizedEvent&event
	){}

	virtual void OnWindowCloseRequested(
		const WindowCloseRequestedEvent&evennt
	){}
};

