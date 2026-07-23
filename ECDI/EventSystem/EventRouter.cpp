#include "EventRouter.h"

#include "EventDispatcher.h"

#include "WindowCreatedEvent.h"
#include "WindowDestroyEvent.h"
#include "WindowResizedEvent.h"
#include "WindowCloseRequsted.h"

void EventRouter::OnEvent(
    const Event& event
)
{
    EventDispatcher dispatcher(event);


    dispatcher.Dispatch<WindowCreatedEvent>(
        [this](const WindowCreatedEvent& e)
        {
            OnWindowCreated(e);
        });


    dispatcher.Dispatch<WindowDestroyedEvent>(
        [this](const WindowDestroyedEvent& e)
        {
            OnWindowDestroyed(e);
        });


    dispatcher.Dispatch<WindowResizedEvent>(
        [this](const WindowResizedEvent& e)
        {
            OnWindowResized(e);
        });


    dispatcher.Dispatch<WindowCloseRequestedEvent>(
        [this](const WindowCloseRequestedEvent& e)
        {
            OnWindowCloseRequested(e);
        });
}