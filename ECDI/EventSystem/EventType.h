#pragma once

enum class EventType{

	None = 0,
	
	//Window
	WindowCreated,
	WindowDestroyed,
	WindowCloseRequested,
	WindowResized,

	// Mouse
	MouseMove,
	MouseButtonDown,
	MouseButtonUp,
	MouseWheel,

	// Keyboard
	KeyDown,
	KeyUp,
	CharInput
};