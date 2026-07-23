#pragma once

#include "Event.h"

#include <utility>


class EventDispatcher{
public:

	explicit EventDispatcher(const Event& event):m_event(event){
	}


	template<typename T, typename Function>

	bool Dispatch(Function&& function) const{

		if(m_event.GetType() == T::StaticType()){

			std::forward<Function>(function)(

				static_cast<const T&>(m_event)
			);

			return true;
		}

		return false;
	}


private:

	const Event& m_event;

};