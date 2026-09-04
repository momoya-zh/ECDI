#include "ECDI/Animation/Easing.h"

#include <algorithm>

namespace ECDI{

float ApplyEasing(Easing easing, float t) noexcept{

	switch (easing){

	case Easing::Linear:	return t;
	case Easing::EaseIn:	return t * t;
	case Easing::EaseOut:	return 1.0f - (1.0f - t) * (1.0f - t);
	case Easing::EaseInOut:	return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
	default:				return t;

	}

}

}

