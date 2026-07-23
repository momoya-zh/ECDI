#pragma once

#include "EventType.h"

class Event{
public:

	virtual ~Event() = default;

	virtual EventType GetType() const = 0;
};

